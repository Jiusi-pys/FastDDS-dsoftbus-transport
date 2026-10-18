"""Exactly two normal B replacements, one A, three five-message phases."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import runpy
import stat
import sys
import time

BASE = Path(__file__).with_name('process_restart_probe.py')
if hashlib.sha256(BASE.read_bytes()).hexdigest() != '8d1f113b515f71a0ce766526392b0e12abed3c19f7aac7a57cdda60f5b9fa659':
    raise ValueError('Frozen single-restart probe changed')
previous = runpy.run_path(str(BASE))


class RepeatedLedger:
    def __init__(self, scenario, instance):
        if not re.fullmatch('[0-9a-f]{16}', scenario) or instance not in ('A', 'B0', 'B1', 'B2'):
            raise ValueError('Exact repeated scenario/instance required')
        self.scenario, self.instance = scenario, instance
        phase = 0 if instance == 'A' else int(instance[1])
        self.next = self.sent = phase * 5
        self.limit = 15 if instance == 'A' else (phase + 1) * 5

    def payload(self, sequence):
        if type(sequence) is not int or not 0 <= sequence < 15:
            raise ValueError('Sequence outside two-replacement scene')
        return 'r28_' + self.scenario + '_' + str(sequence).zfill(2)

    def send(self):
        if self.instance != 'A' or self.sent != self.next or self.next >= self.limit:
            raise ValueError('One outstanding request only')
        value = self.payload(self.sent)
        self.sent += 1
        return value

    def receive(self, text):
        if self.next >= self.limit or text != self.payload(self.next):
            raise ValueError('Duplicate, foreign or out-of-order repeated payload')
        if self.instance == 'A' and self.sent != self.next + 1:
            raise ValueError('Reply without request')
        value = self.next
        self.next += 1
        return value


class RepeatedProcessProbe(previous['ProcessRestartProbe']):
    def __init__(self, args):
        previous['OrdinaryRuntimeProbe'].__init__(self, args)
        if args.side != args.instance[0] or args.role != ('requester' if args.side == 'A' else 'responder'):
            raise ValueError('Instance role/side mismatch')
        self.ledger = RepeatedLedger(args.scenario, args.instance)
        self.topic = '/repeat_' + args.scenario
        self.node_name = 'repeat_' + args.run + '_' + args.instance
        self.phase_done, self.completed_at = set(), None
        self.permitted_phase = 0
        self.publisher = None

    def phase_requested(self, phase):
        path = self.root / ('control.REPEAT_PHASE' + str(phase))
        if not path.exists() and not path.is_symlink():
            return False
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600 or before.st_size > 256:
            raise RuntimeError('Phase control must be bounded regular0600 file')
        text = path.read_text(encoding='ascii')
        after = path.lstat()
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (
                after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('Phase control changed during read')
        if text != ('R28_PHASE schema=1 scenario=' + self.args.scenario + ' run=' + self.args.run +
                    ' side=A pid=' + str(os.getpid()) + ' phase=' + str(phase) + '\n'):
            raise RuntimeError('Phase control identity mismatch')
        return True

    def receive(self, message):
        super().receive(message)
        if not self.failed and self.ledger.next == 15:
            self.completed_at = time.monotonic()

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
                    wanted = {0, 1, 2} if self.args.instance == 'A' else {int(self.args.instance[1])}
                    if self.phase_done != wanted:
                        raise RuntimeError('Normal finish before required repeated phases')
                    return
                rclpy.spin_once(self.node, timeout_sec=0.01)
                if self.failed:
                    raise RuntimeError(self.failed)
                now = time.monotonic()
                counts = (self.publisher.get_subscription_count(), self.node.count_publishers(incoming))
                if any(n > 1 for n in counts):
                    raise RuntimeError('Unexpected extra endpoint')
                if self.args.instance == 'A':
                    for phase in (1, 2):
                        if phase <= self.permitted_phase or not self.phase_requested(phase):
                            continue
                        if phase != self.permitted_phase + 1 or phase - 1 not in self.phase_done:
                            raise RuntimeError('Early or skipped phase control')
                        self.permitted_phase = phase
                        self.emit('PHASE_ACCEPTED', phase=phase)
                if (self.args.side == 'A' and self.ledger.next < 15 and
                        self.ledger.next // 5 <= self.permitted_phase and self.ledger.sent == self.ledger.next and
                        counts == (1, 1) and now >= due):
                    sequence = self.ledger.next
                    message = String(data=self.ledger.send())
                    self.publisher.publish(message)
                    self.content('SEND', sequence, message)
                    due = time.monotonic() + 0.1
                phase = self.ledger.next // 5 - 1 if self.ledger.next in (5, 10, 15) else None
                if phase is not None and phase not in self.phase_done and self.completed_at is not None and now - self.completed_at >= 0.25:
                    remaining = min(5, self.deadline - time.monotonic())
                    if remaining <= 0 or not self.publisher.wait_for_all_acked(Duration(nanoseconds=int(remaining * 1e9))):
                        raise RuntimeError('Bounded acknowledgement failed')
                    self.phase_done.add(phase)
                    self.emit('PHASE_COMPLETE', phase=phase, received=self.ledger.next)
            raise RuntimeError('Fixed40s process deadline exceeded')
        except BaseException as error:
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
    probe = RepeatedProcessProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
