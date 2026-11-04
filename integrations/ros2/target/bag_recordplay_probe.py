"""One public rclpy Node for bounded recording or resumed playback acceptance.

The B process remains the stock ros2 bag CLI. This A-side fixture never controls
another PID, creates a second Node, or grants device/transport qualification.
"""
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

ORDINARY = Path(__file__).with_name('actionbag_runtime_probe.py')
ORDINARY_SHA = '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df'
if hashlib.sha256(ORDINARY.read_bytes()).hexdigest() != ORDINARY_SHA:
    raise ValueError('Selected public probe/finish implementation changed')
OrdinaryRuntimeProbe = runpy.run_path(str(ORDINARY))['OrdinaryRuntimeProbe']


def bag_graph_qos(profile):
    """Encode the actual public graph observation, not a requested QoS override."""
    result = {name: getattr(profile, name).name.lower() for name in
              ('history', 'reliability', 'durability', 'liveliness')}
    result['depth'] = profile.depth
    for name in ('deadline', 'lifespan', 'liveliness_lease_duration'):
        seconds, nanoseconds = divmod(getattr(profile, name).nanoseconds, 1000000000)
        result[name] = {'sec': seconds, 'nsec': nanoseconds}
    result['avoid_ros_namespace_conventions'] = profile.avoid_ros_namespace_conventions
    return result


class BagRecordplayProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        if (args.side != 'A' or args.role not in ('bag-publisher', 'bag-subscriber') or
                not re.fullmatch('[0-9a-f]{16}', args.record_run)):
            raise ValueError('Exact A-side bag role and original record run required')
        self.record_run = args.record_run
        self.node_name = 'bagpub_' + args.run if args.role == 'bag-publisher' else 'bagsub_' + args.run
        self.topic = '/bag_' + args.run if args.role == 'bag-publisher' else '/bagplay_' + args.run
        self.resume = self.root / 'control.BAG_RESUME'
        if args.role == 'bag-publisher':
            if args.record_run != args.run or args.resume_control is not None:
                raise ValueError('Publisher must use its own run and no resume control')
        elif args.record_run == args.run or args.resume_control != str(self.resume):
            raise ValueError('Playback needs a fresh run and exact owned Resume control')
        self.received, self.resume_issued, self.resume_returned = 0, False, False
        self.acked, self.endpoint, self.replay_ready = False, None, False

    def bag_payload(self, sequence):
        return 'r25bag_' + self.record_run + '_' + str(sequence).zfill(3)

    def cdr_fields(self, message):
        from rclpy.serialization import serialize_message
        data = serialize_message(message)
        return {'text': message.data, 'cdr_base64': base64.b64encode(data).decode('ascii'),
                'cdr_sha256': hashlib.sha256(data).hexdigest(), 'cdr_bytes': len(data)}

    def publisher_endpoint(self):
        rows = self.node.get_publishers_info_by_topic(self.topic)
        if not rows:
            return None
        if len(rows) != 1:
            raise RuntimeError('Expected exactly one actual graph publisher')
        info = rows[0]
        gid = bytes(info.endpoint_gid)
        if (info.node_name != self.node_name or info.node_namespace != '/' or
                info.topic_type != 'std_msgs/msg/String' or int(info.endpoint_type) != 1 or
                len(gid) != 16 or not any(gid)):
            raise RuntimeError('Public graph publisher identity differs')
        observed_hash = info.topic_type_hash
        if observed_hash.version == 0 and observed_hash.value == bytes(32):
            # Stock Recorder's type_hash_to_string maps an observed zero hash to ''.
            type_hash = ''
        else:
            type_hash = str(observed_hash)
            if observed_hash.version != 1 or not re.fullmatch('RIHS01_[0-9a-f]{64}', type_hash):
                raise RuntimeError('Public graph returned an unsupported String type hash')
        return {'node_name': info.node_name, 'node_namespace': info.node_namespace,
                'topic_type': info.topic_type, 'gid': gid.hex(),
                'type_description_hash': type_hash,
                'offered_qos_profiles': [bag_graph_qos(info.qos_profile)]}

    def resume_requested(self):
        if self.resume_issued:
            return False
        if not self.resume.exists() and not self.resume.is_symlink():
            return False
        before = self.resume.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600:
            raise RuntimeError('Resume control is not regular0600')
        identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
        data = self.resume.read_bytes()
        after = self.resume.lstat()
        if identity != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('Resume control changed during read')
        expected = ('R25_BAG_RESUME schema=1 run=' + self.args.run + ' side=A pid=' +
                    str(os.getpid()) + ' record_run=' + self.record_run + '\n').encode('ascii')
        if data != expected or not self.replay_ready:
            raise RuntimeError('Resume control identity or readiness differs')
        self.resume_issued = True
        self.emit('BAG_RESUME_ACCEPTED', record_run=self.record_run)
        return True

    def receive_bag(self, message):
        if (not self.resume_issued or self.received >= 10 or
                message.data != self.bag_payload(self.received)):
            self.failed = 'Unexpected, duplicate, early or out-of-order replay payload'
            self.emit('FAIL', reason=self.failed)
            return
        self.emit('BAG_RECEIVE', sequence=self.received, **self.cdr_fields(message))
        self.received += 1

    def communicate_bag(self):
        import rclpy
        from rclpy.duration import Duration
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        from std_msgs.msg import String
        self.node = None
        publisher = subscription = client = None
        try:
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name)
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE,
                             durability=DurabilityPolicy.VOLATILE)
            if self.args.role == 'bag-publisher':
                from rosbag2_py import LocalMessageDefinitionSource
                definition = LocalMessageDefinitionSource().get_full_text('std_msgs/msg/String')
                if definition.topic_type != 'std_msgs/msg/String' or definition.encoding != 'ros2msg':
                    raise RuntimeError('Selected native String definition differs')
                path = self.package / 'share/std_msgs/msg/String.msg'
                raw = path.read_bytes()
                if (hashlib.sha256(raw).hexdigest() != self.manifest['share/std_msgs/msg/String.msg'] or
                        definition.encoded_message_definition != raw.decode('utf-8')):
                    raise RuntimeError('Native definition differs from selected package source')
                self.emit('BAG_DEFINITION', encoding=definition.encoding,
                          encoded_message_definition=definition.encoded_message_definition,
                          type_hash=definition.type_hash, source_sha256=hashlib.sha256(raw).hexdigest())
                publisher = self.node.create_publisher(String, self.topic, qos)
            else:
                from rosbag2_interfaces.srv import Resume
                subscription = self.node.create_subscription(String, self.topic, self.receive_bag, qos)
                service = '/bagplayer_' + self.args.run + '/resume'
                client = self.node.create_client(Resume, service)
            self.emit('ENTITY_READY', entity=self.args.role, name=self.topic)
            due, future, resume_deadline = time.monotonic(), None, None
            while time.monotonic() < self.deadline:
                if self.failed:
                    raise RuntimeError(self.failed)
                if self.finish_requested():
                    if not (self.acked if publisher is not None else self.received == 10 and self.resume_returned):
                        raise RuntimeError('Normal finish before complete bag business')
                    return
                if not rclpy.ok():
                    raise RuntimeError('ROS context stopped before normal finish')
                rclpy.spin_once(self.node, timeout_sec=0.05)
                now = time.monotonic()
                if publisher is not None and self.publications < 10:
                    count = publisher.get_subscription_count()
                    if count > 1:
                        raise RuntimeError('Unexpected extra recorder subscription')
                    endpoint = self.publisher_endpoint()
                    if count == 1 and endpoint is not None and now >= due:
                        if self.endpoint is None:
                            self.endpoint = endpoint
                            self.emit('BAG_PUBLISHER_INFO', **endpoint)
                        elif endpoint != self.endpoint:
                            raise RuntimeError('Publisher graph identity/QoS/hash changed')
                        message = String(data=self.bag_payload(self.publications))
                        fields = self.cdr_fields(message)
                        publisher.publish(message)
                        self.emit('BAG_PUBLISH', sequence=self.publications, matched=count, **fields)
                        self.publications += 1
                        due = time.monotonic() + 0.2
                if publisher is not None and self.publications == 10 and not self.acked:
                    remaining = min(5, self.deadline - time.monotonic())
                    if remaining <= 0 or not publisher.wait_for_all_acked(Duration(nanoseconds=int(remaining * 1e9))):
                        raise RuntimeError('Bounded publisher acknowledgement failed')
                    self.acked = True
                    self.emit('BAG_ACKED', published=10, timeout_seconds=remaining)
                if client is not None:
                    count = self.node.count_publishers(self.topic)
                    if count > 1:
                        raise RuntimeError('Unexpected extra playback publisher')
                    if count == 1 and client.service_is_ready() and not self.replay_ready:
                        self.replay_ready = True
                        self.emit('BAG_REPLAY_READY', publisher_count=count, service=service)
                    if self.resume_requested():
                        resume_deadline = min(self.deadline, time.monotonic() + 5)
                        future = client.call_async(Resume.Request())
                        self.emit('BAG_RESUME_CALLED', service=service)
                    if future is not None and not self.resume_returned:
                        if time.monotonic() >= resume_deadline:
                            raise RuntimeError('Resume response deadline exceeded')
                        if future.done():
                            response = future.result()
                            if not isinstance(response, Resume.Response):
                                raise RuntimeError('Resume returned no valid response')
                            self.resume_returned = True
                            self.emit('BAG_RESUME_RETURNED', success=True)
            raise RuntimeError('Fixed40s bag fixture deadline exceeded')
        finally:
            primary = sys.exc_info()[1]
            errors = []
            for entity, destroy in ((subscription, 'destroy_subscription'), (publisher, 'destroy_publisher'),
                                    (client, 'destroy_client')):
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
                self.emit('FAIL', reason='Bag fixture cleanup failed', cleanup_errors=errors,
                          primary_failure=repr(primary))
                if primary is None:
                    raise RuntimeError('; '.join(errors))


def bag_recordplay_probe_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', choices=('bag-publisher', 'bag-subscriber'), required=True)
    parser.add_argument('--run', required=True)
    parser.add_argument('--side', choices=('A',), required=True)
    parser.add_argument('--record-run', required=True)
    parser.add_argument('--finish-control', required=True)
    parser.add_argument('--resume-control')
    probe = BagRecordplayProbe(parser.parse_args(argv))
    try:
        probe.communicate_bag()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(bag_recordplay_probe_main())
