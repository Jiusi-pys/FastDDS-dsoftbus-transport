"""R35 A-only fixture: original publication or typed/raw stock-bag replay."""
import argparse
import base64
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import runpy
import stat
import time

HERE = Path(__file__).parent
if hashlib.sha256((HERE / 'actionbag_runtime_probe.py').read_bytes()).hexdigest() != '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df':
    raise ValueError('Frozen ordinary probe changed')
OrdinaryRuntimeProbe = runpy.run_path(str(HERE / 'actionbag_runtime_probe.py'))['OrdinaryRuntimeProbe']
bag_graph_qos = runpy.run_path(str(HERE / 'bag_recordplay_probe.py'))['bag_graph_qos']
plain = runpy.run_path(str(HERE / 'parameter_native_probe.py'))['plain']
LANES = ('typed_event', 'serialized_event', 'typed_string', 'serialized_string')
CASES = ('record_A_to_B', 'play_default_B_to_A', 'play_no_loan_B_to_A')
META = ('source_timestamp', 'received_timestamp', 'publication_sequence_number', 'reception_sequence_number')


class SerializedBagProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        if (args.side != 'A' or args.case not in CASES or not re.fullmatch('[0-9a-f]{16}', args.record_run) or
                (args.case == CASES[0]) != (args.record_run == args.run) or
                args.role != ('bag-publisher' if args.case == CASES[0] else 'bag-subscriber')):
            raise ValueError('Exact R35 operation and A-side role required')
        self.recording = args.case == CASES[0]
        self.node_name = ('r35bagpub_' if self.recording else 'r35bagsub_') + args.run
        self.remote_name = ('r35bagrec_' if self.recording else 'r35bagplayer_') + args.run
        self.contract_path = self.root / 'r35-contract.json'
        self.resume_path = self.root / 'control.R35_RESUME'
        if args.contract != str(self.contract_path) or not re.fullmatch('[0-9a-f]{64}', args.contract_sha256):
            raise ValueError('Owned exact graph contract required')
        if self.recording:
            if args.resume_control is not None or args.bag_seal_sha256 is not None:
                raise ValueError('Record cannot consume a playback control/seal')
        elif args.resume_control != str(self.resume_path) or not re.fullmatch('[0-9a-f]{64}', args.bag_seal_sha256 or ''):
            raise ValueError('Exact playback Resume and sealed input required')
        self.node = None
        self.publishers, self.subscriptions, self.retained = {}, {}, []
        self.counts = {lane: [0, 0] for lane in LANES}
        self.seen = set()
        self.calls = 0
        self.done = self.resume_issued = self.resume_returned = False
        self.last_receive = self.last_spin_ns = None

    def emit(self, event, **fields):
        self.sequence += 1
        print('R35_PROBE ' + json.dumps(dict(schema=1, scope='R35_PHASED_STOCK_BAG', case=self.args.case,
            run=self.args.run, record_run=self.args.record_run, side='A', pid=os.getpid(), role=self.args.role,
            seq=self.sequence, event=event, monotonic_ns=time.monotonic_ns(), **fields),
            sort_keys=True, separators=(',', ':')), flush=True)

    def check_time(self):
        if time.monotonic() >= self.deadline:
            raise RuntimeError('Fixed Node-create-return40s probe deadline exceeded')

    def tick(self):
        import rclpy
        self.check_time()
        if not rclpy.ok():
            raise RuntimeError('ROS context stopped early')
        if self.finish_requested():
            if not self.done:
                raise RuntimeError('Normal finish before complete business')
            return False
        rclpy.spin_once(self.node, timeout_sec=0.01)
        self.last_spin_ns = time.monotonic_ns()
        self.check_time()
        return True

    def lane_topic(self, lane):
        return ('/r35_' if self.recording else '/r35play_') + self.args.run + '/' + lane

    def message(self, lane, index):
        from std_msgs.msg import String
        from rcl_interfaces.msg import Parameter, ParameterEvent, ParameterValue
        from builtin_interfaces.msg import Time
        if lane.endswith('string'):
            return String(data='R35:' + self.args.record_run + ':' + format(index, '02d') + ':control')
        return ParameterEvent(stamp=Time(sec=100 + index, nanosec=123),
            node='/parameter_' + self.args.record_run + '_B', changed_parameters=[Parameter(
            name='p' + self.args.record_run + '.bool', value=ParameterValue(type=1, bool_value=index == 2))])

    def identify(self, lane, value):
        indices = [i for i in (1, 2) if value == plain(self.message(lane, i))]
        if len(indices) != 1:
            raise RuntimeError('Foreign message fields/index')
        return indices[0]

    def types_and_definitions(self):
        from rclpy.type_support import check_for_type_support
        from rcl_interfaces.msg import ParameterEvent
        from std_msgs.msg import String
        from rosbag2_py import LocalMessageDefinitionSource
        self.classes = {'event': ParameterEvent, 'string': String}
        origins, capsules = {}, {}
        for name in ('rclpy', 'rclpy._rclpy_pybind11', 'rclpy.serialization', 'rclpy.executors',
                     'rclpy.publisher', 'rclpy.subscription', 'rcl_interfaces.msg', 'std_msgs.msg', 'rosbag2_py'):
            path = Path(importlib.import_module(name).__file__).resolve()
            member = path.relative_to(self.package).as_posix()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if self.manifest.get(member) != digest:
                raise RuntimeError('Selected module origin/hash drift')
            origins[name] = dict(member=member, sha256=digest)
        for name, cls in self.classes.items():
            check_for_type_support(cls)
            capsules[name] = type(cls.__class__._TYPE_SUPPORT).__name__
            if capsules[name] != 'PyCapsule':
                raise RuntimeError('Native type support capsule missing')
        self.emit('TYPE_SUPPORT', origins=origins, capsules=capsules)
        definitions = {}
        for name in ('rcl_interfaces/msg/ParameterEvent', 'std_msgs/msg/String'):
            value = LocalMessageDefinitionSource().get_full_text(name)
            if value.topic_type != name or value.encoding != 'ros2msg' or not value.encoded_message_definition:
                raise RuntimeError('Native full definition unavailable')
            definitions[name] = dict(encoding=value.encoding, encoded_message_definition=value.encoded_message_definition,
                                     native_type_hash=value.type_hash)
        self.emit('DEFINITIONS', definitions=definitions)

    def endpoint(self, info):
        value = info.topic_type_hash
        thash = '' if value.version == 0 and value.value == bytes(32) else str(value)
        gid = bytes(info.endpoint_gid)
        if len(gid) != 16 or not any(gid) or (thash and not re.fullmatch('RIHS01_[0-9a-f]{64}', thash)):
            raise RuntimeError('Endpoint GID/type hash representation')
        return dict(owner=info.node_namespace.rstrip('/') + '/' + info.node_name, type=info.topic_type,
            kind='publisher' if int(info.endpoint_type) == 1 else 'subscription', gid=gid.hex(),
            qos=bag_graph_qos(info.qos_profile), type_hash=thash)

    def graph(self):
        node_names = sorted(ns.rstrip('/') + '/' + name for name, ns in self.node.get_node_names_and_namespaces())
        entities = {}
        for name in (self.node_name, self.remote_name):
            entity = {}
            for kind, method in (('publishers', 'get_publisher_names_and_types_by_node'),
                                 ('subscriptions', 'get_subscriber_names_and_types_by_node'),
                                 ('services', 'get_service_names_and_types_by_node'),
                                 ('clients', 'get_client_names_and_types_by_node')):
                rows = getattr(self.node, method)(name, '/')
                if any(len(types) != 1 for _, types in rows) or len({n for n, _ in rows}) != len(rows):
                    raise RuntimeError('Ambiguous node endpoint types')
                entity[kind] = {n: types[0] for n, types in rows}
            entities['/' + name] = entity
        data = {lane: [self.endpoint(i) for i in (self.node.get_subscriptions_info_by_topic(self.lane_topic(lane))
            if self.recording else self.node.get_publishers_info_by_topic(self.lane_topic(lane)))] for lane in LANES}
        return dict(nodes=node_names, entities=entities, data=data)

    def wait_ready(self):
        from rclpy.node import NodeNameNonExistentError
        raw = self.contract_path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != self.args.contract_sha256:
            raise RuntimeError('Graph contract hash differs')
        contract = json.loads(raw)
        previous, stable, changed, pending = None, 0, 0, False
        while self.tick():
            try:
                snapshot = self.graph()
            except NodeNameNonExistentError as missing:
                if type(missing) is not NodeNameNonExistentError:
                    raise
                # Only initial discovery may lack an expected node. A gap breaks
                # consecutive full observations; it never restarts the deadline.
                previous, stable = None, 0
                if not pending:
                    changed += 1
                    if changed > 64:
                        raise RuntimeError('Discovery observation bound exceeded')
                    self.emit('DISCOVERY_PENDING', known_error_type='NodeNameNonExistentError', changed_snapshots=changed)
                    pending = True
                continue
            pending = False
            matches = {lane: [self.publishers[lane].get_subscription_count()] if self.recording else
                       [s.get_publisher_count() for s in self.subscriptions[lane]] for lane in LANES}
            if snapshot != previous:
                changed += 1
                if changed > 64:
                    raise RuntimeError('Discovery observation bound exceeded')
                self.emit('DISCOVERY', snapshot=snapshot, changed_snapshots=changed)
                stable, previous = 0, snapshot
            stable += 1
            owner = '/' + self.remote_name
            expected_count = [1] if self.recording else [1, 1]
            if snapshot['nodes'] != sorted(contract) or snapshot['entities'] != contract:
                continue
            if any(matches[l] != expected_count or len(snapshot['data'][l]) != 1 for l in LANES):
                continue
            for lane, items in snapshot['data'].items():
                item = items[0]
                qos = item['qos']
                if (item['owner'] != owner or item['type'] != self.type_names[lane] or
                        item['kind'] != ('subscription' if self.recording else 'publisher') or
                        qos['reliability'] != 'reliable' or qos['durability'] != 'volatile' or
                        qos['liveliness'] != 'automatic' or qos['history'] != 'unknown' or qos['depth'] != 0 or
                        qos['avoid_ros_namespace_conventions'] is not False):
                    raise RuntimeError('Recorder/player endpoint identity or QoS differs')
            if not self.recording and not self.client.service_is_ready():
                continue
            if stable >= 2:
                if self.seen or any(any(x) for x in self.counts.values()):
                    raise RuntimeError('Early replay before Resume')
                sources = None
                if self.recording:
                    sources = {}
                    for lane in LANES:
                        infos = self.node.get_publishers_info_by_topic(self.lane_topic(lane))
                        if len(infos) != 1:
                            raise RuntimeError('Expected one actual source publisher')
                        sources[lane] = self.endpoint(infos[0])
                        if (sources[lane]['owner'] != '/' + self.node_name or sources[lane]['kind'] != 'publisher' or
                                sources[lane]['type'] != self.type_names[lane] or sources[lane]['qos'] != snapshot['data'][lane][0]['qos']):
                            raise RuntimeError('Actual source publisher identity/QoS differs')
                self.ready_graph = snapshot
                self.emit('READY', graph=snapshot, requested_qos=dict(history=1, depth=10, reliability=1, durability=2),
                          counts=matches if self.recording else self.counts, slots_empty=True, source_endpoints=sources)
                return
        raise RuntimeError('Finish interrupted readiness')

    def recheck_ready(self):
        if self.graph() != self.ready_graph:
            raise RuntimeError('Exact ready graph changed before publication/Resume')
        for lane in LANES:
            counts = [self.publishers[lane].get_subscription_count()] if self.recording else [s.get_publisher_count() for s in self.subscriptions[lane]]
            if counts != ([1] if self.recording else [1, 1]):
                raise RuntimeError('Endpoint match changed before publication/Resume')

    def capture(self, lane, data, origin, index=None):
        if type(data) is not bytes or not 4 <= len(data) <= 65536:
            raise RuntimeError('Whole buffer size/type')
        self.emit('BUFFER', lane=lane, index=index, origin=origin, cdr_bytes=len(data),
                  cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_base64=base64.b64encode(data).decode('ascii'))
        return self.sequence

    def callback(self, lane, raw):
        def receive(message, info):
            from rclpy.serialization import deserialize_message
            self.check_time()
            if not self.resume_issued:
                raise RuntimeError('Early callback before Resume')
            ref = None
            if raw:
                ref = self.capture(lane, message, 'RAW_SUBSCRIPTION')
                message = deserialize_message(message, self.classes[lane.rsplit('_', 1)[1]])
            value = plain(message)
            index = self.identify(lane, value)
            if raw:
                self.emit('NATIVE_DECODE', buffer_seq=ref, lane=lane, index=index, value=value)
            key = (lane, index, 'raw' if raw else 'typed')
            if key in self.seen:
                raise RuntimeError('Duplicate callback including late observation')
            if type(info) is not dict or set(info) != set(META) or any(
                    v is not None and (type(v) is not int or v < 0) for v in info.values()):
                raise RuntimeError('MessageInfo representation')
            fields = dict(lane=lane, index=index, value=value, metadata=info)
            if raw:
                fields['buffer_seq'] = ref
            self.emit('RAW_RECEIVE' if raw else 'TYPED_RECEIVE', **fields)
            self.seen.add(key)
            self.counts[lane][int(raw)] += 1
            self.last_receive = time.monotonic()
            self.check_time()
        return receive

    def resume_requested(self):
        if self.resume_issued or (not self.resume_path.exists() and not self.resume_path.is_symlink()):
            return False
        before = self.resume_path.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600:
            raise RuntimeError('Resume control file kind/mode')
        raw = self.resume_path.read_bytes()
        after = self.resume_path.lstat()
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('Resume control changed during read')
        expected = dict(schema=1, scope='R35_RESUME', case=self.args.case, run=self.args.run,
                        record_run=self.args.record_run, pid=os.getpid(), bag_seal_sha256=self.args.bag_seal_sha256)
        if json.loads(raw) != expected or self.seen:
            raise RuntimeError('Resume identity or initial observation slots differ')
        self.resume_issued = True
        self.emit('RESUME_ACCEPTED', bag_seal_sha256=self.args.bag_seal_sha256)
        return True

    def communicate(self):
        import rclpy
        from rclpy.duration import Duration
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        from rclpy.serialization import serialize_message, deserialize_message
        primary, cleanup = False, []
        try:
            self.types_and_definitions()
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name, start_parameter_services=False)
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
            self.type_names = {lane: 'std_msgs/msg/String' if lane.endswith('string') else 'rcl_interfaces/msg/ParameterEvent' for lane in LANES}
            if self.recording:
                self.publishers = {l: self.node.create_publisher(self.classes[l.rsplit('_', 1)[1]], self.lane_topic(l), qos) for l in LANES}
            else:
                from rosbag2_interfaces.srv import Resume
                self.subscriptions = {l: [self.node.create_subscription(self.classes[l.rsplit('_', 1)[1]], self.lane_topic(l),
                    self.callback(l, raw), qos, raw=raw) for raw in (False, True)] for l in LANES}
                self.service = '/' + self.remote_name + '/resume'
                self.client = self.node.create_client(Resume, self.service)
            self.wait_ready()
            if self.recording:
                for index in (1, 2):
                    for lane in LANES:
                        self.check_time()
                        message = self.message(lane, index)
                        data = serialize_message(message)
                        self.retained.append((message, data))
                        ref = self.capture(lane, data, 'LOCAL_SERIALIZE_MESSAGE', index)
                        value = plain(deserialize_message(data, type(message)))
                        if value != plain(message):
                            raise RuntimeError('Full-buffer local native roundtrip differs')
                        self.emit('NATIVE_DECODE', buffer_seq=ref, lane=lane, index=index, value=value)
                        self.recheck_ready()
                        self.publishers[lane].publish(data if lane.startswith('serialized_') else message)
                        self.emit('PUBLISH_RETURN', lane=lane, index=index, buffer_seq=ref,
                                  input_kind='bytes' if lane.startswith('serialized_') else 'message')
                        self.calls += 1
                        due = time.monotonic() + 0.2
                        while time.monotonic() < due:
                            if not self.tick():
                                raise RuntimeError('Finish before all publications')
                ack_deadline = min(self.deadline, time.monotonic() + 5)
                for lane in LANES:
                    remaining = int((ack_deadline - time.monotonic()) * 1e9)
                    if remaining <= 0 or not self.publishers[lane].wait_for_all_acked(Duration(nanoseconds=remaining)):
                        raise RuntimeError('Bounded DDS ACK observation failed')
                    self.emit('ACKED', lane=lane, returned=True, timeout_ns=remaining)
                due = time.monotonic() + 0.25
                while time.monotonic() < due:
                    if not self.tick():
                        raise RuntimeError('Finish before fixed post-ack settlement')
                self.done = True
                self.emit('RECORD_COMPLETE', published=8, acknowledged=4, settle_seconds=0.25)
            future = None
            while self.tick():
                if not self.recording and self.resume_requested():
                    self.recheck_ready()
                    self.emit('RESUME_CALLED', service=self.service)
                    future = self.client.call_async(Resume.Request())
                    response_deadline = min(self.deadline, time.monotonic() + 5)
                if future is not None and not self.resume_returned:
                    if time.monotonic() >= response_deadline:
                        raise RuntimeError('One Resume response deadline exceeded')
                    if future.done():
                        if future.cancelled() or future.exception() is not None or type(future.result()) is not Resume.Response:
                            raise RuntimeError('Resume future failed/wrong response')
                        fields = plain(future.result())
                        if fields != {}:
                            raise RuntimeError('Selected Resume response must be empty')
                        self.resume_returned = True
                        self.emit('RESUME_RETURNED', response_type='rosbag2_interfaces/srv/Resume_Response', fields=fields,
                                  future_done=True, future_cancelled=False, future_raised=False)
                if not self.recording and len(self.seen) == 16 and self.resume_returned and not self.done and time.monotonic() - self.last_receive >= 0.25:
                    self.done = True
                    self.emit('PLAY_COMPLETE', callbacks=16, counters=self.counts, quiet_seconds=0.25)
            self.emit('OBSERVATION_END', callbacks=len(self.seen), published=self.calls,
                      last_spin_ns=self.last_spin_ns, cutoff_ns=time.monotonic_ns())
        except BaseException as error:
            primary = True
            self.emit('FAIL', reason='R35 probe failed', error_type=type(error).__name__)
            raise
        finally:
            if self.node is not None:
                try:
                    self.node.destroy_node()
                    self.emit('NODE_DESTROY_RETURN', returned=True)
                except BaseException as error:
                    cleanup.append(type(error).__name__)
            try:
                rclpy.try_shutdown()
                self.emit('SHUTDOWN_RETURN', returned=True)
            except BaseException as error:
                cleanup.append(type(error).__name__)
            if cleanup and not primary:
                raise RuntimeError('R35 fixture cleanup failed: ' + ','.join(cleanup))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('case', 'run', 'record-run', 'side', 'role', 'finish-control', 'contract', 'contract-sha256'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--resume-control')
    parser.add_argument('--bag-seal-sha256')
    probe = SerializedBagProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException:
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
