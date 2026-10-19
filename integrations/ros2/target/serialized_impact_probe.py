"""Bounded typed/serialized publication experiment; full bytes stay in local logs."""
import argparse
import base64
import hashlib
import importlib
from pathlib import Path
import runpy
import time

BASE = Path(__file__).with_name('parameter_native_probe.py')
if hashlib.sha256(BASE.read_bytes()).hexdigest() != '6eeffdbf3221c6ef115610cdceaef83208399e59bf2fb5a0094a9f49a28c6490':
    raise ValueError('Qualified parameter probe changed')
_base = runpy.run_path(str(BASE))
ParameterProbe, plain = _base['ParameterProbe'], _base['plain']
LANES = ('typed_event', 'serialized_event', 'typed_string', 'serialized_string')
MODULES = ('rclpy', 'rclpy._rclpy_pybind11', 'rclpy.publisher', 'rclpy.subscription',
           'rclpy.serialization', 'rclpy.executors', 'rcl_interfaces.msg', 'std_msgs.msg')
META = ('source_timestamp', 'received_timestamp', 'publication_sequence_number', 'reception_sequence_number')


class SerializedImpactProbe(ParameterProbe):
    def __init__(self, args):
        super().__init__(args)
        self.publishers, self.subscriptions, self.seen = {}, {}, set()
        self.last_spin_ns, self.last_receive = None, None
        self.calls = 0

    def check_time(self):
        if time.monotonic() >= self.deadline:
            raise RuntimeError('Fixed probe deadline exceeded')

    def record(self, event, **fields):
        self.check_time()
        self.emit(event, **fields)
        self.check_time()
        return self.sequence

    def tick(self):
        active = super().tick()
        self.check_time()
        if active:
            self.last_spin_ns = time.monotonic_ns()
        return active

    def lane_topic(self, lane):
        return '/r34_' + self.args.scenario + '/' + lane

    def message(self, lane, index):
        from rcl_interfaces.msg import Parameter, ParameterEvent, ParameterValue
        from builtin_interfaces.msg import Time
        from std_msgs.msg import String
        if lane.endswith('_string'):
            return String(data='R34:' + self.args.scenario + ':' + format(index, '02d') + ':control')
        return ParameterEvent(stamp=Time(sec=100 + index, nanosec=123), node=self.server_name,
            changed_parameters=[Parameter(name=self.prefix + 'bool',
                value=ParameterValue(type=1, bool_value=index == 2))])

    def type_support(self):
        from rclpy.type_support import check_for_type_support
        from rcl_interfaces.msg import ParameterEvent
        from std_msgs.msg import String
        self.classes = {'event': ParameterEvent, 'string': String}
        origins, capsules = {}, {}
        for name in MODULES:
            path = Path(importlib.import_module(name).__file__).resolve()
            member, digest = path.relative_to(self.package).as_posix(), hashlib.sha256(path.read_bytes()).hexdigest()
            if self.manifest.get(member) != digest:
                raise RuntimeError('Selected native module differs')
            origins[name] = {'member': member, 'sha256': digest}
        for name, cls in self.classes.items():
            check_for_type_support(cls)
            capsules[name] = type(cls.__class__._TYPE_SUPPORT).__name__
            if capsules[name] != 'PyCapsule':
                raise RuntimeError('Native type support missing')
        self.emit('TYPE_SUPPORT', origins=origins, capsules=capsules)

    def metadata(self, info):
        # The selected Executor passes the native take's dictionary unchanged.
        if type(info) is not dict or set(info) != set(META):
            raise RuntimeError('Unexpected MessageInfo fields')
        result = {name: info[name] for name in META}
        if any(value is not None and (type(value) is not int or value < 0) for value in result.values()):
            raise RuntimeError('Unexpected MessageInfo representation')
        return result

    def identify(self, lane, value):
        indices = [index for index in (1, 2) if plain(self.message(lane, index)) == value]
        if len(indices) != 1:
            raise RuntimeError('Unexpected message fields')
        return indices[0]

    def buffer(self, lane, data, origin, index):
        if type(data) is not bytes or not 4 <= len(data) <= 65536:
            raise RuntimeError('Unexpected raw buffer type or size')
        return self.record('BUFFER', lane=lane, topic=self.lane_topic(lane), index=index,
            message_type='String' if lane.endswith('_string') else 'ParameterEvent', origin=origin,
            cdr_base64=base64.b64encode(data).decode('ascii'), cdr_bytes=len(data),
            cdr_sha256=hashlib.sha256(data).hexdigest())

    def callback(self, lane, raw):
        # Exactly two positional arguments lets stock rclpy supply MessageInfo.
        def receive(message, info):
            from rclpy.serialization import deserialize_message
            self.check_time()
            ref = None
            if raw:
                ref = self.buffer(lane, message, 'RAW_SUBSCRIPTION', None)
                # The full callback bytes object is passed through unchanged.
                message = deserialize_message(message, self.classes[lane.rsplit('_', 1)[1]])
                self.check_time()
            value = plain(message)
            index = self.identify(lane, value)
            kind = 'raw' if raw else 'typed'
            key = (lane, index, kind)
            self.record('RAW_RECEIVE' if raw else 'TYPED_RECEIVE', lane=lane, topic=self.lane_topic(lane),
                index=index, value=value, buffer_seq=ref, metadata=self.metadata(info))
            if key in self.seen:
                raise RuntimeError('Duplicate receive including after provisional completion')
            self.seen.add(key)
            self.last_receive = time.monotonic()
            self.check_time()
        return receive

    def wait_ready(self):
        previous, observations = None, 0
        remote = '/parameter_' + self.args.scenario + ('_B' if self.args.side == 'A' else '_A')
        while True:
            if not self.context_is_active():
                raise RuntimeError('Finish interrupted discovery')
            snapshot = {}
            for lane in LANES:
                if self.args.side == 'A':
                    matches = [self.publishers[lane].get_subscription_count()]
                    info = self.node.get_subscriptions_info_by_topic(self.lane_topic(lane))
                else:
                    matches = [sub.get_publisher_count() for sub in self.subscriptions[lane]]
                    info = self.node.get_publishers_info_by_topic(self.lane_topic(lane))
                names = sorted(item.node_namespace.rstrip('/') + '/' + item.node_name for item in info)
                snapshot[lane] = {'matches': matches, 'owners': names}
            self.check_time()
            if snapshot != previous:
                if observations >= 64:
                    raise RuntimeError('Discovery observation bound exceeded')
                self.record('DISCOVERY_OBSERVATION', topics=snapshot)
                previous, observations = snapshot, observations + 1
            expected = {'matches': [2], 'owners': [remote, remote]} if self.args.side == 'A' else {
                'matches': [1, 1], 'owners': [remote]}
            if all(row == expected for row in snapshot.values()):
                if not self.context_is_active():
                    raise RuntimeError('Finish interrupted discovery')
                self.record('CLIENT_READY' if self.args.side == 'A' else 'SERVER_READY', topics=snapshot,
                    discovery_observations=observations, qos={'history': 1, 'depth': 10, 'reliability': 1, 'durability': 2})
                if not self.context_is_active():
                    raise RuntimeError('Finish interrupted discovery')
                return
            if not self.tick():
                raise RuntimeError('Finish interrupted discovery')

    def client_work(self):
        from rclpy.serialization import serialize_message, deserialize_message
        self.publishers = {lane: self.node.create_publisher(self.classes[lane.rsplit('_', 1)[1]], self.lane_topic(lane), self.qos)
                           for lane in LANES}
        self.wait_ready()
        # Retain message and buffer ownership until explicit normal teardown.
        self.retained = []
        for index in (1, 2):
            for lane in LANES:
                self.check_time()
                message = self.message(lane, index)
                data = serialize_message(message)
                self.check_time()
                self.retained.append((message, data))
                ref = self.buffer(lane, data, 'LOCAL_SERIALIZE_MESSAGE', index)
                decoded = plain(deserialize_message(data, type(message)))
                self.record('LOCAL_DESERIALIZE', lane=lane, index=index, buffer_seq=ref, value=decoded)
                if decoded != plain(message):
                    raise RuntimeError('Complete local native deserialize differs')
                self.publishers[lane].publish(data if lane.startswith('serialized_') else message)
                self.record('PUBLISH_RETURN', lane=lane, index=index, buffer_seq=ref,
                            input_kind='bytes' if lane.startswith('serialized_') else 'message')
                self.calls += 1
        self.done = True
        self.record('CLIENT_COMPLETE', publish_calls=self.calls)

    def server_work(self):
        self.subscriptions = {lane: [self.node.create_subscription(self.classes[lane.rsplit('_', 1)[1]], self.lane_topic(lane),
            self.callback(lane, raw), self.qos, raw=raw) for raw in (False, True)] for lane in LANES}
        self.wait_ready()
        while self.tick():
            if len(self.seen) == 16 and not self.done and time.monotonic() - self.last_receive >= 0.25:
                self.done = True
                self.record('SERVER_COMPLETE', received=16, quiet_seconds=0.25)
        # All callbacks remain live after SERVER_COMPLETE until finish is observed.

    def communicate(self):
        import rclpy
        from rclpy.qos import QoSProfile, HistoryPolicy, ReliabilityPolicy, DurabilityPolicy
        primary, cleanup = False, []
        try:
            self.type_support()
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name, start_parameter_services=self.args.side == 'B')
            self.deadline = time.monotonic() + 40
            self.qos = QoSProfile(history=HistoryPolicy.KEEP_LAST, depth=10,
                reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
            self.record('NODE_READY', name='/' + self.node_name)
            if self.args.side == 'A':
                self.client_work()
                while self.tick():
                    pass
            else:
                self.server_work()
            self.record('OBSERVATION_END', last_spin_ns=self.last_spin_ns, receives=len(self.seen), publish_calls=self.calls)
        except BaseException as error:
            primary = True
            self.emit('FAIL', reason='Probe operation failed', error_type=type(error).__name__)
            raise
        finally:
            if self.node is not None:
                try:
                    self.node.destroy_node()
                    self.emit('NODE_DESTROY_RETURN', returned=True)
                except BaseException as error:
                    cleanup.append('destroy_node:' + type(error).__name__)
            try:
                rclpy.try_shutdown()
                self.emit('SHUTDOWN_RETURN', returned=True)
            except BaseException as error:
                cleanup.append('try_shutdown:' + type(error).__name__)
            if cleanup:
                self.emit('FAIL', reason='Cleanup failed', cleanup_errors=cleanup)
                if not primary:
                    raise RuntimeError('Cleanup failed')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('run', 'scenario', 'instance', 'side', 'role', 'finish-control'):
        parser.add_argument('--' + name, required=True)
    probe = SerializedImpactProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason='Probe failed', error_type=type(error).__name__)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
