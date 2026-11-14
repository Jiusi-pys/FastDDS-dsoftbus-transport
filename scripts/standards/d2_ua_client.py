"""Independent asyncua acceptance client; no gateway/native model code is imported."""
import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import time

from asyncua import Client, ua
import asyncua


def require(ok, text):
    if not ok:
        raise RuntimeError(text)


def encode(key, sequence, label, payload):
    return struct.pack('<IIi', key, sequence, len(label)) + label + struct.pack('<i', len(payload)) + payload


def decode(value, namespace):
    require(isinstance(value, ua.ExtensionObject) and value.Body is not None, 'unknown custom type must remain an ExtensionObject')
    require(value.TypeId == ua.NodeId(3002, namespace), 'wrong BoundedRecord binary encoding NodeId')
    body = value.Body
    key, sequence, label_size = struct.unpack_from('<IIi', body)
    require(-1 <= label_size <= 64, 'encoded label bounds')
    label_size = max(0, label_size)  # UA null and empty ByteString both map to the empty bounded DDS value.
    require(len(body) >= 16 + label_size, 'encoded label length')
    payload_size = struct.unpack_from('<i', body, 12 + label_size)[0]
    require(-1 <= payload_size <= 256 and len(body) == 16 + label_size + max(0, payload_size), 'encoded payload bounds')
    return key, sequence, body[12:12 + label_size], body[16 + label_size:]


class Handler:
    def __init__(self):
        self.events = []
        self.overflow = False

    def datachange_notification(self, node, value, data):
        if len(self.events) >= 128:
            self.overflow = True
            return
        self.events.append((node.nodeid.Identifier, value, data.monitored_item.Value.StatusCode.value))


async def run(args):
    control = args.control.resolve()
    serial = max([0, *[int(p.name.split('-')[1]) for p in control.glob('command-[0-9]*') if p.suffix != '.pending']])
    operations = []

    async def send(op, key, sequence=0, label=b'', payload=b''):
        nonlocal serial
        serial += 1
        pending = control / f'command-{serial}.pending'
        pending.write_text(f'{serial} {op} {key} {sequence} {label.hex() or "-"} {payload.hex() or "-"}\n', encoding='ascii')
        pending.replace(control / f'command-{serial}')
        operations.append(dict(serial=serial, op=op, key=key, sequence=sequence))

    async def until(check, what, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if await check():
                return
            await asyncio.sleep(.025)
        raise RuntimeError('timeout: ' + what)

    # UnregisterInstance includes a checked native ACK fence with a five-second maximum.
    async with Client(args.url, timeout=8, watchdog_intervall=8) as client:
        ns = await client.get_namespace_index('urn:fastdds:standards:d2')
        wire_expected = (7, 9, b'a', b'\x00\xff')
        wire_body = encode(*wire_expected)
        require(decode(ua.ExtensionObject(TypeId=ua.NodeId(3002, ns), Body=wire_body), ns) == wire_expected, 'wire positive control')
        for wrong in (ua.NodeId(9999, ns), ua.NodeId(3002, ns + 1)):
            try:
                decode(ua.ExtensionObject(TypeId=wrong, Body=wire_body), ns)
            except RuntimeError:
                pass
            else:
                raise RuntimeError('wrong wire encoding identity accepted')
        root = await client.nodes.objects.get_child(f'{ns}:DDS')
        types = await root.get_child(f'{ns}:Types')
        datatype = await types.get_child(f'{ns}:BoundedRecord')
        require(await datatype.read_node_class() == ua.NodeClass.DataType, 'DataType browse')
        require((await (await datatype.get_child(f'{ns}:Fields')).read_value()).startswith('id:UInt32:key;'), 'type field metadata')
        domain = await root.get_child(f'{ns}:Domain_{args.domain}')
        require(await (await domain.get_child(f'{ns}:DomainId')).read_value() == args.domain, 'DomainId')
        topic = await domain.get_child(f'{ns}:{args.topic}')
        require(await (await topic.get_child(f'{ns}:RegisteredTypeName')).read_value() == 'Standards::BoundedRecord', 'registered type')
        methods = {name: await topic.get_child(f'{ns}:{name}') for name in ('RegisterInstance', 'WriteSample', 'DisposeInstance', 'UnregisterInstance')}
        print('D2_UA_CONNECTED stack=' + asyncua.__version__, flush=True)

        def node(key, field):
            return client.get_node(ua.NodeId(f'Instance_{key}/{field}', ns))

        async def data(key):
            return await node(key, 'Value').read_attribute(ua.AttributeIds.Value, raise_on_bad_status=False)

        async def expect_record(expected):
            try:
                dv = await data(expected[0])
                return dv.StatusCode.is_good() and decode(dv.Value.Value, ns) == expected
            except ua.UaStatusCodeError:
                return False

        async def state(key, expected):
            try:
                return await node(key, 'InstanceState').read_value() == expected and not await node(key, 'ValidData').read_value()
            except ua.UaStatusCodeError:
                return False

        async def call(name, *values):
            return await topic.call_method(methods[name], *values)

        async def witnessed(key, valid, instance_state, sequence=0, label=b'', payload=b''):
            line = f'D2_PEER_DATA key={key} valid={int(valid)} state={instance_state}'
            if valid:
                line += f' sequence={sequence} label={label.hex() or "-"} payload={payload.hex() or "-"}'
            async def observed():
                return line + '\n' in (control / 'peer.log').read_text(errors='replace')
            await until(observed, 'independent native DDS witness: ' + line)

        async def write_value(key, variant):
            # This profile writes values only; DDS owns source timestamps and quality.
            await node(key, 'Value').write_value(ua.DataValue(Value=variant, StatusCode=None))

        async def rejected(awaitable, codes):
            try:
                await awaitable
            except ua.UaStatusCodeError as error:
                require(error.code in codes, f'wrong rejection {error.code:#x}, expected {codes}')
                return
            raise RuntimeError('invalid UA operation was accepted')

        u32 = lambda value: ua.Variant(value, ua.VariantType.UInt32)
        blob = lambda value: ua.Variant(value, ua.VariantType.ByteString)
        if args.mode in ('capacity', 'max-capacity'):
            capacity = 2 if args.mode == 'capacity' else 64
            for key in range(201, 201 + capacity):
                await call('RegisterInstance', u32(key))
            await rejected(call('RegisterInstance', u32(201 + capacity)), {ua.StatusCodes.BadResourceUnavailable})
            await rejected(call('RegisterInstance', u32(201)), {ua.StatusCodes.BadEntryExists})
            key = 200 + capacity
            require((await data(key)).StatusCode.value == ua.StatusCodes.BadWaitingForInitialData, 'initial value quality')
            await call('WriteSample', u32(key), u32(77), blob(b'ok'), blob(b''))
            await until(lambda: expect_record((key, 77, b'ok', b'')), 'valid instance after capacity rejection')
            result = dict(status='PASS', mode=args.mode, namespace=ns, operations=operations, capacity_rejections=2)
        else:
            key = 101 if args.mode != 'restart2' else 103
            await send('write', key, 0, b'', b'')
            await until(lambda: expect_record((key, 0, b'', b'')), 'first DDS value')
            value_type = await node(key, 'Value').read_data_type()
            require(value_type == datatype.nodeid, 'aggregate datatype identity')
            stamp = await node(key, 'SourceTimestamp').read_value()
            require(abs((datetime.now(timezone.utc) - stamp).total_seconds()) < 20, 'DDS source timestamp')
            handler = Handler()
            subscription = await client.create_subscription(50, handler, queue_maxsize=32)
            try:
                for field in ('sequence_number', 'InstanceState', 'ValidData'):
                    await subscription.subscribe_data_change(node(key, field), sampling_interval=10, queuesize=16)
                await send('write', key, 1, b'changed', b'\x00\x7f\xff')
                await until(lambda: expect_record((key, 1, b'changed', b'\x00\x7f\xff')), 'updated DDS value')
                async def notified():
                    return any(name == f'Instance_{key}/sequence_number' and val == 1 and code == 0 for name, val, code in handler.events)
                await until(notified, 'UA data-change notification')
                await send('dispose', key)
                await until(lambda: state(key, 2), 'DDS disposed metadata')
                disposed = await data(key)
                require(disposed.StatusCode.value == ua.StatusCodes.BadOutOfService and decode(disposed.Value.Value, ns) == (key, 1, b'changed', b'\x00\x7f\xff'), 'disposed retained value and quality')
                await send('write', key, 2, b'alive', bytes(range(256)))
                await until(lambda: expect_record((key, 2, b'alive', bytes(range(256)))), 'DDS resurrection')
            finally:
                await subscription.delete()
            require(not handler.overflow, 'bounded notification collector overflow')
            if args.mode == 'normal':
                maximum = b'\x80' + b'x' * 63
                await send('write', 102, 4294967295, maximum, bytes(range(256)))
                await until(lambda: expect_record((102, 4294967295, maximum, bytes(range(256)))), 'full bounds and UInt32 max')
                await send('unregister', 102)
                await until(lambda: state(102, 4), 'DDS no-writers metadata/key lookup')
                # Prepare only the peer's independent key identity before a lifecycle-only DDS event.
                await send('observe', 77)
                async def expected_key_ready():
                    return f'D2_PEER_SENT serial={serial} op=observe key=77' in (control / 'peer.log').read_text(errors='replace')
                await until(expected_key_ready, 'independent peer expected key')
                await call('RegisterInstance', u32(77))
                await call('DisposeInstance', u32(77))
                await until(lambda: state(77, 2), 'UA dispose before first write')
                require((await data(77)).StatusCode.value == ua.StatusCodes.BadWaitingForInitialData, 'prewrite dispose keeps initial value quality')
                await witnessed(77, False, 2)
                await call('WriteSample', u32(77), u32(1), blob(b'first'), blob(b''))
                await until(lambda: expect_record((77, 1, b'first', b'')), 'prewrite disposed instance resurrection')
                await witnessed(77, True, 1, 1, b'first', b'')
                await call('UnregisterInstance', u32(77))
                await until(lambda: state(77, 4), 'unregister resurrected instance')
                await witnessed(77, False, 4)
                await call('RegisterInstance', u32(78))
                await call('UnregisterInstance', u32(78))
                require(await node(78, 'id').read_value() == 78 and (await data(78)).StatusCode.value == ua.StatusCodes.BadWaitingForInitialData, 'unregister before first write remains live')
                await call('RegisterInstance', u32(201))
                extension = ua.ExtensionObject(TypeId=ua.NodeId(3002, ns), Body=encode(201, 77, b'uawrite', b'\xff\x00'))
                await rejected(node(201, 'Value').write_value(ua.Variant(extension, ua.VariantType.ExtensionObject)), {ua.StatusCodes.BadWriteNotSupported})
                await write_value(201, ua.Variant(extension, ua.VariantType.ExtensionObject))
                await until(lambda: expect_record((201, 77, b'uawrite', b'\xff\x00')), 'UA Write service DDS echo')
                await witnessed(201, True, 1, 77, b'uawrite', b'\xff\x00')
                await rejected(node(201, 'id').write_value(u32(9)), {ua.StatusCodes.BadNotWritable, ua.StatusCodes.BadUserAccessDenied})
                await rejected(call('RegisterInstance', u32(201)), {ua.StatusCodes.BadEntryExists})
                await rejected(call('WriteSample', u32(201), u32(9), blob(b'x' * 65), blob(b'')), {ua.StatusCodes.BadOutOfRange})
                await rejected(call('WriteSample', u32(201), u32(9), blob(b'a\x00b'), blob(b'')), {ua.StatusCodes.BadInvalidArgument})
                await rejected(call('WriteSample', u32(201), u32(9), blob(b''), blob(b'x' * 257)), {ua.StatusCodes.BadOutOfRange})
                await rejected(call('WriteSample', u32(999), u32(9), blob(b''), blob(b'')), {ua.StatusCodes.BadNodeIdUnknown})
                wrong = ua.ExtensionObject(TypeId=ua.NodeId(3002, ns), Body=encode(202, 9, b'', b''))
                await rejected(write_value(201, ua.Variant(wrong, ua.VariantType.ExtensionObject)), {ua.StatusCodes.BadInvalidArgument})
                await rejected(write_value(201, u32(9)), {ua.StatusCodes.BadTypeMismatch})
                await call('RegisterInstance', u32(202))
                await call('WriteSample', u32(202), u32(88), blob(maximum), blob(bytes(range(256))))
                await until(lambda: expect_record((202, 88, maximum, bytes(range(256)))), 'UA WriteSample DDS echo')
                await witnessed(202, True, 1, 88, maximum, bytes(range(256)))
                await call('DisposeInstance', u32(201))
                await until(lambda: state(201, 2), 'UA DisposeInstance DDS echo')
                await witnessed(201, False, 2)
                await call('UnregisterInstance', u32(202))
                await until(lambda: state(202, 4), 'UA UnregisterInstance DDS echo')
                await witnessed(202, False, 4)
                await call('RegisterInstance', u32(202))
                await call('WriteSample', u32(202), u32(89), blob(b'reborn'), blob(b''))
                await until(lambda: expect_record((202, 89, b'reborn', b'')), 'UA re-register resurrection')
                await witnessed(202, True, 1, 89, b'reborn', b'')
            result = dict(status='PASS', mode=args.mode, namespace=ns, operations=operations, notification_events=len(handler.events), independent_stack=asyncua.__version__)
    args.receipt.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('D2_UA_CLIENT PASS mode=' + args.mode, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--domain', type=int, required=True)
    parser.add_argument('--topic', required=True)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--mode', choices=['normal', 'capacity', 'max-capacity', 'restart1', 'restart2'], required=True)
    asyncio.run(run(parser.parse_args()))
