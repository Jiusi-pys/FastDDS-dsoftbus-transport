"""Public native rosbag ABI gate; creates no ROS context, Node or Recorder."""
import argparse
import base64
import contextlib
import hashlib
import importlib
import importlib.metadata
import io
import json
from pathlib import Path
import runpy
import sysconfig
import time

ORDINARY_SHA = '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df'
ORDINARY = Path(__file__).with_name('actionbag_runtime_probe.py')
if hashlib.sha256(ORDINARY.read_bytes()).hexdigest() != ORDINARY_SHA:
    raise ValueError('Selected ordinary finish-control helper changed')
OrdinaryRuntimeProbe = runpy.run_path(str(ORDINARY))['OrdinaryRuntimeProbe']
MODULES = ('_compression_options', '_message_definitions', '_reader', '_storage',
           '_writer', '_info', '_transport', '_reindexer')


def bag_qos_fields(profile):
    result = {key: int(getattr(profile, key)) for key in
              ('history', 'depth', 'reliability', 'durability', 'liveliness')}
    for key in ('deadline', 'lifespan', 'liveliness_lease_duration'):
        seconds, nanoseconds = divmod(getattr(profile, key).nanoseconds, 1000000000)
        result[key] = {'sec': seconds, 'nsec': nanoseconds}
    result['avoid_ros_namespace_conventions'] = profile.avoid_ros_namespace_conventions
    return result


class BagRuntimeProbe(OrdinaryRuntimeProbe):
    def bag_abi_probe(self):
        import rclpy
        import rosbag2_py as bag
        import yaml
        from rclpy.duration import Duration
        from rclpy.qos import QoSProfile, HistoryPolicy, ReliabilityPolicy, DurabilityPolicy, LivelinessPolicy
        from rclpy.serialization import serialize_message, deserialize_message
        from rosbag2_interfaces.srv import Resume
        from std_msgs.msg import String
        if rclpy.ok() or sysconfig.get_config_var('SOABI') != 'cpython-312-aarch64-linux-ohos':
            raise RuntimeError('Require inactive default context and selected target ABI')
        origins = {}
        for name in ['rosbag2_py.' + module for module in MODULES] + [
                'rclpy._rclpy_pybind11', 'ros2bag.verb.record', 'ros2bag.verb.play', 'ros2bag_sqlite3_cli']:
            module = importlib.import_module(name)
            path = Path(module.__file__).resolve()
            member = path.relative_to(self.package).as_posix()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if not member.startswith('python/') or self.manifest.get(member) != digest:
                raise RuntimeError('Unselected native/module origin: ' + name)
            origins[name] = {'path': str(path), 'sha256': digest}
        capsules, cdr = {}, []
        for label, cls, message in [('String', String, String(data='r25bag_' + self.args.run)),
                                   ('Resume.Request', Resume.Request, Resume.Request()),
                                   ('Resume.Response', Resume.Response, Resume.Response())]:
            meta = type(cls)
            meta.__import_type_support__()
            fields = ['_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
            if any(type(getattr(meta, field, None)).__name__ != 'PyCapsule' for field in fields):
                raise RuntimeError('Missing actual typesupport capsule: ' + label)
            capsules[label] = fields
            data = serialize_message(message)
            if deserialize_message(data, cls) != message:
                raise RuntimeError('Native message CDR roundtrip failed: ' + label)
            cdr.append({'type': label, 'bytes': len(data), 'base64': base64.b64encode(data).decode(),
                        'sha256': hashlib.sha256(data).hexdigest()})
        type(Resume).__import_type_support__()
        if type(type(Resume)._TYPE_SUPPORT).__name__ != 'PyCapsule':
            raise RuntimeError('Missing actual Resume service capsule')
        capsules['Resume'] = ['_TYPE_SUPPORT']
        profiles = [QoSProfile(history=HistoryPolicy.KEEP_LAST, depth=depth,
            reliability=reliability, durability=durability, liveliness=liveliness,
            deadline=Duration(seconds=2, nanoseconds=13), lifespan=Duration(seconds=3, nanoseconds=17),
            liveliness_lease_duration=Duration(seconds=4, nanoseconds=19), avoid_ros_namespace_conventions=avoid)
            for depth, reliability, durability, liveliness, avoid in [
                (7, ReliabilityPolicy.RELIABLE, DurabilityPolicy.VOLATILE, LivelinessPolicy.AUTOMATIC, False),
                (11, ReliabilityPolicy.BEST_EFFORT, DurabilityPolicy.TRANSIENT_LOCAL, LivelinessPolicy.MANUAL_BY_TOPIC, True)]]
        overrides = {'/bag_abi_' + str(index): profile for index, profile in enumerate(profiles)}
        record, play = bag.RecordOptions(), bag.PlayOptions()
        record.topic_qos_profile_overrides = overrides
        play.topic_qos_profile_overrides = overrides
        # Options getters are Python caches, not evidence of native readback.
        expected = [bag_qos_fields(profile) for profile in profiles]
        serialized = [{**row, **{key: getattr(profile, key).name.lower() for key in
            ('history', 'reliability', 'durability', 'liveliness')}} for row, profile in zip(expected, profiles)]
        native = bag.to_rclcpp_qos_vector(yaml.safe_dump(serialized), 9)
        actual = [bag_qos_fields(bag.convert_rclcpp_qos_to_rclpy_qos(profile)) for profile in native]
        if actual != expected:
            raise RuntimeError('Public native QoS vector roundtrip differs')
        distributions = {name: importlib.metadata.distribution(name) for name in ('ros2bag', 'ros2bag_sqlite3_cli')}
        if any(dist.version != '0.26.11' for dist in distributions.values()):
            raise RuntimeError('Unexpected rosbag distribution version')
        entries = list(importlib.metadata.entry_points(group='ros2bag.storage_plugin_cli_extension', name='sqlite3'))
        if len(entries) != 1 or entries[0].value != 'ros2bag_sqlite3_cli':
            raise RuntimeError('Require the genuine unambiguous SQLite plugin entrypoint')
        presets = entries[0].load().get_preset_profiles()
        if [row[0] for row in presets] != ['none', 'resilient']:
            raise RuntimeError('Unexpected genuine SQLite preset profiles')
        from ros2bag.verb.record import RecordVerb
        from ros2bag.verb.play import PlayVerb
        recorder_parser = argparse.ArgumentParser(prog='ros2 bag record')
        RecordVerb().add_arguments(recorder_parser, 'ros2 bag record')
        # No file/output/storage write and no main call: genuine argument registration only.
        default = recorder_parser.parse_args(['--storage', 'sqlite3', '--topics', '/bag_abi'])
        explicit = recorder_parser.parse_args(['--storage', 'sqlite3', '--storage-preset-profile', 'none', '--topics', '/bag_abi'])
        if default.storage_preset_profile != 'none' or explicit.storage_preset_profile != 'none':
            raise RuntimeError('SQLite default/explicit parser selection differs')
        rejected = io.StringIO()
        with contextlib.redirect_stderr(rejected):
            try:
                recorder_parser.parse_args(['--storage', 'sqlite3', '--storage-preset-profile', 'invalid', '--topics', '/bag_abi'])
            except SystemExit as error:
                if error.code != 2:
                    raise RuntimeError('Unexpected parser rejection status') from error
            else:
                raise RuntimeError('Invalid SQLite preset accepted')
        player_parser = argparse.ArgumentParser(prog='ros2 bag play')
        PlayVerb().add_arguments(player_parser, 'ros2 bag play')
        parsed = player_parser.parse_args(['--input', str(self.root / 'not-created-bag'), 'sqlite3',
            '--topics', '/bag_abi', '--remap', '/bag_abi:=/bagplay_abi', '__node:=bagplayer_abi',
            '--start-paused', '--disable-keyboard-controls', '--wait-for-all-acked', '5000'])
        if not parsed.start_paused or not parsed.disable_keyboard_controls or parsed.wait_for_all_acked != 5000:
            raise RuntimeError('Selected stock playback parser flags differ')
        if rclpy.ok():
            raise RuntimeError('ABI probe unexpectedly initialized ROS')
        self.emit('BAG_ABI_READY', node_created=False, soabi=sysconfig.get_config_var('SOABI'),
            origins=origins, capsules=capsules, cdr=cdr, qos_setters='NATIVE_CAST_COMPLETED_NOT_MAP_READBACK',
            qos_native_roundtrip=actual, qos_expected=expected, qos_yaml_v9=serialized, presets=presets,
            preset_default=default.storage_preset_profile, invalid_preset_exit=2,
            invalid_preset_diagnostic=rejected.getvalue(), playback_paused=True, playback_ack_timeout_ms=5000,
            maps=Path('/proc/self/maps').read_text())
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.finish_requested():
                return
            time.sleep(0.05)
        raise RuntimeError('Bag ABI map hold deadline exceeded')


def bag_probe_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', choices=('bag-abi',), required=True)
    parser.add_argument('--run', required=True)
    parser.add_argument('--side', choices=('A', 'B'), required=True)
    parser.add_argument('--finish-control', required=True)
    args = parser.parse_args(argv)
    probe = BagRuntimeProbe(args)
    try:
        probe.bag_abi_probe()
        probe.emit('FINAL', success=True, node_created=False)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(bag_probe_main())
