"""One ordinary public-rclpy role in one admitted process. Never spawns children."""
import argparse
import base64
import hashlib
import importlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import stat
import sys
import sysconfig
import time


class OrdinaryRuntimeProbe:
    def __init__(self, args):
        if not re.fullmatch('[0-9a-f]{16}', args.run) or args.side not in ('A', 'B'):
            raise ValueError('Exact run/physical side required')
        self.args, self.sequence, self.failed = args, 0, None
        self.root = Path('/data/local/tmp/mdds_dds_' + args.run + '_' + args.side)
        self.finish = self.root / 'control.PROBE_FINISH'
        if args.finish_control != str(self.finish):
            raise ValueError('Only the exact owned finish file is permitted')
        self.package = Path(__file__).resolve().parent.parent
        self.manifest = json.loads((self.package / 'LAUNCHER_PACKAGE.json').read_text())['files']
        self.node_name = 'ab_' + args.run + '_' + args.side
        self.topic, self.action = '/actionbag_' + args.run, '/fibonacci_' + args.run
        self.payload = 'payload_' + args.run
        self.goal_count, self.data_count, self.publications = 0, 0, 0
        self.finish_identity = None

    def emit(self, event, **fields):
        self.sequence += 1
        print('R25_PROBE ' + json.dumps({'schema': 1, 'run': self.args.run, 'side': self.args.side,
            'pid': os.getpid(), 'role': self.args.role, 'seq': self.sequence, 'event': event, **fields},
            sort_keys=True, separators=(',', ':')), flush=True)

    def finish_requested(self):
        if not self.finish.exists() and not self.finish.is_symlink(): return False
        before = self.finish.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600:
            raise RuntimeError('Finish control is not an owned regular0600 file')
        identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
        if self.finish_identity is not None and identity != self.finish_identity:
            raise RuntimeError('Finish control replaced')
        text = self.finish.read_text(encoding='ascii')
        after = self.finish.lstat()
        if identity != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('Finish control changed during read')
        match = re.fullmatch('R25_PROBE_FINISH schema=1 run=' + self.args.run + ' side=' + self.args.side +
            ' pid=' + str(os.getpid()) + ' reason=(NORMAL_COMPLETION|WINDOW_FAILED)\n', text)
        if not match: raise RuntimeError('Finish control identity/schema mismatch')
        self.finish_identity = identity
        self.emit('FINISH_ACCEPTED', reason=match[1])
        if match[1] != 'NORMAL_COMPLETION': raise RuntimeError('Host latched window failure')
        return True

    def imports_cdr(self):
        if sys.version_info[:3] != (3, 12, 7) or sysconfig.get_config_var('SOABI') != 'cpython-312-aarch64-linux-ohos':
            raise RuntimeError('Unexpected target CPython ABI')
        origins = {}
        for name in ('rclpy', 'rclpy._rclpy_pybind11', 'ros2cli.cli', 'ament_index_python', 'rpyutils',
                     'ros2node.command.node', 'ros2topic.command.topic', 'ros2action.command.action',
                     'example_interfaces.action'):
            module = importlib.import_module(name)
            path = Path(module.__file__).resolve()
            relative = path.relative_to(self.package).as_posix()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if not relative.startswith('python/') or self.manifest.get(relative) != digest:
                raise RuntimeError('Unselected module origin: ' + name)
            origins[name] = {'path': str(path), 'sha256': digest}
        versions = {name: importlib.metadata.version(name) for name in
            ('ros2cli', 'ros2node', 'ros2topic', 'ros2action', 'ros2pkg', 'ros2interface', 'example_interfaces')}
        if any(v != ('0.12.1' if n == 'example_interfaces' else '0.32.12') for n, v in versions.items()):
            raise RuntimeError('Unselected CLI/types version')
        entrypoints = {}
        for name in ('ros2cli', 'ros2node', 'ros2topic', 'ros2action'):
            entrypoints[name] = sorted((e.group, e.name, e.value) for e in importlib.metadata.distribution(name).entry_points)
        from example_interfaces.action import Fibonacci
        from std_msgs.msg import String
        from rclpy.serialization import serialize_message, deserialize_message
        capsules = {}
        for label, cls in [('Action', Fibonacci), ('Goal', Fibonacci.Goal), ('Result', Fibonacci.Result), ('Feedback', Fibonacci.Feedback)]:
            meta = type(cls)
            meta.__import_type_support__()
            fields = ['_TYPE_SUPPORT'] if label == 'Action' else [
                '_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
            if any(type(getattr(meta, field, None)).__name__ != 'PyCapsule' for field in fields):
                raise RuntimeError('Real typesupport capsules missing: ' + label)
            capsules[label] = fields
        cdr = []
        for label, cls, message, field, expected in [
            ('Goal', Fibonacci.Goal, Fibonacci.Goal(order=6), 'order', 6),
            ('Result', Fibonacci.Result, Fibonacci.Result(sequence=[0, 1, 1, 2, 3, 5, 8]), 'sequence', [0, 1, 1, 2, 3, 5, 8]),
            ('Feedback', Fibonacci.Feedback, Fibonacci.Feedback(sequence=[0, 1, 1, 2]), 'sequence', [0, 1, 1, 2]),
            ('String', String, String(data=self.payload), 'data', self.payload)]:
            data = serialize_message(message)
            if not isinstance(data, bytes) or len(data) <= 4 or data[:4] not in (b'\x00\x00\x00\x00', b'\x00\x01\x00\x00'):
                raise RuntimeError('Unexpected CDR encapsulation: ' + label)
            actual = getattr(deserialize_message(data, cls), field)
            if isinstance(expected, list): actual = list(actual)
            if actual != expected: raise RuntimeError('CDR roundtrip mismatch: ' + label)
            cdr.append({'type': label, 'field': field, 'value': actual, 'bytes': len(data),
                'sha256': hashlib.sha256(data).hexdigest(), 'base64': base64.b64encode(data).decode(), 'encapsulation': data[:4].hex()})
        maps = Path('/proc/self/maps').read_text()
        self.emit('IMPORTS_CDR_READY', origins=origins, versions=versions, entrypoints=entrypoints,
            capsules=capsules, cdr=cdr, executable=sys.executable, prefix=sys.prefix,
            version=sys.version, soabi=sysconfig.get_config_var('SOABI'),
            extension_suffix=sysconfig.get_config_var('EXT_SUFFIX'), maps=maps, node_created=False)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.finish_requested(): return
            time.sleep(0.05)
        raise RuntimeError('Import hold deadline exceeded')

    def goal(self, request):
        from rclpy.action import GoalResponse
        self.goal_count += 1
        if request.order != 6 or self.goal_count != 1:
            self.failed = 'Unexpected/duplicate goal'
            self.emit('FAIL', reason=self.failed, order=request.order)
            return GoalResponse.REJECT
        return GoalResponse.ACCEPT

    def execute_goal(self, handle):
        from example_interfaces.action import Fibonacci
        uuid = bytes(handle.goal_id.uuid).hex()
        if not re.fullmatch('[0-9a-f]{32}', uuid) or int(uuid, 16) == 0:
            raise RuntimeError('Invalid accepted UUID')
        self.emit('GOAL_ACCEPTED', uuid=uuid, order=handle.request.order)
        sequence = [0, 1]
        for unused in range(5):
            if time.monotonic() >= self.deadline: raise RuntimeError('Goal callback deadline exceeded')
            time.sleep(0.2)
            sequence.append(sequence[-1] + sequence[-2])
            handle.publish_feedback(Fibonacci.Feedback(sequence=sequence))
            self.emit('FEEDBACK', uuid=uuid, sequence=list(sequence))
        handle.succeed()
        self.emit('RESULT_INTENT', uuid=uuid, sequence=list(sequence), status='SUCCEEDED')
        return Fibonacci.Result(sequence=sequence)

    def receive(self, message):
        if message.data != self.payload:
            self.failed = 'Unexpected String payload'
            self.emit('FAIL', reason=self.failed, data=message.data)
            return
        self.data_count += 1
        self.emit('DATA', data=message.data, received=self.data_count, matched=self.node.count_publishers(self.topic))

    def publish(self):
        from std_msgs.msg import String
        self.publications += 1
        matched = self.publisher.get_subscription_count()
        self.publisher.publish(String(data=self.payload))
        self.emit('PUBLISH', data=self.payload, published=self.publications, matched=matched)

    def communicate(self):
        import rclpy
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        from std_msgs.msg import String
        self.node = None
        timer = publisher = subscription = server = None
        try:
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name)
            self.emit('NODE_READY', name='/' + self.node_name)
            self.deadline = time.monotonic() + 40
            qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
            if self.args.role == 'publisher':
                self.publisher = publisher = self.node.create_publisher(String, self.topic, qos)
                timer = self.node.create_timer(0.2, self.publish)
            elif self.args.role == 'subscriber':
                subscription = self.node.create_subscription(String, self.topic, self.receive, qos)
            elif self.args.role == 'action-server':
                from rclpy.action import ActionServer
                from example_interfaces.action import Fibonacci
                server = ActionServer(self.node, Fibonacci, self.action, self.execute_goal, goal_callback=self.goal)
            self.emit('ENTITY_READY', entity=self.args.role, name=self.action if server else self.topic if publisher or subscription else '/' + self.node_name)
            while time.monotonic() < self.deadline:
                if self.failed: raise RuntimeError(self.failed)
                if self.finish_requested(): return
                rclpy.spin_once(self.node, timeout_sec=0.05)
            raise RuntimeError('Role deadline exceeded')
        finally:
            primary = sys.exc_info()[1]
            cleanup_errors = []
            for entity, destroy in ((timer, 'destroy_timer'), (subscription, 'destroy_subscription'), (publisher, 'destroy_publisher')):
                if entity is not None:
                    try: getattr(self.node, destroy)(entity)
                    except BaseException as error: cleanup_errors.append(destroy + ': ' + repr(error))
            if server is not None:
                try: server.destroy()
                except BaseException as error: cleanup_errors.append('ActionServer.destroy: ' + repr(error))
            if self.node is not None:
                try:
                    self.node.destroy_node()
                    self.emit('NODE_DESTROY_RETURN', returned=True)
                except BaseException as error: cleanup_errors.append('destroy_node: ' + repr(error))
            try:
                rclpy.try_shutdown()
                self.emit('SHUTDOWN_RETURN', returned=True)
            except BaseException as error: cleanup_errors.append('try_shutdown: ' + repr(error))
            if cleanup_errors:
                self.emit('FAIL', reason='Cleanup failures', cleanup_errors=cleanup_errors, primary_failure=repr(primary))
                if primary is None: raise RuntimeError('; '.join(cleanup_errors))


def ordinary_probe_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', choices=('imports-cdr', 'hold-node', 'publisher', 'subscriber', 'action-server'), required=True)
    parser.add_argument('--run', required=True)
    parser.add_argument('--side', choices=('A', 'B'), required=True)
    parser.add_argument('--finish-control', required=True)
    args = parser.parse_args(argv)
    probe = OrdinaryRuntimeProbe(args)
    try:
        if args.role == 'imports-cdr': probe.imports_cdr()
        else: probe.communicate()
        probe.emit('FINAL', success=True, node_created=args.role != 'imports-cdr')
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(ordinary_probe_main())
