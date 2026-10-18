"""Fixed 200-String round trips in one public Node per process; no reconnect.

Uses the accepted finish-control contract and runtime package. Host model tests
are not evidence of native communication. A separate admitted runner must own
deployment, paired close, process identity, and native-path verification.
"""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import runpy
import sys
import time

ORDINARY = Path(__file__).with_name('actionbag_runtime_probe.py')
ORDINARY_SHA = '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df'
if hashlib.sha256(ORDINARY.read_bytes()).hexdigest() != ORDINARY_SHA:
    raise ValueError('Selected public probe/finish implementation changed')
OrdinaryRuntimeProbe = runpy.run_path(str(ORDINARY))['OrdinaryRuntimeProbe']
COUNT = 200
INTERVAL = 0.1


class RoundtripLedger:
    """Strict local sequence accounting, with no application retry or dedupe."""
    def __init__(self, run, side):
        if not re.fullmatch('[0-9a-f]{16}', run) or side not in ('A', 'B'):
            raise ValueError('Exact run and side required')
        self.run, self.side, self.sent, self.received = run, side, 0, 0

    def payload(self, sequence):
        if type(sequence) is not int or not 0 <= sequence < COUNT:
            raise ValueError('Sequence outside fixed run')
        return 'r26_' + self.run + '_' + str(sequence).zfill(3)

    def next_send(self):
        if self.side != 'A' or self.sent != self.received:
            raise ValueError('Only one outstanding request is permitted')
        return self.payload(self.sent)

    def sent_request(self, text):
        if text != self.next_send():
            raise ValueError('Unexpected sent request')
        self.sent += 1

    def receive(self, text):
        if self.received >= COUNT or text != self.payload(self.received):
            raise ValueError('Unexpected, duplicate, excess or out-of-order payload')
        if self.side == 'A' and self.sent != self.received + 1:
            raise ValueError('Reply has no outstanding request')
        sequence = self.received
        self.received += 1
        return sequence


def process_resources():
    status = Path('/proc/self/status').read_text(encoding='ascii')
    result = {}
    for name in ('VmRSS', 'VmHWM', 'Threads'):
        match = re.search(r'^' + name + r':\s+(\d+)(?:\s+(kB))?\s*$', status, re.M)
        if not match or (name != 'Threads') != (match[2] == 'kB'):
            raise RuntimeError('Missing or malformed process resource: ' + name)
        result[name] = int(match[1])
    result['fd_count'] = len(list(Path('/proc/self/fd').iterdir()))
    result['monotonic_ns'] = time.monotonic_ns()
    return result


class SustainedRoundtripProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        expected = 'requester' if args.side == 'A' else 'responder'
        if args.role != expected:
            raise ValueError('Role must match physical side')
        self.ledger = RoundtripLedger(args.run, args.side)
        self.node_name = 'roundtrip_' + args.run + '_' + args.side
        self.request_topic = '/roundtrip_' + args.run + '/request'
        self.reply_topic = '/roundtrip_' + args.run + '/reply'
        self.publisher = None
        self.completed_at = None

    def content(self, event, sequence, message):
        from rclpy.serialization import serialize_message
        data = serialize_message(message)
        self.emit(event, sequence=sequence, text=message.data,
                  cdr_base64=base64.b64encode(data).decode('ascii'),
                  cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_bytes=len(data),
                  monotonic_ns=time.monotonic_ns())

    def receive_roundtrip(self, message):
        try:
            sequence = self.ledger.receive(message.data)
            self.content('ROUNDTRIP_RECEIVE', sequence, message)
            if self.args.side == 'B':
                # Return the actual received public message; do not synthesize success.
                self.publisher.publish(message)
                self.content('ROUNDTRIP_ECHO', sequence, message)
            if self.ledger.received % 50 == 0:
                self.emit('RESOURCES', received=self.ledger.received, **process_resources())
            if self.ledger.received == COUNT:
                self.completed_at = time.monotonic()
        except Exception as error:
            self.failed = repr(error)
            self.emit('FAIL', reason=self.failed)

    def communicate_roundtrip(self):
        import rclpy
        from rclpy.duration import Duration
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        from std_msgs.msg import String
        self.node = None
        subscription = None
        complete = False
        try:
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name)
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE,
                             durability=DurabilityPolicy.VOLATILE)
            outgoing, incoming = (self.request_topic, self.reply_topic) if self.args.side == 'A' else (
                self.reply_topic, self.request_topic)
            self.publisher = self.node.create_publisher(String, outgoing, qos)
            subscription = self.node.create_subscription(String, incoming, self.receive_roundtrip, qos)
            self.emit('ENTITY_READY', entity=self.args.role, outgoing=outgoing, incoming=incoming)
            self.emit('RESOURCES', received=0, **process_resources())
            due = time.monotonic()
            while time.monotonic() < self.deadline:
                if self.failed:
                    raise RuntimeError(self.failed)
                if self.finish_requested():
                    if not complete:
                        raise RuntimeError('Normal finish before complete round trips and quiet interval')
                    return
                if not rclpy.ok():
                    raise RuntimeError('Context stopped before normal finish')
                rclpy.spin_once(self.node, timeout_sec=0.01)
                if self.failed:
                    raise RuntimeError(self.failed)
                now = time.monotonic()
                counts = (self.publisher.get_subscription_count(), self.node.count_publishers(incoming))
                if any(n > 1 for n in counts):
                    raise RuntimeError('Unexpected extra endpoint')
                if (self.args.side == 'A' and self.ledger.sent < COUNT and
                        self.ledger.sent == self.ledger.received and counts == (1, 1) and now >= due):
                    sequence = self.ledger.sent
                    message = String(data=self.ledger.next_send())
                    self.publisher.publish(message)
                    self.ledger.sent_request(message.data)
                    self.content('ROUNDTRIP_SEND', sequence, message)
                    due = time.monotonic() + INTERVAL
                if self.completed_at is not None and not complete and now - self.completed_at >= 2:
                    remaining = min(5, self.deadline - time.monotonic())
                    if remaining <= 0 or not self.publisher.wait_for_all_acked(
                            Duration(nanoseconds=int(remaining * 1e9))):
                        raise RuntimeError('Bounded publisher acknowledgement failed')
                    complete = True
                    self.emit('ROUNDTRIP_COMPLETE', received=COUNT, quiet_seconds=now - self.completed_at,
                              monotonic_ns=time.monotonic_ns())
            raise RuntimeError('Fixed40s roundtrip deadline exceeded')
        finally:
            primary, errors = sys.exc_info()[1], []
            for entity, destroy in ((subscription, 'destroy_subscription'), (self.publisher, 'destroy_publisher')):
                if entity is not None:
                    try:
                        if getattr(self.node, destroy)(entity) is not True:
                            raise RuntimeError('Entity destruction returned false')
                    except BaseException as error:
                        errors.append(destroy + ': ' + repr(error))
            if self.node is not None:
                try:
                    self.node.destroy_node()
                    self.emit('NODE_DESTROY_RETURN', returned=True)
                except BaseException as error:
                    errors.append('destroy_node: ' + repr(error))
            try:
                rclpy.try_shutdown()
                self.emit('SHUTDOWN_RETURN', returned=True)
            except BaseException as error:
                errors.append('try_shutdown: ' + repr(error))
            if errors:
                self.emit('FAIL', reason='Roundtrip cleanup failed', cleanup_errors=errors,
                          primary_failure=repr(primary))
                if primary is None:
                    raise RuntimeError('; '.join(errors))


def sustained_roundtrip_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', choices=('requester', 'responder'), required=True)
    parser.add_argument('--side', choices=('A', 'B'), required=True)
    parser.add_argument('--run', required=True)
    parser.add_argument('--finish-control', required=True)
    probe = SustainedRoundtripProbe(parser.parse_args(argv))
    try:
        probe.communicate_roundtrip()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(sustained_roundtrip_main())
