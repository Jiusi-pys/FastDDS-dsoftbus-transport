"""One fixed public-rclpy process in a five-before/five-after restart scene."""
import argparse
import base64
import hashlib
import os
from pathlib import Path
import re
import runpy
import stat
import sys
import time

BASE = Path(__file__).with_name('actionbag_runtime_probe.py')
if hashlib.sha256(BASE.read_bytes()).hexdigest() != '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df':
    raise ValueError('Frozen ordinary probe changed')
OrdinaryRuntimeProbe = runpy.run_path(str(BASE))['OrdinaryRuntimeProbe']


class RestartLedger:
    def __init__(self, scenario, instance):
        if not re.fullmatch('[0-9a-f]{16}', scenario) or instance not in ('A', 'B0', 'B1'):
            raise ValueError('Exact scenario/instance required')
        self.scenario, self.instance = scenario, instance
        self.next = 5 if instance == 'B1' else 0
        self.sent = self.next
        self.limit = 5 if instance == 'B0' else 10

    def payload(self, sequence):
        if type(sequence) is not int or not 0 <= sequence < 10:
            raise ValueError('Sequence outside finite scene')
        return 'r27_' + self.scenario + '_' + str(sequence).zfill(2)

    def send(self):
        if self.instance != 'A' or self.sent != self.next or self.next >= self.limit:
            raise ValueError('One outstanding request only')
        result = self.payload(self.sent)
        self.sent += 1
        return result

    def receive(self, text):
        if self.next >= self.limit or text != self.payload(self.next):
            raise ValueError('Duplicate, foreign or out-of-order payload')
        if self.instance == 'A' and self.sent != self.next + 1:
            raise ValueError('Reply without request')
        result = self.next
        self.next += 1
        return result


class ProcessRestartProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        if args.side != args.instance[0] or args.role != ('requester' if args.side == 'A' else 'responder'):
            raise ValueError('Instance role/side mismatch')
        self.ledger = RestartLedger(args.scenario, args.instance)
        self.topic = '/restart_' + args.scenario
        self.node_name = 'restart_' + args.run + '_' + args.instance
        self.phase_done, self.completed_at = set(), None
        self.post_accepted = False
        self.publisher = None

    def emit(self, event, **fields):
        super().emit(event, scenario=self.args.scenario, instance=self.args.instance,
                     monotonic_ns=time.monotonic_ns(), **fields)

    def post_requested(self):
        path = self.root / 'control.PROCESS_POST'
        if not path.exists() and not path.is_symlink():
            return False
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600 or before.st_size > 256:
            raise RuntimeError('POST control must be bounded regular0600 file')
        text = path.read_text(encoding='ascii')
        after = path.lstat()
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (
                after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('POST control changed during read')
        if text != ('R27_POST schema=1 scenario=' + self.args.scenario + ' run=' + self.args.run +
                    ' side=A pid=' + str(os.getpid()) + '\n'):
            raise RuntimeError('POST control identity mismatch')
        return True

    def content(self, event, sequence, message):
        from rclpy.serialization import serialize_message
        data = serialize_message(message)
        self.emit(event, sequence=sequence, text=message.data, cdr_base64=base64.b64encode(data).decode('ascii'),
                  cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_bytes=len(data))

    def receive(self, message):
        try:
            sequence = self.ledger.receive(message.data)
            self.content('RECEIVE', sequence, message)
            if self.args.side == 'B':
                self.publisher.publish(message)
                self.content('ECHO', sequence, message)
            if self.ledger.next in (5, 10):
                self.completed_at = time.monotonic()
        except Exception as error:
            self.failed = repr(error)
            self.emit('FAIL', reason=self.failed)

    def communicate(self):
        import rclpy
        from rclpy.duration import Duration
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        from std_msgs.msg import String
        self.node = None
        subscription = None
        try:
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name)
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
            outgoing, incoming = (self.topic + '/request', self.topic + '/reply') if self.args.side == 'A' else (
                self.topic + '/reply', self.topic + '/request')
            self.publisher = self.node.create_publisher(String, outgoing, qos)
            subscription = self.node.create_subscription(String, incoming, self.receive, qos)
            self.emit('ENTITY_READY', outgoing=outgoing, incoming=incoming)
            due = time.monotonic()
            while time.monotonic() < self.deadline:
                if self.failed or not rclpy.ok():
                    raise RuntimeError(self.failed or 'Context stopped')
                if self.finish_requested():
                    wanted = {'PRE'} if self.args.instance == 'B0' else {'POST'} if self.args.instance == 'B1' else {'PRE', 'POST'}
                    if self.phase_done != wanted:
                        raise RuntimeError('Normal finish before required phases')
                    return
                rclpy.spin_once(self.node, timeout_sec=0.01)
                if self.failed:
                    raise RuntimeError(self.failed)
                now = time.monotonic()
                counts = (self.publisher.get_subscription_count(), self.node.count_publishers(incoming))
                if any(n > 1 for n in counts):
                    raise RuntimeError('Unexpected extra endpoint')
                if self.args.instance == 'A' and 'PRE' in self.phase_done and not self.post_accepted and self.post_requested():
                    self.post_accepted = True
                    self.emit('POST_ACCEPTED')
                allow = self.ledger.next < 5 or self.post_accepted
                if (self.args.side == 'A' and allow and self.ledger.next < 10 and
                        self.ledger.sent == self.ledger.next and counts == (1, 1) and now >= due):
                    sequence = self.ledger.next
                    message = String(data=self.ledger.send())
                    self.publisher.publish(message)
                    self.content('SEND', sequence, message)
                    due = time.monotonic() + 0.1
                phase = 'PRE' if self.ledger.next == 5 else 'POST' if self.ledger.next == 10 else None
                if phase and phase not in self.phase_done and self.completed_at is not None and now - self.completed_at >= 0.25:
                    # Fast DDS 2.14 defaults to a 3-second periodic heartbeat.
                    # Keep S1's finite ACK cap within the original process deadline.
                    remaining = min(5, self.deadline - time.monotonic())
                    if remaining <= 0 or not self.publisher.wait_for_all_acked(Duration(nanoseconds=int(remaining * 1e9))):
                        raise RuntimeError('Bounded acknowledgement failed')
                    self.phase_done.add(phase)
                    self.emit('PHASE_COMPLETE', phase=phase, received=self.ledger.next)
            raise RuntimeError('Fixed40s process deadline exceeded')
        except BaseException as error:
            # Native close can wait for host release: expose the cause first.
            self.emit('FAIL', reason=repr(error))
            raise
        finally:
            primary, errors = sys.exc_info()[1], []
            for entity, method in ((subscription, 'destroy_subscription'), (self.publisher, 'destroy_publisher')):
                if entity is not None:
                    try:
                        if getattr(self.node, method)(entity) is not True:
                            raise RuntimeError('Destruction returned false')
                    except BaseException as error:
                        errors.append(method + ': ' + repr(error))
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
                self.emit('FAIL', reason='Cleanup failed', cleanup_errors=errors, primary_failure=repr(primary))
                if primary is None:
                    raise RuntimeError('; '.join(errors))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('run', 'scenario', 'instance', 'side', 'role', 'finish-control'):
        parser.add_argument('--' + name, required=True)
    probe = ProcessRestartProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
