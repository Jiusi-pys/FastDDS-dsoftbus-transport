"""Native ROS CLI provenance probe. Imports only; never initializes rclpy or a Node."""
import hashlib
import importlib
import importlib.machinery
import importlib.metadata
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys
import sysconfig


def cli_probe_file(filename):
    path = Path(filename)
    result = {'path': str(path), 'resolved': str(path.resolve())}
    try:
        result.update(bytes=path.stat().st_size,
                      sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    except OSError as error:
        result['error'] = str(error)
    return result


def cli_probe_module(name, required=True):
    result = {'required': required, 'loaded': name in sys.modules}
    try:
        # Optional dependencies are inspected without importing or mocking them.
        module = importlib.import_module(name) if required else sys.modules.get(name)
        spec = module.__spec__ if module is not None else importlib.util.find_spec(name)
        result['origin'] = spec.origin if spec else None
        result['loader'] = type(spec.loader).__name__ if spec else None
        if spec and spec.origin and Path(spec.origin).is_file():
            result['file'] = cli_probe_file(spec.origin)
        result['loaded'] = name in sys.modules
        if required and (not spec or not result.get('file', {}).get('sha256')):
            result['error'] = 'required module has no hashable file origin'
    except Exception as error:
        result['error'] = repr(error)
    result['distributions'] = []
    for dist_name in importlib.metadata.packages_distributions().get(name, []):
        try:
            dist = importlib.metadata.distribution(dist_name)
            result['distributions'].append({
                'name': dist.metadata['Name'], 'version': dist.version,
                'root': str(dist.locate_file('')),
                'entry_points': [{'group': ep.group, 'name': ep.name, 'value': ep.value}
                                 for ep in dist.entry_points]})
        except Exception as error:
            result.setdefault('metadata_errors', []).append(repr(error))
    if not required:
        result['stub_assessment'] = 'UNDETERMINED: origin/hash only; no substitute imported'
    return result


if __name__ == '__main__':
    report = {
        'schema': 1, 'scope': 'NATIVE_ROS2_CLI_STARTUP_ONLY',
        'dsoftbus_communications_verified': False,
        'python': {'version': sys.version, 'executable': sys.executable,
                   'prefix': sys.prefix, 'base_prefix': sys.base_prefix,
                   'path': sys.path, 'machine': platform.machine(),
                   'SOABI': sysconfig.get_config_var('SOABI'),
                   'EXT_SUFFIX': sysconfig.get_config_var('EXT_SUFFIX'),
                   'dlopen_flags': sys.getdlopenflags()},
        'environment': {key: os.environ.get(key) for key in (
            'ROS2_HOME', 'ROS_DISTRO', 'RMW_IMPLEMENTATION', 'FASTDDS_BUILTIN_TRANSPORTS',
            'ROS2_PYTHON_BOOTSTRAP_DIR', 'ROS2_PYTHON_BOOTSTRAP_SHA256',
            'ROS2_PYTHON_REMOTE_PREFIX', 'ROS2_ARCHIVE_SHA256',
            'ROS2_BUILD_RECEIPT_SHA256', 'ROS2_RELEASE_PROVENANCE_SHA256',
            'PYTHONPATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD')},
        'modules': {}, 'artifacts': {}, 'errors': [],
    }
    for name in ('rclpy', 'ros2cli', 'ament_index_python', 'rpyutils'):
        report['modules'][name] = cli_probe_module(name)
        if 'error' in report['modules'][name]:
            report['errors'].append(name + ': ' + report['modules'][name]['error'])
    try:
        from rclpy.impl.implementation_singleton import rclpy_implementation
        origin = rclpy_implementation.__file__
        report['rclpy_extension'] = cli_probe_file(origin)
        report['rclpy_extension']['module'] = rclpy_implementation.__name__
        if not any(origin.endswith(suffix) for suffix in importlib.machinery.EXTENSION_SUFFIXES):
            report['errors'].append('rclpy implementation is not a native extension')
        if 'sha256' not in report['rclpy_extension']:
            report['errors'].append('rclpy extension is not hashable')
    except Exception as error:
        report['errors'].append('rclpy extension: ' + repr(error))
    for name in ('numpy', 'psutil'):
        report['modules'][name] = cli_probe_module(name, required=False)
    bootstrap = os.environ.get('ROS2_PYTHON_BOOTSTRAP_DIR', '')
    for label, filename in (
            ('wrapper', '/usr/local/bin/ros2'),
            ('environment', '/data/local/tmp/ros2-generic/env.sh'),
            ('bootstrap', bootstrap + '/sitecustomize.py'),
            ('probe', __file__), ('python', sys.executable),
            ('build_receipt', '/data/local/tmp/ros2-generic/build_receipt.json'),
            ('release_provenance', '/data/local/tmp/ros2-generic/release_provenance.json')):
        report['artifacts'][label] = cli_probe_file(filename)
        if 'error' in report['artifacts'][label]:
            report['errors'].append(label + ': ' + report['artifacts'][label]['error'])
    expected = '/data/python312-rk3588a-verify-c646982b346f/usr/bin/python3.12'
    if sys.executable != expected or sys.version_info[:2] != (3, 12):
        report['errors'].append('unexpected Python executable/version')
    if platform.machine() not in ('aarch64', 'arm64') or not str(report['python']['SOABI']).endswith('-aarch64-linux-ohos'):
        report['errors'].append('unexpected architecture/SOABI')
    if report['environment']['FASTDDS_BUILTIN_TRANSPORTS'] != 'UDPv4':
        report['errors'].append('reviewed UDPv4 environment changed')
    report['loaded_dsos'] = []
    try:
        # Preserve maps lines as evidence; deduplicate hash work by mapped filename.
        lines = Path('/proc/self/maps').read_text().splitlines()
        selected = [line for line in lines if any(key in line for key in (
            'ros2', 'python3.12', 'rclpy', 'fastrtps', 'fastdds', 'libc++', 'libc.so',
            'librcl', 'librmw', 'librcutils', 'librcpputils'))]
        report['maps_lines'] = selected
        paths = {line.split(None, 5)[5] for line in selected if len(line.split(None, 5)) == 6}
        report['loaded_dsos'] = [cli_probe_file(path) for path in sorted(paths)
                                 if path.startswith('/') and '.so' in path]
        report['requested_dso_observations'] = {
            name: [path for path in sorted(paths) if Path(path).name.startswith(name + '.so')]
            for name in ('librcl_action', 'librcl_lifecycle', 'librcl', 'librmw',
                         'librmw_implementation', 'librcutils', 'librcpputils')}
        report['dso_observation_note'] = 'Empty lists mean NOT_LOADED in this import-only process; no ABI compatibility claim'
        if not report['loaded_dsos']:
            report['errors'].append('no relevant mapped DSOs captured')
    except OSError as error:
        report['errors'].append('/proc/self/maps: ' + str(error))
    report['status'] = 'FAIL' if report['errors'] else 'PASS'
    print(json.dumps(report, indent=2, sort_keys=True))
    sys.exit(1 if report['errors'] else 0)
