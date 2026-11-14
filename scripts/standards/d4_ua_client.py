"""Independent asyncua wire/semantic oracle; no gateway/DDS Python code imported."""
import argparse
import asyncio
from dataclasses import dataclass
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import time

from asyncua import Client, ua
from asyncua.common.utils import Buffer
from asyncua.ua import ua_binary


def demand(ok, message):
    if not ok:
        raise RuntimeError(message)


async def wait(predicate, label, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        await asyncio.sleep(.01)
    raise RuntimeError('oracle timeout: ' + label)


class Oracle:
    def __init__(self, args):
        self.args, self.calls = args, []
        self.indices = {str(p): len(list(p.glob('response-*.txt'))) for p in args.peers}

    async def command(self, peer, lines):
        index = self.indices[str(peer)]
        temp = peer / 'command.tmp'
        temp.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        temp.replace(peer / ('request-' + str(index) + '.txt'))
        response = peer / ('response-' + str(index) + '.txt')
        await wait(response.exists, 'native response')
        values = response.read_text(encoding='utf-8').splitlines()
        demand(len(values) == 3, 'native response fields')
        self.indices[str(peer)] += 1
        self.calls.append(dict(peer=str(peer), index=index, input=lines, response=values))
        return int(values[0]), bytes.fromhex(values[1]), values[2]

    async def rpc(self, op, request, response_type, peer=None, service_code=0):
        data = ua_binary.struct_to_binary(request)
        prefix = ua_binary.nodeid_to_binary(request.TypeId)
        demand(data.startswith(prefix), 'independent UA request TypeId')
        status, body, token = await self.command(peer or self.args.peers[0], [str(op), 'local', data[len(prefix):].hex()])
        demand(status == 0 and body, 'D4 transport status/body')
        expected = response_type()
        buffer = Buffer(ua_binary.nodeid_to_binary(expected.TypeId) + body)
        result = ua_binary.struct_from_binary(response_type, buffer)
        demand(not buffer, 'trailing UA response data')
        demand(result.ResponseHeader.ServiceResult.value == service_code, 'UA service result: ' + str(result.ResponseHeader.ServiceResult))
        return result


async def services(oracle):
    async with Client(oracle.args.endpoint) as direct:
        ns = await direct.get_namespace_index('urn:fastdds:standards:d4:independent')
        folder, node = ua.NodeId('Sensors', ns), ua.NodeId('SensorA', ns)
        read = ua.ReadRequest()
        read.Parameters.NodesToRead = [ua.ReadValueId(NodeId=node, AttributeId=ua.AttributeIds.Value)]
        if oracle.args.mode == 'capacity':
            for index, peer in enumerate(oracle.args.peers):
                if index < 4:
                    response = await oracle.rpc(8, read, ua.ReadResponse, peer)
                    demand(response.Results[0].Value.Value == 10, 'session capacity successful read')
                else:
                    raw = ua_binary.struct_to_binary(read)[len(ua_binary.nodeid_to_binary(read.TypeId)):]
                    code, body, _ = await oracle.command(peer, ['8', 'local', raw.hex()])
                    demand(code == ua.StatusCodes.BadTooManySessions and not body, 'session capacity rejection')
            return
        browse = ua.BrowseRequest()
        browse.Parameters.RequestedMaxReferencesPerNode = 2
        browse.Parameters.NodesToBrowse = [ua.BrowseDescription(NodeId=folder, BrowseDirection=ua.BrowseDirection.Forward,
                                                              IncludeSubtypes=True, NodeClassMask=ua.NodeClass.Variable, ResultMask=ua.BrowseResultMask.All)]
        response = await oracle.rpc(1, browse, ua.BrowseResponse)
        refs = response.Results[0].References
        demand(len(refs) == 2 and response.Results[0].ContinuationPoint, 'Browse first page')
        next_req = ua.BrowseNextRequest()
        next_req.Parameters.ReleaseContinuationPoints = False
        next_req.Parameters.ContinuationPoints = [response.Results[0].ContinuationPoint]
        response = await oracle.rpc(2, next_req, ua.BrowseNextResponse)
        refs += response.Parameters.Results[0].References
        demand({r.NodeId for r in refs} == {ua.NodeId('Sensor' + s, ns) for s in 'ABC'} | {ua.NodeId('Large', ns)}, 'Browse/BrowseNext actual nodes')
        translate = ua.TranslateBrowsePathsToNodeIdsRequest()
        path = ua.BrowsePath(StartingNode=folder)
        path.RelativePath.Elements = [ua.RelativePathElement(ReferenceTypeId=ua.NodeId(ua.ObjectIds.HierarchicalReferences), IsInverse=False, IncludeSubtypes=True, TargetName=ua.QualifiedName('SensorA', ns))]
        translate.Parameters.BrowsePaths = [path]
        response = await oracle.rpc(3, translate, ua.TranslateBrowsePathsToNodeIdsResponse)
        demand(response.Results[0].Targets[0].TargetId == node, 'Translate actual target')
        register = ua.RegisterNodesRequest()
        register.Parameters.NodesToRegister = [node]
        response = await oracle.rpc(4, register, ua.RegisterNodesResponse)
        demand(response.Parameters.RegisteredNodeIds == [node], 'RegisterNodes result')
        unregister = ua.UnregisterNodesRequest()
        unregister.Parameters.NodesToUnregister = [node]
        await oracle.rpc(5, unregister, ua.UnregisterNodesResponse)
        query = ua.QueryFirstRequest()
        query.Parameters.NodeTypes = [ua.NodeTypeDescription(TypeDefinitionNode=ua.NodeId(ua.ObjectIds.BaseDataVariableType),
                                                           DataToReturn=[ua.QueryDataDescription(AttributeId=ua.AttributeIds.Value)])]
        query.Parameters.MaxDataSetsToReturn = 1
        element = ua.ContentFilterElement(FilterOperator=ua.FilterOperator.GreaterThan,
                                         FilterOperands=[ua.SimpleAttributeOperand(TypeDefinitionId=ua.NodeId(ua.ObjectIds.BaseDataVariableType), AttributeId=ua.AttributeIds.Value),
                                                         ua.LiteralOperand(Value=ua.Variant(15, ua.VariantType.UInt32))])
        query.Parameters.Filter.Elements = [element]
        first = await oracle.rpc(6, query, ua.QueryFirstResponse)
        demand(first.Parameters.QueryDataSets[0].Values[0].Value == 20 and first.Parameters.ContinuationPoint, 'Query first filtered page')
        qnext = ua.QueryNextRequest()
        qnext.Parameters.ReleaseContinuationPoint = False
        qnext.Parameters.ContinuationPoint = first.Parameters.ContinuationPoint
        await oracle.rpc(7, qnext, ua.QueryNextResponse, oracle.args.peers[1], ua.StatusCodes.BadContinuationPointInvalid)
        response = await oracle.rpc(7, qnext, ua.QueryNextResponse)
        demand([d.Values[0].Value for d in response.Parameters.QueryDataSets] == [30] and not response.Parameters.RevisedContinuationPoint, 'Query next correlated page')
        await oracle.rpc(7, qnext, ua.QueryNextResponse, service_code=ua.StatusCodes.BadContinuationPointInvalid)
        first = await oracle.rpc(6, query, ua.QueryFirstResponse)
        qnext.Parameters.ContinuationPoint = first.Parameters.ContinuationPoint
        qnext.Parameters.ReleaseContinuationPoint = True
        response = await oracle.rpc(7, qnext, ua.QueryNextResponse)
        demand(not response.Parameters.QueryDataSets and not response.Parameters.RevisedContinuationPoint, 'Query release')
        response = await oracle.rpc(8, read, ua.ReadResponse)
        demand(response.Results[0].Value.Value == 10, 'Read actual value')
        write = ua.WriteRequest()
        write.Parameters.NodesToWrite = [ua.WriteValue(NodeId=node, AttributeId=ua.AttributeIds.Value, Value=ua.DataValue(ua.Variant(77, ua.VariantType.UInt32), SourceTimestamp=datetime.now(timezone.utc)))]
        response = await oracle.rpc(9, write, ua.WriteResponse)
        demand(response.Results[0].is_good() and await direct.get_node(node).read_value() == 77, 'Write observed independently')
        call = ua.CallRequest()
        call.Parameters.MethodsToCall = [ua.CallMethodRequest(ObjectId=folder, MethodId=ua.NodeId('Add', ns), InputArguments=[ua.Variant(3, ua.VariantType.UInt32), ua.Variant(8, ua.VariantType.UInt32)])]
        response = await oracle.rpc(12, call, ua.CallResponse)
        demand(response.Results[0].StatusCode.is_good() and response.Results[0].OutputArguments[0].Value == 11, 'Call actual result')
        update = ua.HistoryUpdateRequest()
        update.Parameters.HistoryUpdateDetails = [ua.UpdateDataDetails(NodeId=node, PerformInsertReplace=ua.PerformUpdateType.Insert,
                   UpdateValues=[ua.DataValue(Value=ua.Variant(88, ua.VariantType.UInt32), SourceTimestamp=datetime.now(timezone.utc))])]
        response = await oracle.rpc(11, update, ua.HistoryUpdateResponse)
        demand(response.Results[0].StatusCode.is_good() and response.Results[0].OperationResults[0].is_good(), 'HistoryUpdate actual insert')
        history = ua.HistoryReadRequest()
        history.Parameters.HistoryReadDetails = ua.ReadRawModifiedDetails(IsReadModified=False, ReturnBounds=False, StartTime=datetime.now(timezone.utc)-timedelta(minutes=1), EndTime=datetime.now(timezone.utc)+timedelta(minutes=1), NumValuesPerNode=16)
        history.Parameters.ReleaseContinuationPoints = False
        history.Parameters.TimestampsToReturn = ua.TimestampsToReturn.Source
        history.Parameters.NodesToRead = [ua.HistoryReadValueId(NodeId=node)]
        response = await oracle.rpc(10, history, ua.HistoryReadResponse)
        demand(88 in [v.Value.Value for v in response.Results[0].HistoryData.DataValues], 'HistoryRead observes inserted history')
        for op, label, body, expected in [(100, 'local', b'', ua.StatusCodes.BadServiceUnsupported),
                                         (8, 'foreign', b'', ua.StatusCodes.BadServerNameMissing),
                                         (8, 'local', b'\xff', ua.StatusCodes.BadDecodingError),
                                         (8, 'local', ua_binary.struct_to_binary(read)[len(ua_binary.nodeid_to_binary(read.TypeId)):] + b'\x00', ua.StatusCodes.BadDecodingError)]:
            status, result, _ = await oracle.command(oracle.args.peers[0], [str(op), label, body.hex()])
            demand(status == expected and not result, 'explicit RPC failure: ' + str(expected))
        read.Parameters.NodesToRead[0].NodeId = ua.NodeId('Large', ns)
        raw = ua_binary.struct_to_binary(read)[len(ua_binary.nodeid_to_binary(read.TypeId)):]
        status, result, _ = await oracle.command(oracle.args.peers[0], ['8', 'local', raw.hex()])
        demand(status == ua.StatusCodes.BadEncodingLimitsExceeded and not result, 'response size rejection')


@dataclass
class HistoryRecord:
    id: ua.UInt32 = 0
    sequence_number: ua.UInt32 = 0
    label: ua.ByteString = b''
    payload: ua.ByteString = b''


async def history(oracle):
    async with Client(oracle.args.endpoint) as client:
        ns = await client.get_namespace_index('urn:fastdds:standards:d4:history')
        ua.register_extension_object('D4HistoryRecord', ua.NodeId(3002, ns), HistoryRecord, ua.NodeId(3001, ns))
        nodeid = lambda field='sequence_number', key=7: ua.NodeId('Instance_' + str(key) + '_' + field, ns)
        async def publish(sequence, key=7, kind='write', source=None):
            label = bytes([128]) + b'x' * 63 if sequence == 6 else ('sample' + str(sequence)).encode()
            payload = bytes(range(256)) if sequence == 6 else bytes([0, 255, sequence])
            if sequence == 1:
                label, payload = b'', b''
            seconds, nanos = source or (int(time.time()), sequence * 1000000)
            await oracle.command(oracle.args.peers[0], [kind, str(key), str(sequence), str(seconds), str(nanos), label.hex(), payload.hex()])
            deadline = time.monotonic() + 8
            while True:
                try:
                    value = await client.get_node(nodeid(key=key)).read_data_value(raise_on_bad_status=False)
                    if kind == 'dispose' and value.StatusCode.value == ua.StatusCodes.BadOutOfService:
                        break
                    if kind == 'write' and value.StatusCode.is_good() and value.Value.Value == sequence:
                        break
                except ua.UaStatusCodeError:
                    pass
                demand(time.monotonic() < deadline, 'actual DDS-to-UA observation')
                await asyncio.sleep(.01)
            return label, payload, seconds

        async def read(field='sequence_number', cp=None, page=16, release=False, reverse=False, bounds=False, other=None, start=None, end=None, key=7, stamps=ua.TimestampsToReturn.Source):
            params = ua.HistoryReadParameters()
            params.HistoryReadDetails = ua.ReadRawModifiedDetails(IsReadModified=False, StartTime=start or datetime(2020, 1, 1, tzinfo=timezone.utc), EndTime=end or datetime(2035, 1, 1, tzinfo=timezone.utc), NumValuesPerNode=page, ReturnBounds=bounds)
            if reverse:
                params.HistoryReadDetails.StartTime, params.HistoryReadDetails.EndTime = params.HistoryReadDetails.EndTime, params.HistoryReadDetails.StartTime
            params.TimestampsToReturn = stamps
            params.ReleaseContinuationPoints = release
            params.NodesToRead = [ua.HistoryReadValueId(NodeId=nodeid(field, key), ContinuationPoint=cp)]
            return (await (other or client).uaclient.history_read(params))[0]

        if oracle.args.mode == 'restart-after':
            try:
                await client.get_node(nodeid()).read_value()
                raise RuntimeError('history unexpectedly persisted after restart')
            except ua.UaStatusCodeError as error:
                demand(error.code == ua.StatusCodes.BadNodeIdUnknown, 'restart missing node error')
            await publish(2)
            result = await read()
            demand([d.Value.Value for d in result.HistoryData.DataValues] == [2], 'restart historical cache reset')
            return
        await publish(1)
        demand(await client.get_node(nodeid('payload')).read_value() == b'', 'empty member ByteString is not UA Null')
        empty = await client.get_node(nodeid('Value')).read_value()
        demand(empty.label == b'' and empty.payload == b'', 'empty aggregate octets are not UA Null')
        if oracle.args.mode == 'restart-before':
            demand([v.Value.Value for v in (await read()).HistoryData.DataValues] == [1], 'before restart history')
            return
        if oracle.args.mode == 'expiry':
            await publish(2)
            first = await read(page=1)
            demand(first.ContinuationPoint and first.HistoryData.DataValues[0].Value.Value == 1, 'expiry initial page')
            await asyncio.sleep(oracle.args.retention_ms / 1000 + .15)
            demand((await read(cp=first.ContinuationPoint)).StatusCode.value == ua.StatusCodes.BadContinuationPointInvalid, 'expired continuation')
            result = await read()
            demand(result.StatusCode.value == ua.StatusCodes.GoodNoData and not result.HistoryData.DataValues, 'retention time window expires')
            demand(await client.get_node(nodeid()).read_value() == 2, 'live value retained after historical expiry')
            return
        expected = {}
        for sequence in range(2, 7):
            expected[sequence] = await publish(sequence)
        node = client.get_node(nodeid())
        demand((await node.read_attribute(ua.AttributeIds.Historizing)).Value.Value is True, 'Historizing advertised')
        demand((await node.read_attribute(ua.AttributeIds.AccessLevel)).Value.Value & (1 << 2), 'HistoryRead advertised')
        result = await read()
        demand([v.Value.Value for v in result.HistoryData.DataValues] == [3, 4, 5, 6], 'bounded historical count')
        first = await read(page=2)
        demand([v.Value.Value for v in first.HistoryData.DataValues] == [3, 4] and first.ContinuationPoint, 'historical first page')
        async with Client(oracle.args.endpoint) as other:
            cross = await read(cp=first.ContinuationPoint, other=other)
            demand(cross.StatusCode.value == ua.StatusCodes.BadContinuationPointInvalid, 'history cross-session continuation rejected')
        await publish(7)
        second = await read(cp=first.ContinuationPoint)
        demand([v.Value.Value for v in second.HistoryData.DataValues] == [5, 6] and not second.ContinuationPoint, 'immutable historical snapshot across new sample')
        demand((await read(cp=first.ContinuationPoint)).StatusCode.value == ua.StatusCodes.BadContinuationPointInvalid, 'history token single-use')
        result = await read(reverse=True)
        demand([v.Value.Value for v in result.HistoryData.DataValues] == [7, 6, 5, 4], 'reverse source timestamp order')
        aggregate = await read('Value')
        record = [v for v in aggregate.HistoryData.DataValues if v.Value.Value.sequence_number == 6][0]
        demand(isinstance(record.Value.Value, HistoryRecord) and record.Value.Value.id == 7 and record.Value.Value.label == expected[6][0]
               and record.Value.Value.payload == expected[6][1], 'full historical record fields')
        stamp = datetime.fromtimestamp(expected[6][2], timezone.utc) + timedelta(microseconds=6000)
        demand(abs((record.SourceTimestamp - stamp).total_seconds()) <= .000002, 'source timestamp preservation within UA Python/RTPS clock precision')
        for field, expected_value in [('id', 7), ('label', expected[6][0]), ('payload', expected[6][1])]:
            values = (await read(field)).HistoryData.DataValues
            demand(values[-2].Value.Value == expected_value, 'member historical mapping ' + field)
        narrow = await read(start=record.SourceTimestamp - timedelta(microseconds=1), end=record.SourceTimestamp + timedelta(microseconds=1))
        demand([v.Value.Value for v in narrow.HistoryData.DataValues] == [6], 'source time range selection')
        demand((await read(bounds=True)).StatusCode.value == ua.StatusCodes.BadHistoryOperationUnsupported, 'unsupported bound interpolation rejected')
        first = await read(page=1)
        demand((await read(cp=first.ContinuationPoint, release=True)).StatusCode.is_good(), 'explicit history cursor release')
        demand((await read(cp=first.ContinuationPoint)).StatusCode.value == ua.StatusCodes.BadContinuationPointInvalid, 'released cursor cannot replay')
        points = []
        for _ in range(8):
            item = await read(page=1)
            demand(item.StatusCode.is_good() and item.ContinuationPoint, 'bounded history cursor allocation')
            points.append(item.ContinuationPoint)
        demand((await read(page=1)).StatusCode.value == ua.StatusCodes.BadNoContinuationPoints, 'history cursor capacity rejected')
        for point in points:
            demand((await read(cp=point, release=True)).StatusCode.is_good(), 'cursor capacity recovery')
        await publish(9, kind='dispose')
        result = await read()
        demand(any(v.StatusCode.value == ua.StatusCodes.BadOutOfService for v in result.HistoryData.DataValues), 'DDS lifecycle quality retained historically')
        await publish(8)
        demand((await node.read_data_value()).StatusCode.is_good(), 'DDS resurrection quality')
        await publish(10, key=8)
        value = await client.get_node(nodeid(key=8)).read_value()
        demand(value == 10 and await node.read_value() == 8, 'keyed history isolation')
        same_source = (int(time.time()), 250000000)
        await publish(11, key=9, source=same_source)
        await publish(12, key=9, source=same_source)
        duplicate = await read(key=9)
        demand([v.Value.Value for v in duplicate.HistoryData.DataValues] == [12], 'raw duplicate source time selects newest version')
        demand(duplicate.HistoryData.DataValues[0].StatusCode.value & 0x408 == 0x408, 'raw newest version advertises historian ExtraData')
        first_server = (await read(key=9, stamps=ua.TimestampsToReturn.Server)).HistoryData.DataValues[0]
        await asyncio.sleep(.05)
        second_server = (await read(key=9, stamps=ua.TimestampsToReturn.Server)).HistoryData.DataValues[0]
        both = (await read(key=9, stamps=ua.TimestampsToReturn.Both)).HistoryData.DataValues[0]
        demand(first_server.SourceTimestamp is None and first_server.ServerTimestamp is not None
               and first_server.ServerTimestamp == second_server.ServerTimestamp == both.ServerTimestamp and both.SourceTimestamp is not None,
               'historical receipt ServerTimestamp is stable across queries and timestamp modes')
        minimum = datetime(1601, 1, 1, tzinfo=timezone.utc)
        for parameters in (dict(start=minimum, end=minimum, page=16), dict(start=minimum, end=minimum, page=0),
                           dict(start=datetime(2020, 1, 1, tzinfo=timezone.utc), end=minimum, page=0)):
            invalid = await read(key=9, **parameters)
            demand(invalid.StatusCode.value == ua.StatusCodes.BadInvalidArgument
                   and (not invalid.HistoryData or not invalid.HistoryData.DataValues), 'under-specified raw time domain is rejected')
        for timestamps in (ua.TimestampsToReturn.Invalid, ua.TimestampsToReturn.Neither):
            try:
                await read(key=9, stamps=timestamps)
                raise RuntimeError('invalid historical timestamp selector accepted')
            except ua.UaStatusCodeError as error:
                demand(error.code == ua.StatusCodes.BadTimestampsToReturnInvalid, 'historical timestamp parameter error code')
        forward = (await read(end=minimum)).HistoryData.DataValues
        reverse = (await read(start=minimum)).HistoryData.DataValues
        demand(forward and [v.SourceTimestamp for v in forward] == sorted(v.SourceTimestamp for v in forward), 'open-ended forward historical range')
        demand(reverse and [v.SourceTimestamp for v in reverse] == sorted((v.SourceTimestamp for v in reverse), reverse=True), 'open-ended backward historical range')


async def main(args):
    report = dict(status='FAIL', mode=args.mode)
    oracle = Oracle(args)
    (args.directory / 'ready').touch()
    await wait(lambda: (args.directory / 'go').exists(), 'oracle go')
    try:
        if args.mode in ('services', 'capacity'):
            await services(oracle)
        else:
            await history(oracle)
        report.update(status='PASS')
    finally:
        report['calls'] = oracle.calls
        (args.directory / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('D4_UA_CLIENT_CLOSED exit=0 owners=0', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', required=True)
    parser.add_argument('--peers', type=Path, action='append', required=True)
    parser.add_argument('--endpoint', required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--retention-ms', type=int, default=2000)
    asyncio.run(main(parser.parse_args()))
