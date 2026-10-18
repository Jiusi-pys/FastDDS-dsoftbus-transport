"""Host stubs for finite probe control; never native ROS or DSoftBus evidence."""
import base64
from enum import IntEnum
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile
from types import ModuleType, SimpleNamespace as NS
import unittest
from unittest.mock import patch

PROBE = Path(__file__).resolve().parents[2] / 'integrations/ros2/target/bag_recordplay_probe.py'
spec = importlib.util.spec_from_file_location('candidate_bag_recordplay_probe', PROBE)
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)


class Policy(IntEnum):
    UNKNOWN = 0
    RELIABLE = 1
    VOLATILE = 2
    AUTOMATIC = 3


class Duration:
    def __init__(self, nanoseconds=0):
        self.nanoseconds = nanoseconds


class QoS:
    def __init__(self, depth=10, reliability=Policy.RELIABLE, durability=Policy.VOLATILE):
        self.depth, self.reliability, self.durability = depth, reliability, durability
        self.history, self.liveliness = Policy.UNKNOWN, Policy.AUTOMATIC
        self.deadline = self.lifespan = self.liveliness_lease_duration = Duration(2000000013)
        self.avoid_ros_namespace_conventions = False


class String:
    def __init__(self, data=''):
        self.data = data


class Resume:
    class Request:
        pass

    class Response:
        pass


class TypeHash:
    def __init__(self, version=1, value=bytes.fromhex('11' * 32)):
        self.version, self.value = version, value

    def __str__(self):
        return 'RIHS%02d_%s' % (self.version, self.value.hex())


def serialized(message):
    raw = message.data.encode() + b'\0'
    return b'\0\1\0\0' + struct.pack('<I', len(raw)) + raw


class BagRecordplayProbeControls(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='bag-probe-stub-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.raw = b'# selected test String\nstring data\n'
        path = self.root / 'share/std_msgs/msg/String.msg'
        path.parent.mkdir(parents=True)
        path.write_bytes(self.raw)
        self.clock, self.events, self.published, self.nodes, self.closed = 0.0, [], [], [], []
        self.match, self.graph, self.ack, self.early_finish = 1, True, True, False
        self.drift, self.destroy_ok, self.extra_data = False, True, False
        self.resume_calls, self.receive_count = 0, 0
        self.future_done, self.future_response = True, Resume.Response()
        self.p = object.__new__(M.BagRecordplayProbe)
        self.p.args = NS(run='0123456789abcdef', record_run='0123456789abcdef', side='A', role='bag-publisher')
        self.p.record_run = self.p.args.record_run
        self.p.node_name = 'bagpub_' + self.p.args.run
        self.p.topic = '/bag_' + self.p.args.run
        self.p.package, self.p.manifest = self.root, {'share/std_msgs/msg/String.msg': hashlib.sha256(self.raw).hexdigest()}
        self.p.publications = self.p.received = 0
        self.p.acked = self.p.resume_issued = self.p.resume_returned = self.p.replay_ready = False
        self.p.failed = self.p.endpoint = None
        self.p.emit = lambda event, **data: self.events.append({'event': event, **data})
        self.p.finish_requested = lambda: self.early_finish or (self.p.acked if self.p.args.role == 'bag-publisher' else self.p.received == 10 and self.p.resume_returned)
        self.endpoint = NS(node_name=self.p.node_name, node_namespace='/', topic_type='std_msgs/msg/String',
                           endpoint_type=1, endpoint_gid=list(range(16)),
                           topic_type_hash=TypeHash(), qos_profile=QoS(depth=0))
        self.publisher = NS(get_subscription_count=lambda: self.match, publish=self.published.append,
                            wait_for_all_acked=self.wait_ack)
        self.client = NS(service_is_ready=lambda: True, call_async=self.call_resume)
        self.node = NS(create_publisher=lambda *args: self.publisher,
                       get_publishers_info_by_topic=self.get_info,
                       create_subscription=self.subscribe, create_client=lambda *args: self.client,
                       count_publishers=lambda topic: self.match,
                       destroy_publisher=lambda entity: self.destroy('publisher'),
                       destroy_subscription=lambda entity: self.destroy('subscription'),
                       destroy_client=lambda entity: self.destroy('client'),
                       destroy_node=lambda: self.closed.append('node'))
        modules = {}
        def module(name, **values):
            item = ModuleType(name); item.__dict__.update(values); modules[name] = item
        module('rclpy', init=lambda **kw: None, create_node=self.create_node,
               spin_once=self.spin, ok=lambda: True, try_shutdown=lambda: self.closed.append('context'))
        module('rclpy.duration', Duration=Duration)
        module('rclpy.qos', QoSProfile=QoS, ReliabilityPolicy=Policy, DurabilityPolicy=Policy)
        module('rclpy.serialization', serialize_message=serialized)
        module('std_msgs'); module('std_msgs.msg', String=String)
        module('rosbag2_interfaces'); module('rosbag2_interfaces.srv', Resume=Resume)
        definition = NS(topic_type='std_msgs/msg/String', encoding='ros2msg',
                        encoded_message_definition=self.raw.decode(), type_hash='')
        module('rosbag2_py', LocalMessageDefinitionSource=lambda: NS(get_full_text=lambda name: definition))
        self.addCleanup(patch.stopall)
        patch.dict(sys.modules, modules).start()
        patch.object(M.time, 'monotonic', lambda: self.clock).start()

    def create_node(self, name):
        self.nodes.append(name)
        return self.node

    def get_info(self, topic):
        if self.drift and len(self.published) > 0:
            self.endpoint.endpoint_gid = [9] * 16
        return [self.endpoint] if self.graph else []

    def wait_ack(self, timeout):
        self.assertGreater(timeout.nanoseconds, 0)
        self.assertLessEqual(timeout.nanoseconds, 5000000000)
        self.assertEqual(len(self.published), 10)
        return self.ack

    def destroy(self, kind):
        self.closed.append(kind)
        return self.destroy_ok

    def subscribe(self, cls, topic, callback, qos):
        self.callback = callback
        return object()

    def call_resume(self, request):
        self.resume_calls += 1
        return NS(done=lambda: self.future_done, result=lambda: self.future_response)

    def spin(self, node, timeout_sec):
        self.clock += timeout_sec
        if self.resume_calls and self.receive_count < (11 if self.extra_data else 10):
            self.callback(String(data=self.p.bag_payload(self.receive_count)))
            self.receive_count += 1

    def subscriber(self):
        self.p.args.role = 'bag-subscriber'
        self.p.topic = '/bagplay_' + self.p.args.run
        def resume():
            if self.p.replay_ready and not self.p.resume_issued:
                self.p.resume_issued = True
                return True
            return False
        self.p.resume_requested = resume

    def test_publisher_ten_actual_messages_and_cdr_then_ack_one_node(self):
        self.p.communicate_bag()
        self.assertEqual(len(self.nodes), 1)
        self.assertEqual([m.data for m in self.published], [self.p.bag_payload(i) for i in range(10)])
        rows = [e for e in self.events if e['event'] == 'BAG_PUBLISH']
        self.assertEqual([e['sequence'] for e in rows], list(range(10)))
        for row, message in zip(rows, self.published):
            self.assertEqual(base64.b64decode(row['cdr_base64']), serialized(message))
        graph = next(e for e in self.events if e['event'] == 'BAG_PUBLISHER_INFO')
        self.assertEqual(graph['offered_qos_profiles'][0]['depth'], 0)
        self.assertEqual(graph['offered_qos_profiles'][0]['history'], 'unknown')
        self.assertEqual(self.closed, ['publisher', 'node', 'context'])
        self.assertGreaterEqual(self.clock, 1.8)
        self.assertLess(self.clock, 5)

    def test_missing_match_expires_without_publish(self):
        self.match = 0
        with self.assertRaisesRegex(RuntimeError, 'Fixed40s'):
            self.p.communicate_bag()
        self.assertEqual(self.published, [])
        self.assertEqual(self.closed, ['publisher', 'node', 'context'])

    def test_missing_graph_never_uses_requested_qos(self):
        self.graph = False
        with self.assertRaisesRegex(RuntimeError, 'Fixed40s'):
            self.p.communicate_bag()
        self.assertEqual(self.published, [])

    def test_extra_subscription_rejected(self):
        self.match = 2
        with self.assertRaisesRegex(RuntimeError, 'extra recorder'):
            self.p.communicate_bag()

    def test_wrong_graph_identity_rejected(self):
        self.endpoint.node_name = 'other'
        with self.assertRaisesRegex(RuntimeError, 'identity differs'):
            self.p.communicate_bag()

    def test_graph_drift_stops_further_publication(self):
        self.drift = True
        with self.assertRaisesRegex(RuntimeError, 'graph identity/QoS/hash changed'):
            self.p.communicate_bag()
        self.assertEqual(len(self.published), 1)

    def test_ack_false_is_failure(self):
        self.ack = False
        with self.assertRaisesRegex(RuntimeError, 'acknowledgement failed'):
            self.p.communicate_bag()
        self.assertFalse(self.p.acked)

    def test_early_finish_is_failure(self):
        self.early_finish = True
        with self.assertRaisesRegex(RuntimeError, 'before complete'):
            self.p.communicate_bag()

    def test_cleanup_false_cannot_pass(self):
        self.destroy_ok = False
        with self.assertRaisesRegex(RuntimeError, 'destruction returned false'):
            self.p.communicate_bag()
        self.assertEqual(self.closed, ['publisher', 'node', 'context'])

    def test_definition_source_mismatch_is_failure(self):
        (self.root / 'share/std_msgs/msg/String.msg').write_bytes(b'wrong')
        with self.assertRaisesRegex(RuntimeError, 'definition differs'):
            self.p.communicate_bag()
        self.assertEqual(self.published, [])

    def test_subscriber_one_node_one_resume_ten_ordered_payloads(self):
        self.subscriber()
        self.p.communicate_bag()
        self.assertEqual((len(self.nodes), self.resume_calls, self.p.received), (1, 1, 10))
        self.assertEqual(self.closed, ['subscription', 'client', 'node', 'context'])

    def test_resume_response_timeout_is_bounded(self):
        self.subscriber(); self.future_done = False
        with self.assertRaisesRegex(RuntimeError, 'Resume response deadline'):
            self.p.communicate_bag()
        self.assertLess(self.clock, 5.2)
        self.assertEqual(self.resume_calls, 1)

    def test_resume_invalid_response_is_failure(self):
        self.subscriber(); self.future_response = None
        with self.assertRaisesRegex(RuntimeError, 'no valid response'):
            self.p.communicate_bag()

    def test_receive_before_resume_and_out_of_order(self):
        self.p.receive_bag(String(self.p.bag_payload(0)))
        self.assertIsNotNone(self.p.failed)
        self.p.failed = None; self.p.resume_issued = True
        self.p.receive_bag(String(self.p.bag_payload(1)))
        self.assertIsNotNone(self.p.failed)
        self.assertEqual(self.p.received, 0)

    def test_resume_control_exact_identity_single_use_and_mode(self):
        self.p.replay_ready = True
        content = ('R25_BAG_RESUME schema=1 run=' + self.p.args.run + ' side=A pid=' +
                   str(os.getpid()) + ' record_run=' + self.p.record_run + '\n').encode()
        info = NS(st_mode=stat.S_IFREG | 0o600, st_dev=1, st_ino=2, st_size=len(content), st_mtime_ns=3)
        self.p.resume = NS(exists=lambda: True, is_symlink=lambda: False, lstat=lambda: info,
                           read_bytes=lambda: content)
        self.assertTrue(self.p.resume_requested())
        self.assertFalse(self.p.resume_requested())
        self.p.resume_issued = False; info.st_mode = stat.S_IFLNK | 0o600
        with self.assertRaisesRegex(RuntimeError, 'regular0600'):
            self.p.resume_requested()
        info.st_mode = stat.S_IFREG | 0o600
        self.p.resume.read_bytes = lambda: content.replace(b'side=A', b'side=B')
        with self.assertRaisesRegex(RuntimeError, 'identity or readiness'):
            self.p.resume_requested()

    def test_real_parent_and_child_construction_preserves_payload_method(self):
        for role in ('bag-publisher', 'bag-subscriber'):
            with self.subTest(role=role):
                run = '0123456789abcdef'
                root = '/data/local/tmp/mdds_dds_' + run + '_A'
                args = NS(run=run, side='A', role=role, finish_control=str(Path(root) / 'control.PROBE_FINISH'),
                          record_run=run if role == 'bag-publisher' else 'fedcba9876543210',
                          resume_control=None if role == 'bag-publisher' else str(Path(root) / 'control.BAG_RESUME'))
                # Execute both real constructors; only the target manifest read is supplied.
                with patch.object(Path, 'read_text', return_value=json.dumps({'files': {}})):
                    probe = M.BagRecordplayProbe(args)
                self.assertEqual(probe.payload, 'payload_' + run)
                self.assertEqual(probe.bag_payload(0), 'r25bag_' + args.record_run + '_000')
                self.assertEqual(probe.sequence, 0)

    def test_real_constructor_rejects_reused_playback_run(self):
        run = '0123456789abcdef'; root = '/data/local/tmp/mdds_dds_' + run + '_A'
        args = NS(run=run, side='A', role='bag-subscriber', record_run=run,
                  finish_control=str(Path(root) / 'control.PROBE_FINISH'), resume_control=str(Path(root) / 'control.BAG_RESUME'))
        with patch.object(Path, 'read_text', return_value=json.dumps({'files': {}})), self.assertRaisesRegex(ValueError, 'fresh run'):
            M.BagRecordplayProbe(args)

    def test_observed_zero_hash_uses_stock_recorder_empty_encoding(self):
        self.p.node = self.node
        self.endpoint.topic_type_hash = TypeHash(0, bytes(32))
        self.assertEqual(self.p.publisher_endpoint()['type_description_hash'], '')

    def test_unsupported_hash_is_not_silently_empty(self):
        self.p.node = self.node
        for invalid in (TypeHash(-1), TypeHash(2), TypeHash(0, b'1' * 32)):
            with self.subTest(version=invalid.version):
                self.endpoint.topic_type_hash = invalid
                with self.assertRaisesRegex(RuntimeError, 'unsupported String type hash'):
                    self.p.publisher_endpoint()


if __name__ == '__main__':
    unittest.main()
