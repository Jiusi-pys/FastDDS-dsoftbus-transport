"""No-node project Python ABI diagnostic, run only by the isolated slice entry."""
import hashlib
import ctypes
import importlib
import json
import os
from pathlib import Path
import platform
import sys
import sysconfig
import traceback


def project_probe_file(path):
    path = Path(path)
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'bytes': path.stat().st_size}


def project_probe_maps(prefix, manifest):
    paths, rows = {}, []
    for line in Path('/proc/self/maps').read_text().splitlines():
        fields = line.split(None, 5)
        if len(fields) < 5:
            raise ValueError('Malformed process mapping: ' + line)
        path = fields[5] if len(fields) == 6 else ''
        if not path or (path.startswith('[') and path.endswith(']')):
            # Kernel labels can contain a DSO's name, e.g. anonymous musl BSS.
            # They are diagnostics, never filenames to open or native providers.
            rows.append({'path': path or '[anonymous]', 'origin': 'ANONYMOUS',
                         'mapping_lines': [line]})
            continue
        if '.so' not in path:
            continue
        if not path.startswith('/') or path.endswith(' (deleted)'):
            raise ValueError('Unknown or deleted shared-library mapping: ' + path)
        paths.setdefault(path, []).append(line)
    expected = manifest['files']
    runtime = Path(manifest['runtime_prefix']) / 'usr'
    for path in sorted(paths):
        file = Path(path)
        row = project_probe_file(file)
        row['mapping_lines'] = paths[path]
        if file.is_relative_to(prefix):
            relative = file.relative_to(prefix).as_posix()
            row['origin'] = 'PROJECT_SLICE'
            if relative not in expected or expected[relative]['sha256'] != row['sha256']:
                raise ValueError('Unselected or changed project native file: ' + path)
        elif file.is_relative_to(runtime / 'lib'):
            # This is the unchanged, separately provenance-bound CPython runtime.
            # ROS/FastDDS/serialization libraries are never accepted here.
            if any(token in file.name for token in ('rcl', 'rmw', 'fastcdr', 'fastrtps', 'rosidl', 'rcutils', 'rcpputils', 'softbus', 'mdds')):
                raise ValueError('ROS native library outside project slice: ' + path)
            row['origin'] = 'EXISTING_CPYTHON_RUNTIME'
        elif str(file) == '/system/lib/ld-musl-aarch64.so.1':
            if row['sha256'] != 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86':
                raise ValueError('Platform libc loader changed')
            row['origin'] = 'OPENHARMONY_PLATFORM'
        elif file.is_relative_to(Path('/system/lib64')):
            if any(token in file.name for token in ('rcl', 'rmw', 'fastcdr', 'fastrtps', 'rosidl', 'rcutils', 'rcpputils', '_msgs', 'builtin_interfaces')):
                raise ValueError('ROS library resolved from platform fallback: ' + path)
            row['origin'] = 'OPENHARMONY_PLATFORM'
        else:
            raise ValueError('Native origin outside declared slice/runtime/platform: ' + path)
        rows.append(row)
    return rows


def project_probe_main():
    prefix = Path(__file__).resolve().parent
    manifest = json.loads((prefix / 'PACKAGE.json').read_text())
    report = {'schema': 1, 'scope': 'PROJECT_LINKED_PYTHON_ABI_NO_NODE',
              'status': 'FAIL', 'dsoftbus_communication_verified': False,
              'python': {'executable': sys.executable, 'version': sys.version,
                         'prefix': sys.prefix, 'path': sys.path,
                         'SOABI': sysconfig.get_config_var('SOABI'),
                         'EXT_SUFFIX': sysconfig.get_config_var('EXT_SUFFIX'),
                         'machine': platform.machine()}, 'errors': []}
    try:
        if (sys.executable != manifest['runtime_prefix'] + '/usr/bin/python3.12'
                or sys.version_info[:3] != (3, 12, 7)
                or sysconfig.get_config_var('SOABI') != 'cpython-312-aarch64-linux-ohos'):
            raise ValueError('Unexpected native Python runtime/ABI')
        if os.environ.get('LD_LIBRARY_PATH') != str(prefix / 'lib') + ':' + manifest['runtime_prefix'] + '/usr/lib':
            raise ValueError('Unexpected native search path')
        if any('ros2-generic' in path for path in sys.path):
            raise ValueError('Generic Python fallback visible')
        report['maps_before'] = project_probe_maps(prefix, manifest)
        libc, alias = ctypes.CDLL('libc.so'), ctypes.CDLL('libc.musl-aarch64.so.1')
        report['libc_alias'] = {'same_handle': libc._handle == alias._handle,
                               'same_malloc': ctypes.cast(libc.malloc, ctypes.c_void_p).value == ctypes.cast(alias.malloc, ctypes.c_void_p).value}
        if not all(report['libc_alias'].values()):
            raise ValueError('NumPy musl dependency did not resolve to the platform libc')
        for name in ('rclpy', 'ros2cli', 'ament_index_python', 'rpyutils', 'numpy'):
            module = importlib.import_module(name)
            path = Path(module.__file__).resolve()
            if not path.is_relative_to(prefix / 'python'):
                raise ValueError('Python module escaped slice: ' + name)
            report.setdefault('modules', {})[name] = project_probe_file(path)
        from rclpy.impl.implementation_singleton import rclpy_implementation
        report['rclpy_extension'] = project_probe_file(rclpy_implementation.__file__)
        from rclpy.serialization import deserialize_message, serialize_message
        from std_msgs.msg import String
        # Fixed independent CDR1 little-endian framing/string oracle, including
        # UTF-8 and terminating NUL. No Context, Node or Participant is created.
        value = 'project-python-ABI-\u4e2d\u6587'
        encoded = value.encode('utf-8') + b'\0'
        expected = b'\x00\x01\x00\x00' + len(encoded).to_bytes(4, 'little') + encoded
        message = String(data=value)
        actual = serialize_message(message)
        restored = deserialize_message(actual, String)
        report['string'] = {'value': value, 'expected_hex': expected.hex(),
                            'actual_hex': actual.hex(), 'restored': restored.data}
        if actual != expected or restored.data != value:
            raise ValueError('String CDR bytes or round-trip value mismatch')
        report['maps_after'] = project_probe_maps(prefix, manifest)
        loaded = {Path(row['path']).name: row for row in report['maps_after']}
        for name in ('libfastrtps.so.2.14', 'libfastcdr.so.2', 'librcl.so', 'librcl_action.so',
                     'librcl_lifecycle.so', 'librmw_implementation.so', 'librmw_fastrtps_cpp.so',
                     'librmw_fastrtps_shared_cpp.so', 'librcutils.so', 'librcpputils.so'):
            if name not in loaded or loaded[name]['origin'] != 'PROJECT_SLICE':
                raise ValueError('Required selected project DSO was not loaded: ' + name)
        report['status'] = 'PASS'
    except Exception as error:
        report['errors'].append(repr(error))
        report['traceback'] = traceback.format_exc()
        try:
            report['maps_after_failure'] = project_probe_maps(prefix, manifest)
        except Exception as maps_error:
            report['errors'].append('Failure map capture: ' + repr(maps_error))
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(project_probe_main())
