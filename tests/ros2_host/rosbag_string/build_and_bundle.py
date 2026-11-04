"""Build/package the local persistence fixture; never deploy or execute target code.

Uses the qualified original cpp compiler include/link bindings, with explicit
consumer changes. This is a bounded test driver, not a package qualification gate.
Independent tester owns invocation and interpretation of execution evidence.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys
import time


# These are line lists generated on Windows, consumed by LF-only target readers.
# Only these two runtime copies may differ from their immutable installed inputs.
INDEXES = {
    'share/ament_index/resource_index/rosbag2_storage__pluginlib__plugin/rosbag2_storage_sqlite3': (
        '80f4a17c8968e2d6cddc18fe96892c41099995fa8ece838b7bf7988a25ae0af1',
        'ab7dc545abfa5ebc0308cad8f80992d078a73039f507289d7900da75f6f44841'),
    'share/ament_index/resource_index/rosidl_interfaces/std_msgs': (
        'f5a583299fcb26a806db4ffceaadec7cdc3889eacaf9e1eecf3a0728a52cc070',
        'fd01977513492eafd6c057b934dc90a2e9b6aa31e28e265cf66159ad1d274560'),
}


def lf_index(data, expected_sha256):
    derived = data.replace(b'\r\n', b'\n')
    if b'\r' in derived:
        raise ValueError('Stray CR in runtime line-list resource')
    if hashlib.sha256(derived).hexdigest() != expected_sha256:
        raise ValueError('Runtime index entries/order/final newline differ qualified content')
    return derived


def runtime_resource(relative, data):
    if relative not in INDEXES:
        return data, 'BYTE_IDENTICAL'
    return lf_index(data, INDEXES[relative][1]), 'CRLF_TO_LF_ONLY_EXACT_INDEX'


def record(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or (part.exists() and getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError('Reparse input: '+str(part))
    data = path.read_bytes()
    info = path.stat()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                identity=[info.st_dev, info.st_ino])


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')


def run(name, argv, output, env, seconds):
    argv = [str(x) for x in argv]
    receipt = dict(argv=argv, cwd=str(output), environment=env, shell=False,
                   timeout_seconds=seconds, started_utc=datetime.now(timezone.utc).isoformat())
    started = time.monotonic()
    with (output/(name+'.stdout.bin')).open('wb') as stdout, (output/(name+'.stderr.bin')).open('wb') as stderr:
        try:
            result = subprocess.run(argv, cwd=output, env=env, stdout=stdout, stderr=stderr,
                                    shell=False, timeout=seconds)
            receipt.update(exit_code=result.returncode, timed_out=False)
        except subprocess.TimeoutExpired:
            receipt.update(exit_code=None, timed_out=True)
    receipt['elapsed_seconds'] = time.monotonic()-started
    save(output/(name+'.PROCESS.json'), receipt)
    if receipt['timed_out'] or receipt['exit_code'] != 0:
        raise RuntimeError(name+' failed; preserved process receipt/logs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifact-repo', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo, sdk, output = (p.resolve() for p in (args.artifact_repo, args.sdk, args.output))
    if output.parent != repo/'build' or not re.fullmatch(r'r19baglocal[1-9][0-9]*', output.name) or output.exists():
        raise ValueError('Fresh immediate artifact-repo/build/r19baglocal<positive integer> required')
    worktree = Path(__file__).resolve().parents[3]
    source = Path(__file__).with_name('main.cpp').resolve()
    packager = worktree/'integrations/ros2/target/package_elf.py'
    entry_helper = worktree/'integrations/ros2/target/runtime_entry.py'
    evidence = repo/'build/r19-rosbag-preparation'
    inputs = []

    def bound(path, expected=None):
        row = record(path)
        if expected and row['sha256'] != expected:
            raise ValueError('Qualified input changed: '+str(path))
        inputs.append(row)
        return row

    bound(evidence/'CPP_POST_PACKAGE_PUBLICATION_BASELINE.json',
          'a13c1b52f3bd3ed094c93a4502bdab751a02dd391ffe2e76880a7335be0d1b41')
    bound(evidence/'LOCAL_STRING_POST_PUBLICATION_BASELINE.json',
          'd9227a37138a55ca771f9d833f42ad06a334681f496941eb44ea57310b14f11e')
    reports = {}
    for name, digest in [('CONFIGURE_AUDIT.json', 'ad0e28ade84ecbd8a6ee39c49bd4cd85dd7c548286e2ef9214ae8f7c776dacd8'),
                         ('BUILD_AUDIT.json', '59c8d2cd31d6b457b596427261b541a32483b3c18b7ae185d1cf48210b171202')]:
        path = evidence/'rosbag-cpp-fix3-test'/name
        bound(path, digest)
        reports[name] = json.loads(path.read_text(encoding='utf-8'))
        if reports[name]['status'] != 'PASS':
            raise ValueError('Original package binding not PASS')
    config, build = reports['CONFIGURE_AUDIT.json'], reports['BUILD_AUDIT.json']
    compiler, linker, readelf = (sdk/'llvm/bin'/x for x in ('clang++.exe', 'ld.lld.exe', 'llvm-readelf.exe'))
    if Path(config['ninja']['compile'][0]['compiler_argv'][0]).resolve() != compiler:
        raise ValueError('SDK differs qualified compiler binding')
    for path in (source, Path(__file__), packager, entry_helper, Path(sys.executable), compiler, linker, readelf):
        bound(path)
    cpp = repo/'build/r19cpp1/install/rosbag2_cpp'
    storage = repo/'build/r19storage3/install/rosbag2_storage'
    sqlite = repo/'build/r19sqlite2/install/rosbag2_storage_sqlite3'
    ros = repo/'build/ros2-target/r2/ros/install'
    includes = [cpp/'include/rosbag2_cpp'] + [Path(x) for x in config['ninja']['compile'][0]['ordered_include_roots'][1:]]
    includes.append(ros/'include/std_msgs')
    if any(not p.is_dir() for p in includes):
        raise ValueError('Missing declared include root')
    # Preserve ordered original link inputs; only package DSO + typed consumer support are new roots.
    libraries = shlex.split(config['ninja']['link']['variables']['LINK_LIBRARIES'])
    if any(x.startswith('-') and x not in ('-lrt', '-lpthread', '-ldl', '-lm') for x in libraries):
        raise ValueError('Unexpected original link option')
    link_files = [Path(x) for x in libraries if not x.startswith('-')]
    for row in build['link_archive']['inputs'].values():
        path = Path(row['path'])
        if path in link_files:
            bound(path, row['sha256'])
    if any(not any(Path(r['path']) == p for r in inputs) for p in link_files):
        raise ValueError('Original link library missing qualified binding')
    cpp_dso = cpp/'lib/librosbag2_cpp.so'
    bound(cpp_dso, '63d04394abffea6176b7a2b7f88dea7cd8fed578d06b30f458bccf0ab92e27c4')
    typed = ros/'lib/libstd_msgs__rosidl_typesupport_cpp.so'
    dynamic = [sqlite/'lib/librosbag2_storage_sqlite3.so', ros/'lib/libstd_msgs__rosidl_typesupport_fastrtps_cpp.so']
    for path in [typed, *dynamic]:
        bound(path)
    # Finite library directories: original bound link parents plus installed plugin/vendor dependencies.
    vendor = evidence/'vendor-wrapper-execution/install'
    libdirs = sorted({p.parent for p in link_files} | {cpp/'lib', sqlite/'lib', ros/'lib',
                     vendor/'opt/sqlite3_vendor/lib'}, key=str)
    catalog = []
    for directory in libdirs:
        if not directory.is_dir():
            raise ValueError('Missing declared DSO directory: '+str(directory))
        for path in sorted(directory.iterdir()):
            if path.is_file() and re.fullmatch(r'.+\.so(?:\.[0-9.]+)?', path.name):
                bound(path)
                catalog.append(path)
    shared_cxx = sdk/'llvm/lib/aarch64-linux-ohos/libc++_shared.so'
    bound(shared_cxx)
    catalog.append(shared_cxx)
    # rcl_yaml_param_parser's original DT_NEEDED includes this C YAML vendor.
    # It is outside ros/lib; select the exact qualified installed DSO, not a
    # broad vendor directory or the unrelated C++ yaml-cpp library.
    yaml = ros/'opt/libyaml_vendor/lib/libyaml.so'
    yaml_binding = bound(yaml, '83864723a3698505c5f808cf43ccda73cdc78dca5bc6d4061e2fb8f00197c07d')
    catalog.append(yaml)
    yaml_provenance = [ros.parent/'libyaml_vendor-install-sync-1.json',
                       repo/'build/ros2-target/r2/src/ros_libyaml_vendor.source.json',
                       ros.parent/'build-rcl_yaml_param_parser/build.ninja']
    yaml_provenance += [ros.parent/name for name in (
        '079-libyaml_vendor-elf-configure.json', '080-libyaml_vendor-elf-build.json',
        '081-libyaml_vendor-elf-install.json', '082-libyaml_vendor-install.json')]
    for path in yaml_provenance:
        bound(path)
    spdlog = ros/'opt/spdlog_vendor/lib/libspdlog.so.1.12.0'
    spdlog_binding = bound(spdlog, 'd87486f2a602b1ffe277e2219a43efc16cfd351064416a7c8cf894b0507e3bb2')
    catalog.append(spdlog)
    spdlog_provenance = [ros.parent/'spdlog_vendor-install-sync-1.json',
                         ros.parent/'spdlog_vendor-source.json',
                         ros.parent/'build-rcl_logging_spdlog/build.ninja']
    spdlog_provenance += [ros.parent/name for name in (
        '099-spdlog_vendor-elf-configure.json', '100-spdlog_vendor-elf-build.json',
        '101-spdlog_vendor-elf-install.json', '102-spdlog_vendor-install.json')]
    for path in spdlog_provenance:
        bound(path)
    resources = []

    def resource(prefix, relative):
        relative = PurePosixPath(relative)
        if relative.is_absolute() or '..' in relative.parts or ':' in str(relative) or '\\' in str(relative):
            raise ValueError('Unsafe original resource path')
        path = prefix/Path(str(relative))
        bound(path, INDEXES[str(relative)][0] if str(relative) in INDEXES else None)
        resources.append((path, str(relative)))

    for prefix, name in [(cpp, 'rosbag2_cpp'), (storage, 'rosbag2_storage'),
                         (sqlite, 'rosbag2_storage_sqlite3'), (ros, 'std_msgs')]:
        resource(prefix, 'share/ament_index/resource_index/packages/'+name)
        resource(prefix, 'share/'+name+'/package.xml')
    plugin_index = 'share/ament_index/resource_index/rosbag2_storage__pluginlib__plugin/rosbag2_storage_sqlite3'
    plugin_bytes, _ = runtime_resource(plugin_index, (sqlite/plugin_index).read_bytes())
    if plugin_bytes != b'share/rosbag2_storage_sqlite3/plugin_description.xml\n':
        raise ValueError('Unexpected original SQLite plugin index')
    resource(sqlite, plugin_index)
    resource(sqlite, 'share/rosbag2_storage_sqlite3/plugin_description.xml')
    index = 'share/ament_index/resource_index/rosidl_interfaces/std_msgs'
    resource(ros, index)
    interface_bytes, _ = runtime_resource(index, (ros/index).read_bytes())
    entries = interface_bytes.decode('utf-8').splitlines()
    if not entries or len(set(entries)) != len(entries) or 'msg/String.msg' not in entries:
        raise ValueError('Inconsistent original std_msgs interface index')
    for entry in entries:
        if not entry.startswith('msg/') or not entry.endswith(('.msg', '.idl')):
            raise ValueError('Unexpected std_msgs interface resource')
        resource(ros, 'share/std_msgs/'+entry)
    definition = ros/'share/std_msgs/msg/String.msg'
    if b'string data' not in definition.read_bytes():
        raise ValueError('Real nonempty String definition required')
    output.mkdir(parents=True)
    (output/'temp').mkdir()
    env = {k: v for k, v in os.environ.items() if k in ('SystemRoot', 'WINDIR', 'COMSPEC', 'USERPROFILE')}
    env.update(PATH=str(sdk/'llvm/bin')+os.pathsep+str(Path(sys.executable).parent)+os.pathsep+str(Path(os.environ['SystemRoot'])/'System32'),
               TEMP=str(output/'temp'), TMP=str(output/'temp'), PYTHONDONTWRITEBYTECODE='1')
    save(output/'INPUTS.json', dict(scope='LOCAL_STRING_FIXTURE_BUILD_INPUTS', files=inputs,
         include_roots=[str(p) for p in includes], library_directories=[str(p) for p in libdirs],
         additional_vendor_libraries=[dict(file=yaml_binding, soname='libyaml.so',
             abi='ELF64 little-endian AArch64 DYN', needed=['libc.so'],
             required_by='librcl_yaml_param_parser.so', provenance=[str(p) for p in yaml_provenance]),
             dict(file=spdlog_binding, soname='libspdlog.so.1.12',
             abi='ELF64 little-endian AArch64 DYN', needed=['libc++_shared.so', 'libc.so'],
             required_by='librcl_logging_spdlog.so', provenance=[str(p) for p in spdlog_provenance])],
         graph_risk='UNKNOWN; unindexed packager; direct rebuild.py caller covered by legacy mock regression'))
    common = [compiler, '--target=aarch64-linux-ohos', '--gcc-toolchain='+str(sdk/'llvm'),
              '--sysroot='+str(sdk/'sysroot'), '-stdlib=libc++', '-D__MUSL__']
    common += [x for x in config['ninja']['compile'][0]['compiler_argv']
               if x.startswith('-D') and x != '-D__MUSL__'
               and not x.startswith(('-DROSBAG2_CPP_BUILDING_DLL', '-D_SRC_REINDEX_DIR_PATH', '-Drosbag2_cpp_EXPORTS'))]
    obj, exe = output/'main.o', output/'rosbag_string_persistence'
    compile_argv = common + ['-std=gnu++17', '-O2', '-fPIE', '-fstack-protector-strong', '-Wall', '-Wextra',
                            '-MD', '-MF', output/'main.o.d']
    for directory in includes:
        compile_argv += ['-isystem', directory]
    compile_argv += ['-c', source, '-o', obj]
    link_argv = common + ['--rtlib=compiler-rt', '-fuse-ld=lld', '--ld-path='+str(linker), '-pie',
                         '-Wl,--build-id=sha1', '-Wl,--no-undefined', '-Wl,-z,noexecstack',
                         '-Wl,--trace', '-Wl,--reproduce='+str(output/'link-inputs.tar'),
                         '-Wl,-Map='+str(output/'link.map'), obj, cpp_dso, typed] + libraries + ['-o', exe]
    save(output/'COMMANDS.json', dict(compile=[str(x) for x in compile_argv], link=[str(x) for x in link_argv]))
    run('compile', compile_argv, output, env, 120)
    # Keep every raw dependency occurrence (including legitimate /./ spellings).
    # Compiler depfile is retained verbatim; the receipt binds each selected header.
    deptext = (output/'main.o.d').read_text(encoding='utf-8').replace('\\\n', '')
    separator = deptext.find(': ')
    if separator < 0:
        raise ValueError('Unexpected compiler depfile syntax')
    tokens = re.findall(r'(?:\\ |[^\s])+', deptext[separator+2:])
    dependencies = []
    for token in tokens:
        path = Path(token.replace('\\ ', ' '))
        if not path.is_absolute():
            path = output/path
        if not any(path.resolve().is_relative_to(root.resolve()) for root in [source.parent, sdk, *includes]):
            raise ValueError('Unexpected compiler dependency: '+token)
        dependencies.append(dict(raw=token, selected=record(path)))
    if not dependencies or not any(Path(r['selected']['path']) == source for r in dependencies):
        raise ValueError('Compiler source missing from dependencies')
    save(output/'HEADER_INPUTS.json', dependencies)
    run('link', link_argv, output, env, 120)
    run('readelf', [readelf, '-hld', exe], output, env, 60)
    package_argv = [sys.executable, '-B', packager, '--readelf', readelf, '--main', exe,
                    '--consumer-only', '--output', output/'bundle/lib', '--external', 'libc.so']
    for path in dynamic:
        package_argv += ['--dynamic-root', path]
    for path in catalog:
        package_argv += ['--library', path]
    run('package', package_argv, output, env, 300)
    receipt = json.loads((output/'bundle/lib/PACKAGE_INPUTS.json').read_text(encoding='utf-8'))
    for member in receipt['members']:
        if any(word in member['name'].lower() for word in ('dsoftbus', 'token', 'softbus', 'accesstoken')):
            raise ValueError('Unexpected native/identity closure member: '+member['name'])
    copied = []
    for path, relative in resources:
        destination = output/'bundle'/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        original = path.read_bytes()
        derived, transformation = runtime_resource(relative, original)
        destination.write_bytes(derived)
        row = dict(source=record(path), destination=relative, copied=record(destination),
                   transformation=transformation)
        if relative in INDEXES:
            preserved = output/'resource-originals'/relative
            preserved.parent.mkdir(parents=True, exist_ok=True)
            preserved.write_bytes(original)
            row.update(preserved_original=record(preserved),
                       changed_crlf_count=original.count(b'\r\n'),
                       entries_and_order_unchanged=True, final_newline_presence_unchanged=True)
        elif original != derived:
            raise ValueError('Unexpected non-index resource transformation')
        copied.append(row)
    save(output/'RESOURCES.json', copied)
    entry_spec = importlib.util.spec_from_file_location('runtime_entry', entry_helper)
    entry_module = importlib.util.module_from_spec(entry_spec)
    entry_spec.loader.exec_module(entry_module)
    entry_module.install_entry(output/'bundle', [exe.name], [relative for _, relative in resources])
    # Recheck selected immutable inputs after all host tools. Headers and linker implicit
    # inputs remain evidenced losslessly by main.o.d and link-inputs.tar for tester inspection.
    for row in inputs:
        if record(row['path']) != row:
            raise ValueError('Input changed during build: '+row['path'])
    for row in dependencies:
        if record(row['selected']['path']) != row['selected']:
            raise ValueError('Compiler dependency changed after compile: '+row['raw'])
    save(output/'RUN_SPEC.json', dict(scope='PLANNED_LOCAL_PERSISTENCE_NOT_EXECUTED',
         device='3e01ff55454d202020104033bf453b00', device_label='A', required_preflight='Explicit target; fresh absent deployment directory and bag URI',
         remote_prefix_template='/data/local/tmp/r19_rosbag_local_<unique>',
         argv=['<prefix>/run', 'rosbag_string_persistence', '<prefix>/bag', '<prefix>/share/std_msgs/msg/String.msg'],
         environment={'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp'},
         entry_environment_policy='run replaces AMENT_PREFIX_PATH and LD_LIBRARY_PATH from its own physical prefix; all other variables inherited',
         entry_posix_mode='0755; preserve/set on fresh deployed project run file',
         entry_cwd_policy='caller cwd preserved; test from unrelated cwd',
         outer_timeout_seconds=40, fixture_timeout_seconds=30,
         expected_exit=0, expected_status='PASS', messages=4, no_deployment_or_runtime_performed=True))
    save(output/'BUNDLE_MANIFEST.json', [record(p) for p in sorted((output/'bundle').rglob('*')) if p.is_file()])
    print('BUILD_AND_BUNDLE_COMPLETE; target runtime NOT EXECUTED')


if __name__ == '__main__':
    main()
