"""Build one isolated project ROS/Python slice; never deploy or run target code.

Host Python runs generators. Explicit, hash-bound OHOS Python/NumPy development
inputs compile target modules. No generic ROS native-library search fallback.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import time

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[3]
INTERFACES = ('builtin_interfaces', 'unique_identifier_msgs', 'service_msgs',
              'action_msgs', 'type_description_interfaces', 'rcl_interfaces',
              'lifecycle_msgs', 'std_msgs')
EXTENSION_SHA = '997bb060c4f5508d6facb730a3d11957eb09976d9a3a481b1e0514b9f7155c4f'


def project_python_digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def project_python_owned_command(argv, environment, stdout, stderr, timeout_seconds):
    """Own the suspended Windows command and all descendants before execution."""
    import ctypes as c
    from ctypes import wintypes as w
    import _winapi
    import msvcrt

    class JobLimits(c.Structure):
        _fields_ = [('times', c.c_longlong * 2), ('flags', w.DWORD),
                    ('working_sets', c.c_size_t * 2), ('active_limit', w.DWORD),
                    ('affinity', c.c_size_t), ('priority', w.DWORD), ('scheduling', w.DWORD)]

    class IoCounters(c.Structure):
        _fields_ = [('counters', c.c_ulonglong * 6)]

    class ExtendedLimits(c.Structure):
        _fields_ = [('basic', JobLimits), ('io', IoCounters), ('memory', c.c_size_t * 4)]

    class JobAccounting(c.Structure):
        _fields_ = [('times', c.c_longlong * 4), ('faults', w.DWORD),
                    ('total', w.DWORD), ('active', w.DWORD), ('terminated', w.DWORD)]

    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [c.c_void_p, w.LPCWSTR]
    kernel.CreateJobObjectW.restype = w.HANDLE
    kernel.SetInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
    kernel.SetInformationJobObject.restype = w.BOOL
    kernel.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
    kernel.AssignProcessToJobObject.restype = w.BOOL
    kernel.QueryInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.c_void_p]
    kernel.QueryInformationJobObject.restype = w.BOOL
    kernel.TerminateJobObject.argtypes = [w.HANDLE, w.UINT]
    kernel.TerminateJobObject.restype = w.BOOL
    kernel.ResumeThread.argtypes = [w.HANDLE]
    kernel.ResumeThread.restype = w.DWORD
    row = {'exit_code': None, 'timed_out': False, 'ownership': 'WINDOWS_UNNAMED_JOB',
           'assigned_before_resume': False, 'tree_empty_confirmed': False,
           'cleanup_timeout_seconds': 5, 'cleanup_required': False}
    job = process = thread = None
    inherited = []
    deadline = time.monotonic() + timeout_seconds
    try:
        job = kernel.CreateJobObjectW(None, None)  # Unnamed and non-inheritable.
        if not job:
            raise c.WinError(c.get_last_error())
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # KILL_ON_JOB_CLOSE; neither breakaway flag.
        if not kernel.SetInformationJobObject(job, 9, c.byref(limits), c.sizeof(limits)):
            raise c.WinError(c.get_last_error())
        current = _winapi.GetCurrentProcess()
        with open(os.devnull, 'rb') as stdin:
            for stream in (stdin, stdout, stderr):
                inherited.append(_winapi.DuplicateHandle(current, msvcrt.get_osfhandle(stream.fileno()),
                                                        current, 0, True, _winapi.DUPLICATE_SAME_ACCESS))
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESTDHANDLES
            startup.hStdInput, startup.hStdOutput, startup.hStdError = inherited
            startup.lpAttributeList = {'handle_list': inherited}
            process, thread, pid, _ = _winapi.CreateProcess(
                None, subprocess.list2cmdline(argv), None, None, True,
                0x4 | subprocess.CREATE_NO_WINDOW, environment, str(REPO), startup)
        row['root_pid'] = pid
        while inherited:
            _winapi.CloseHandle(inherited.pop())
        if not kernel.AssignProcessToJobObject(job, process):
            raise c.WinError(c.get_last_error())
        row['assigned_before_resume'] = True
        if kernel.ResumeThread(thread) == 0xffffffff:
            raise c.WinError(c.get_last_error())
        while True:
            accounting = JobAccounting()
            if not kernel.QueryInformationJobObject(job, 1, c.byref(accounting), c.sizeof(accounting), None):
                raise c.WinError(c.get_last_error())
            row['active_processes_last_observed'] = accounting.active
            if accounting.active == 0 and _winapi.WaitForSingleObject(process, 0) == _winapi.WAIT_OBJECT_0:
                row.update(exit_code=_winapi.GetExitCodeProcess(process), tree_empty_confirmed=True)
                break
            if time.monotonic() >= deadline:
                row['timed_out'] = True
                break
            time.sleep(min(0.02, max(0, deadline - time.monotonic())))
    except BaseException as error:
        row['execution_error'] = repr(error)
    finally:
        if process and not row['tree_empty_confirmed']:
            row['cleanup_required'] = True
            try:
                if row['assigned_before_resume']:
                    if not kernel.TerminateJobObject(job, 1):
                        raise c.WinError(c.get_last_error())
                else:
                    _winapi.TerminateProcess(process, 1)  # Exact suspended root; never resumed.
                cleanup_deadline = time.monotonic() + row['cleanup_timeout_seconds']
                while True:
                    accounting = JobAccounting()
                    if not kernel.QueryInformationJobObject(job, 1, c.byref(accounting), c.sizeof(accounting), None):
                        raise c.WinError(c.get_last_error())
                    row['active_processes_last_observed'] = accounting.active
                    if accounting.active == 0 and _winapi.WaitForSingleObject(process, 0) == _winapi.WAIT_OBJECT_0:
                        row['tree_empty_confirmed'] = True
                        row['root_exit_code_after_cleanup'] = _winapi.GetExitCodeProcess(process)
                        break
                    if time.monotonic() >= cleanup_deadline:
                        raise TimeoutError('Owned command cleanup did not confirm an empty job')
                    time.sleep(0.02)
            except BaseException as error:
                row['cleanup_error'] = repr(error)
        for handle in inherited + [thread, process, job]:
            if handle:
                try:
                    _winapi.CloseHandle(handle)
                except OSError as error:
                    row['cleanup_error'] = repr(error)
    return row


def project_python_record(output, label, argv, environment, timeout_seconds=900):
    if not 0 < timeout_seconds <= 900:
        raise ValueError('Command deadline must be positive and at most 900 seconds')
    number = len(list((output / 'commands').glob('*.json'))) + 1
    stem = output / 'commands' / (f'{number:03d}-' + label)
    row = {'argv': [str(x) for x in argv], 'cwd': str(REPO),
           'started_utc': datetime.now(timezone.utc).isoformat(),
           'scope': 'LOCAL_TARGET_BUILD_NO_DEVICE', 'timeout_seconds': timeout_seconds}
    with stem.with_suffix('.stdout').open('wb') as stdout, stem.with_suffix('.stderr').open('wb') as stderr:
        try:
            row.update(project_python_owned_command(row['argv'], environment, stdout, stderr, timeout_seconds))
        except BaseException as error:
            row.update(exit_code=None, execution_error=repr(error), tree_empty_confirmed=False)
    row['ended_utc'] = datetime.now(timezone.utc).isoformat()
    stem.with_suffix('.json').write_text(json.dumps(row, indent=2) + '\n', encoding='utf-8')
    print(label, row['exit_code'], flush=True)
    if row['exit_code'] != 0 or not row['tree_empty_confirmed'] or row.get('execution_error') or row.get('cleanup_error'):
        raise RuntimeError('Build stopped; inspect ' + str(stem))


def project_python_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True, help='Primary repository with existing read-only artifacts')
    parser.add_argument('--output', type=Path, required=True, help='Contained W3 build directory')
    parser.add_argument('--phase', choices=('build', 'finish-build', 'package'), default='build')
    parser.add_argument('--python-candidates', type=Path, help='Explicit retrieved candidate site-packages and manifest')
    parser.add_argument('--package-output', type=Path, help='Fresh contained package directory; preserves any earlier failed package')
    args = parser.parse_args()
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if not output.is_relative_to(REPO / 'build') or output == REPO / 'build':
        raise ValueError('Output must be a contained W3 build subdirectory')
    evidence = root / 'build/r24-project-python'
    dev = evidence / 'python-dev'
    corrected = evidence / 'python-dev-fix1'
    selection = json.loads((root / 'build/ros2-target/r2/BUILD_SELECTION.json').read_text())
    sdk, cmake, python, ninja, host = [Path(selection[key]) for key in ('sdk', 'cmake', 'python', 'ninja', 'host_tools')]
    runtime_manifest = json.loads((evidence / 'python-inputs/A-PYTHON_RUNTIME_ARTIFACT.manifest.json').read_text())
    validation = json.loads((corrected / 'VALIDATION.json').read_text())
    if validation.get('status') != 'PASS':
        raise ValueError('Target development input preparation did not pass')
    # Recompute every retrieved development-file digest, not only prior status.
    manifest = json.loads((dev / 'MANIFEST.json').read_text())
    selected_dev = {name: row for name, row in manifest.items()
                    if name.startswith(('usr/include/python3.12/', 'numpy-include/'))
                    or name == 'usr/lib/libpython3.12.so.1.0'}
    for name, row in selected_dev.items():
        if project_python_digest(dev / name) != row['sha256']:
            raise ValueError('Retrieved target dev input changed: ' + name)
    headers = [name for name in selected_dev if name.startswith('usr/include/python3.12/')]
    if len(headers) != 214 or sum((dev / n).stat().st_size for n in headers) != 1285109:
        raise ValueError('Unexpected CPython header membership')
    numpy_headers = [name for name in selected_dev if name.startswith('numpy-include/')]
    if len(numpy_headers) != 31 or sum((dev / n).stat().st_size for n in numpy_headers) != 418555:
        raise ValueError('Unexpected target NumPy header membership')
    for row in runtime_manifest['runtime_interface']['files']:
        if row['path'] in ('include/python3.12/Python.h', 'include/python3.12/patchlevel.h', 'lib/libpython3.12.so.1.0'):
            if project_python_digest(dev / 'usr' / row['path']) != row['sha256']:
                raise ValueError('Target CPython interface mismatch')
    header_checks = json.loads((corrected / 'HEADERS_VALIDATION.json').read_text())
    if len(header_checks) != 2 or any(not row.get('match') or row.get('remote_count') != 214 for row in header_checks):
        raise ValueError('Exact A/B target header evidence missing')
    if project_python_digest(corrected / '_sysconfigdata__linux_aarch64-linux-ohos.py') != '222dc493a2ab757af62c05f183101c5bbabe6d96b79b0cb5743286e76398ea10':
        raise ValueError('Exact target sysconfig missing or changed')
    for tool, digest in selection['tool_sha256'].items():
        if project_python_digest(tool) != digest:
            raise ValueError('Selected installed build tool changed: ' + tool)
    spec = importlib.util.spec_from_file_location('project_ros_slice', Path(__file__).with_name('build_ros_slice.py'))
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    deps = [Path(row['path']) for row in selection['dependency_prefixes']]
    fast = root / 'build/g2a1/target214-install'
    ros = root / 'build/ros2-target/r2/ros/install'
    action = root / 'build/r18-action-native/dependency-build/install'
    install = output / 'install'
    prefixes = [install, action, ros, fast] + deps
    environment = os.environ.copy()
    environment.update(PYTHONDONTWRITEBYTECODE='1', CMAKE_BUILD_PARALLEL_LEVEL='4',
                       AMENT_PREFIX_PATH=os.pathsep.join(str(p) for p in prefixes),
                       PYTHONPATH=os.pathsep.join([str(install / 'lib/python3.12/site-packages'), str(host / 'Lib/site-packages')]),
                       PATH=os.pathsep.join([str(python.parent), str(python.parent / 'Scripts'), str(python.parent / 'Library/bin'), environment.get('PATH', '')]))
    if args.phase in ('build', 'finish-build'):
        if args.phase == 'build':
            output.mkdir(parents=True, exist_ok=False)
            (output / 'commands').mkdir()
            install.mkdir()
        elif not (output / 'commands/030-rcl_lifecycle-configure.json').is_file():
            raise ValueError('finish-build requires the preserved initial R24 interface build')
        available = builder.sources(root / 'build/ros2-target/r2/src/ros')
        available['rosidl_generator_py'] = root / 'ros2/src/ros2/rosidl_python/rosidl_generator_py'
        source_records = {}
        packages = ('rosidl_generator_py',) + INTERFACES + ('rcl_lifecycle', 'rmw_implementation')
        for name in packages:
            source = available[name]
            destination = output / 'src' / name
            if args.phase == 'build':
                shutil.copytree(source, destination, ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '.git'))
            source_records[name] = {'source': str(source), 'files': {
                p.relative_to(destination).as_posix(): project_python_digest(p)
                for p in sorted(destination.rglob('*')) if p.is_file()}}
        # Exact upstream OHOS cross-build adaptation, isolated snapshot only.
        patch = root / 'ros2/ohos/patches_full/ros2_rosidl_python.patch'
        if project_python_digest(patch) != '5778a37e5405d8161e03d73e689c5423ddf27a292c9a313db6efd38161d2ca1b':
            raise ValueError('Reviewed Python generator adaptation changed')
        directory = (output / 'src').relative_to(REPO).as_posix()
        if args.phase == 'build':
            project_python_record(output, 'generator-patch-check', ['git', 'apply', '--check', '--directory=' + directory, patch], environment)
            project_python_record(output, 'generator-patch', ['git', 'apply', '--directory=' + directory, patch], environment)
        else:
            project_python_record(output, 'generator-patch-reverse-check', ['git', 'apply', '--reverse', '--check', '--directory=' + directory, patch], environment)
            packages = ('rcl_lifecycle', 'rmw_implementation')
        for name in packages:
            current = {k: v for k, v in builder.package_dirs(host).items()
                       if k.startswith('ament_') or k in builder.PURE_TOOLS or k == 'python_cmake_module'}
            for prefix in reversed(prefixes):
                current.update(builder.package_dirs(prefix))
            folder = output / ('build-' + name)
            argv = [cmake, '-S', output / 'src' / name, '-B', folder, '-G', 'Ninja',
                    '-DCMAKE_MAKE_PROGRAM=' + ninja.as_posix(),
                    '-DCMAKE_TOOLCHAIN_FILE=' + str(sdk / 'build/cmake/ohos.toolchain.cmake'),
                    '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared', '-DCMAKE_BUILD_TYPE=Release',
                    '-DBUILD_SHARED_LIBS=ON', '-DBUILD_TESTING=OFF', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                    '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                    '-DCMAKE_INSTALL_PREFIX=' + str(install), '-DPython3_EXECUTABLE=' + str(python),
                    '-DCMAKE_PROJECT_INCLUDE=' + str(Path(__file__).with_name('ProjectPython.cmake')),
                    '-DPROJECT_PYTHON_INCLUDE=' + str(dev / 'usr/include/python3.12'),
                    '-DPROJECT_PYTHON_LIBRARY=' + str(dev / 'usr/lib/libpython3.12.so.1.0'),
                    '-DPROJECT_NUMPY_INCLUDE=' + str(dev / 'numpy-include'),
                    '-DCMAKE_PREFIX_PATH=' + ';'.join(str(p) for p in prefixes)]
            argv += ['-D' + k + '_DIR:PATH=' + v.as_posix() for k, v in sorted(current.items())]
            library_dirs = [prefix / leaf for prefix in prefixes for leaf in ('lib', 'Lib') if (prefix / leaf).is_dir()]
            flags = ' '.join('-L"' + p.as_posix() + '"' for p in library_dirs)
            argv += ['-DCMAKE_SHARED_LINKER_FLAGS=' + flags,
                     '-DFastRTPS_INCLUDE_DIR:PATH=' + (fast / 'include').as_posix(),
                     '-DFastRTPS_LIBRARY_RELEASE:FILEPATH=' + (fast / 'lib/libfastrtps.so').as_posix(),
                     '-DFastCDR_LIBRARY_RELEASE:FILEPATH=' + (deps[0] / 'lib/libfastcdr.so').as_posix()]
            project_python_record(output, name + '-configure', argv, environment)
            project_python_record(output, name + '-build', [cmake, '--build', folder, '--parallel', '4'], environment)
            project_python_record(output, name + '-install', [cmake, '--install', folder], environment)
        receipt = {'scope': 'PROJECT_TARGET_PYTHON_BUILD_NOT_DEVICE_TEST', 'sources': source_records,
                   'patch': {'path': str(patch), 'sha256': project_python_digest(patch)},
                   'development_inputs': selected_dev, 'prefixes': [str(p) for p in prefixes],
                   'artifacts': {p.relative_to(install).as_posix(): project_python_digest(p)
                                 for p in sorted(install.rglob('*')) if p.is_file()},
                   'driver_sha256': project_python_digest(__file__),
                   'hook_sha256': project_python_digest(Path(__file__).with_name('ProjectPython.cmake'))}
        (output / 'BUILD.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        print('PROJECT_PYTHON_TARGET_BUILD_PRESENT_NOT_DEVICE_VERIFIED', flush=True)
        return
    if not args.python_candidates:
        raise ValueError('Package phase requires explicit reviewed Python candidate input')
    built = json.loads((output / 'BUILD.json').read_text())
    for name, digest in built['artifacts'].items():
        if project_python_digest(install / name) != digest:
            raise ValueError('Built project artifact changed: ' + name)
    candidate = args.python_candidates.resolve()
    for label in ('OVERLAY_VALIDATION.json', 'GENERIC_VALIDATION.json'):
        if json.loads((candidate / label).read_text()).get('status') != 'PASS':
            raise ValueError('Unbound Python candidate origin: ' + label)
    candidate_files = json.loads((candidate / 'INPUT_MANIFEST.json').read_text())
    for name, row in candidate_files.items():
        if project_python_digest(candidate / name) != row['sha256']:
            raise ValueError('Retrieved Python candidate changed: ' + name)
    numpy_observed = json.loads((evidence / 'numpy-diagnostic/RESULT.json').read_text())
    if len(numpy_observed) != 2 or any(row.get('version') != '1.26.4' or row.get('arange') != [0, 1, 2]
                                     or row.get('remote_completion') != 'NUMPY_REMOTE_EXIT=0' for row in numpy_observed):
        raise ValueError('Measured NumPy candidate input evidence missing')
    native_selection = {'generic/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so',
                        'overlay/psutil/_psutil_linux.cpython-312-aarch64-linux-ohos.so',
                        'overlay/psutil/_psutil_posix.cpython-312-aarch64-linux-ohos.so'}
    for path in numpy_observed[0]['paths']:
        prefix = '/data/python312-rk3588a-verify-c646982b346f/ros2-site-packages/'
        if path.startswith(prefix) and '.so' in path:
            native_selection.add('overlay/' + path[len(prefix):])
    if project_python_digest(candidate / 'generic/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so') != EXTENSION_SHA:
        raise ValueError('Observed rclpy binary candidate changed')
    bundle = args.package_output.resolve() if args.package_output else output / 'package'
    if not bundle.is_relative_to(output) or bundle == output:
        raise ValueError('Package output must be contained in this build output')
    bundle.mkdir(exist_ok=False)
    (bundle / 'python').mkdir()
    provenance = {}
    omitted = []
    for name in candidate_files:
        source = candidate / name
        if source.read_bytes()[:4] == b'\x7fELF' and name not in native_selection:
            omitted.append(name)
            continue
        if '__pycache__' in source.parts or source.suffix == '.pyc':
            continue
        relative = 'python/' + name.split('/', 1)[1]
        destination = bundle / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        provenance[relative] = {'source': str(source), 'origin': 'HASH_BOUND_DEPLOYED_PYTHON_CANDIDATE'}
    # Generated messages need the installed Python typesupport importer as well
    # as their native modules. Both come from this exact project target build.
    generated = install / 'lib/python3.12/site-packages'
    for name in ('rosidl_generator_py',) + INTERFACES:
        for source in sorted((generated / name).rglob('*')):
            if not source.is_file() or '__pycache__' in source.parts or source.suffix == '.pyc':
                continue
            relative = 'python/' + source.relative_to(generated).as_posix()
            destination = bundle / relative
            if destination.exists():
                raise ValueError('Generated/candidate Python package collision: ' + relative)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            provenance[relative] = {'source': str(source), 'origin': (
                'PROJECT_BUILT_PYTHON_RUNTIME' if name == 'rosidl_generator_py' else 'PROJECT_GENERATED_PYTHON')}
    # interface show reads this pure parser API. Use the project's archived
    # rosidl source, not a Host Python installation or another generic native lib.
    adapter = root / 'build/ros2-target/r2/src/ros/rosidl/rosidl_adapter/rosidl_adapter'
    for source in sorted(adapter.rglob('*.py')):
        relative = 'python/rosidl_adapter/' + source.relative_to(adapter).as_posix()
        destination = bundle / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        provenance[relative] = {'source': str(source), 'origin': 'PROJECT_ARCHIVED_PURE_PYTHON'}
    # Complete interface resources come from project installs, never generic Lib.
    shutil.copytree(install / 'share', bundle / 'share')
    index = bundle / 'share/ament_index/resource_index/packages'
    index.mkdir(parents=True, exist_ok=True)
    (index / 'rclpy').write_bytes(b'')
    readelf = sdk / 'llvm/bin/llvm-readelf.exe'
    elf_spec = importlib.util.spec_from_file_location('project_elf_selection', Path(__file__).with_name('package_elf.py'))
    elf = importlib.util.module_from_spec(elf_spec)
    elf_spec.loader.exec_module(elf)
    catalog, native_rows = {}, {}
    for prefix in prefixes + [root / 'build/r19baglocal5/bundle']:
        for source in sorted(prefix.rglob('*')):
            if source.is_file() and re.fullmatch(r'.+\.so(?:\.[0-9.]+)?', source.name):
                row = elf.inspect(readelf, source)
                # Windows-materialized unversioned aliases can retain stale
                # build RUNPATHs. Select the actual SONAME filename explicitly.
                if row['soname'] and source.name == row['soname']:
                    catalog.setdefault(row['soname'], (source, row))
    pending = [p for p in (bundle / 'python').rglob('*') if p.is_file() and p.read_bytes()[:4] == b'\x7fELF']
    # Runtime-selected RMW and String C typesupport are explicit dynamic roots.
    for name in ('librmw_fastrtps_cpp.so', 'librmw_fastrtps_shared_cpp.so',
                 'libstd_msgs__rosidl_typesupport_fastrtps_c.so', 'librmw_implementation.so'):
        if name not in catalog:
            raise ValueError('Required project dynamic root missing: ' + name)
        source, _ = catalog[name]
        pending.append(source)
    external = {'libc.so', 'libc.musl-aarch64.so.1', 'libpython3.12.so.1.0'}
    (bundle / 'lib').mkdir()
    inspected = set()
    while pending:
        source = pending.pop()
        key = str(source.resolve())
        if key in inspected:
            continue
        inspected.add(key)
        row = elf.inspect(readelf, source)
        target = source if source.is_relative_to(bundle) else bundle / 'lib' / (row['soname'] or source.name)
        relative = target.relative_to(bundle).as_posix()
        if target != source:
            if target.exists() and project_python_digest(target) != row['sha256']:
                raise ValueError('Conflicting selected project library: ' + relative)
            shutil.copyfile(source, target)
            provenance[relative] = {'source': str(source), 'origin': 'PROJECT_TARGET_LIBRARY'}
        # The only accepted candidate RPATH is the observed contained NumPy path.
        paths = re.findall(r'\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]', row['readelf_stdout'])
        if paths and paths != ['$ORIGIN/../../numpy.libs']:
            raise ValueError('Unreviewed native search path: ' + relative)
        for path in paths:
            resolved = (target.parent / path.replace('$ORIGIN/', '')).resolve()
            if not resolved.is_relative_to(bundle):
                raise ValueError('Candidate RPATH escapes isolated slice')
        native_rows[relative] = row
        for needed in row['needed']:
            if needed in external:
                continue
            bundled = [p for p in (bundle / 'python').rglob(needed) if p.is_file()]
            if bundled:
                if len(bundled) != 1:
                    raise ValueError('Ambiguous candidate native dependency: ' + needed)
                pending.append(bundled[0])
            elif needed in catalog:
                pending.append(catalog[needed][0])
            else:
                raise ValueError('Unresolved isolated native dependency: ' + needed)
    # Required dynamic symbol names must be available from selected native files,
    # exact libpython and SDK platform libc. This is necessary, not an ABI verdict.
    providers = list(native_rows)
    platform_libc = evidence / 'platform-inputs/ld-musl-aarch64.so.1'
    if project_python_digest(platform_libc) != 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86':
        raise ValueError('Actual target platform export-provider input changed')
    # Inspect only; the platform loader is never copied into the bundle.
    symbol_files = [bundle / p for p in providers] + [dev / 'usr/lib/libpython3.12.so.1.0', platform_libc]
    exports, requirements = set(), {}
    for file in symbol_files:
        result = subprocess.run([str(readelf), '--dyn-symbols', '--wide', str(file)], capture_output=True, text=True, check=True, timeout=30)
        undefined = set()
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) < 8 or not parts[0].rstrip(':').isdigit():
                continue
            symbol = parts[7].split('@')[0]
            if parts[6] != 'UND':
                exports.add(symbol)
            elif parts[4] == 'GLOBAL':
                undefined.add(symbol)
        requirements[str(file)] = undefined
    missing = {name: sorted(symbols - exports) for name, symbols in requirements.items() if symbols - exports}
    (bundle / 'SYMBOL_CLOSURE.json').write_text(json.dumps({'scope': 'NECESSARY_SYMBOL_NAME_CHECK_NOT_ABI_PROOF',
        'platform_provider': {'path': str(platform_libc), 'sha256': project_python_digest(platform_libc), 'not_bundled': True},
        'missing': missing}, indent=2), encoding='utf-8')
    if missing:
        raise ValueError('Required dynamic symbol closure incomplete; inspect package SYMBOL_CLOSURE.json')
    shutil.copyfile(Path(__file__).with_name('project_python_probe.py'), bundle / 'project_python_probe.py')
    runtime_prefix = '/data/python312-rk3588a-verify-c646982b346f'
    run = '''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -P "${0%/*}" && pwd)
unset PYTHONHOME PYTHONUSERBASE _PYTHON_SYSCONFIGDATA_NAME _PYTHON_PROJECT_BASE
unset FASTRTPS_DEFAULT_PROFILES_FILE FASTDDS_DEFAULT_PROFILES_FILE CYCLONEDDS_URI
export PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$HERE/python"
export AMENT_PREFIX_PATH="$HERE"
export LD_LIBRARY_PATH="$HERE/lib:@RUNTIME@/usr/lib"
export LD_PRELOAD="@RUNTIME@/usr/lib/libpython3.12.so.1.0"
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export RCUTILS_COLORIZED_OUTPUT=0
[ "$(sha256sum "$LD_PRELOAD" | cut -d ' ' -f1)" = 47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a ] || exit 70
case "${1:-}" in
 probe) exec @RUNTIME@/usr/bin/python3.12 -P -s -B -c 'import os,runpy,sys; sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_GLOBAL); runpy.run_path(sys.argv[1],run_name="__main__")' "$HERE/project_python_probe.py" ;;
 help) shift; set -- --help ;;
 pkg_prefix) shift; set -- pkg prefix rclpy ;;
 interface) shift; set -- interface show std_msgs/msg/String ;;
 *) exit 64 ;;
esac
exec @RUNTIME@/usr/bin/python3.12 -P -s -B -c 'import os,sys; sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_GLOBAL); sys.argv[0]="ros2"; from ros2cli.cli import main; sys.exit(main())' "$@"
'''.replace('@RUNTIME@', runtime_prefix)
    (bundle / 'run').write_bytes(run.encode())
    files = {p.relative_to(bundle).as_posix(): {'sha256': project_python_digest(p), 'bytes': p.stat().st_size,
              **provenance.get(p.relative_to(bundle).as_posix(), {'origin': 'PROJECT_SLICE_RESOURCE_OR_ENTRY'})}
             for p in sorted(bundle.rglob('*')) if p.is_file()}
    package = {'schema': 1, 'scope': 'PROJECT_PYTHON_ABI_CANDIDATE_NOT_DEVICE_VERIFIED',
               'runtime_prefix': runtime_prefix, 'files': files, 'native': native_rows,
               'external': sorted(external), 'omitted_candidate_native': omitted,
               'build_receipt_sha256': project_python_digest(output / 'BUILD.json')}
    (bundle / 'PACKAGE.json').write_text(json.dumps(package, indent=2) + '\n', encoding='utf-8')
    print('PROJECT_PYTHON_SLICE_PACKAGED_NOT_DEVICE_VERIFIED files=' + str(len(files)), flush=True)


if __name__ == '__main__':
    project_python_main()
