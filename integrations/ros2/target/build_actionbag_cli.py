"""Build a separate offline pure CLI overlay; never edits the R24 runtime.

No target import, Node, daemon, device, bag or Action typesupport build occurs.
The source map describes a future private composition; it does not apply it.
"""
import argparse
import ast
import base64
import csv
from datetime import datetime, timezone
import hashlib
import importlib.metadata as metadata
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import runpy
import shutil
import sys
import xml.etree.ElementTree as XML
import zipfile

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]
CLI = ('ros2cli', 'ros2node', 'ros2topic', 'ros2action', 'ros2pkg', 'ros2interface')
SETUP_HASH = {'ros2cli': '45742b28c41824d3a1a1837d626a7dca8d761bd7d770fa2c169992215c8aa716',
    'ros2node': '818afee81ccebbab025f951a7047e7fb59c73c0bf4d7844e4f344db6b9147306',
    'ros2topic': 'ad81ecde7daf690f092e3629cfb2201d19990532e50f50b68af95c6912805106',
    'ros2action': 'a9b9d0395cf279a5517d71dd945f83664aab9582802029349e73cd84e7039521',
    'ros2pkg': 'df8688dbdc54fbe049cd5441ee70f07c23d705b4ea1d989675e179558b0628bb',
    'ros2interface': '2a4d8654981bafc230afc9813154b65e3f2215c2ccf3095e8f0121b5a0750d5f',
    'ament_copyright': 'c0ff22799b7490c3d6a9b980febd5b42ab5a94eec5dc129ae1db6fdcf06577a8'}
BASE_HASH = 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f'
PURE_BASE = {'argcomplete': ('argcomplete', '3.1.4'), 'packaging': ('packaging', '24.0'),
    'ament-index-python': ('ament_index_python', '1.8.4'), 'rosidl-runtime-py': ('rosidl_runtime_py', '0.13.2'),
    'rosidl-parser': ('rosidl_parser', '4.6.9'), 'lark': ('lark', '1.1.9')}
PURE_HOST = {'catkin-pkg': (('catkin_pkg',), '1.0.0'), 'empy': (('em',), '3.3.4'),
    'pyparsing': (('pyparsing',), '3.1.1'), 'docutils': (('docutils',), '0.20.1'),
    'python-dateutil': (('dateutil',), '2.8.2'), 'six': (('six',), '1.16.0')}


def actionbag_cli_inventory(directory):
    result = {}
    for path in sorted(directory.rglob('*')):
        if path.is_symlink():
            raise ValueError('Unselected symlink: ' + str(path))
        if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc':
            data = path.read_bytes()
            result[path.relative_to(directory).as_posix()] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
    return result


class ActionbagCliBuilder:
    def __init__(self, args):
        self.root, self.output = args.inputs_root.resolve(), args.output.resolve()
        self.base = REPO / 'build/r24-project-python/final-runtime/package'
        if self.output.exists() or not self.output.is_relative_to(REPO / 'build') or self.output == REPO / 'build' or self.output.is_relative_to(self.base):
            raise ValueError('A fresh owned W3 build subdirectory is required')
        self.output.mkdir(parents=True)
        (self.output / 'commands').mkdir()
        (self.output / 'temp').mkdir()
        self.overlay = self.output / 'overlay'
        (self.overlay / 'python').mkdir(parents=True)
        self.origins, self.distributions, self.protected = {}, {}, {}
        self.inputs = {str(Path(__file__)): hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        self.wheels = {}

    def save(self, name, value):
        (self.output / name).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    def protect(self, directory):
        before = actionbag_cli_inventory(directory)
        self.protected[str(directory)] = before
        return before

    def put(self, name, data, origin):
        # A POSIX archive member must have one faithful Windows file spelling.
        # Reject drive/ADS/control syntax before either path parser can normalize it.
        if not name or re.search(r'[:\x00-\x1f\x7f]', name):
            raise ValueError('Noncanonical overlay member: ' + name)
        relative = PurePosixPath(name)
        if relative.as_posix() != name or relative.is_absolute() or '..' in relative.parts or '\\' in name or any(part.endswith((' ', '.')) or PureWindowsPath(part).is_reserved() for part in relative.parts) or re.search(r'\.(?:so(?:\.[0-9]+)*|pyd|dll|exe|a|o|pyc)$', name, re.I) or data.startswith((b'\x7fELF', b'MZ')):
            raise ValueError('Non-pure or unsafe overlay member: ' + name)
        target = self.overlay.joinpath(*relative.parts)
        if not target.resolve().is_relative_to(self.overlay.resolve()):
            raise ValueError('Overlay member resolves outside the owned prefix: ' + name)
        if target.exists():
            raise ValueError('Overlay member collision: ' + name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        self.origins[name] = origin

    def collect(self):
        base_manifest = self.base / 'PACKAGE.json'
        if hashlib.sha256(base_manifest.read_bytes()).hexdigest() != BASE_HASH:
            raise ValueError('Canonical R24 manifest differs from selected reviewed input')
        self.base_rows = json.loads(base_manifest.read_text())['files']
        base_files = self.protect(self.base)
        for name, row in self.base_rows.items():
            if base_files.get(name, {}).get('sha256') != row['sha256']:
                raise ValueError('Canonical R24 payload changed: ' + name)
        self.inputs[str(base_manifest)] = BASE_HASH
        adapter_xml = Path(self.base_rows['python/rosidl_adapter/__init__.py']['source']).parents[1] / 'package.xml'
        if XML.fromstring(adapter_xml.read_bytes()).findtext('version') != '4.6.9':
            raise ValueError('Archived pure rosidl_adapter version changed')
        self.inputs[str(adapter_xml)] = hashlib.sha256(adapter_xml.read_bytes()).hexdigest()
        self.base_dist = {}
        for dist in metadata.distributions(path=[str(self.base / 'python')]):
            name = re.sub('[-_.]+', '-', dist.metadata['Name']).lower()
            if name in self.base_dist:
                raise ValueError('Duplicate baseline distribution: ' + name)
            self.base_dist[name] = dist
        for name in CLI:
            dist = self.base_dist.get(name)
            if name in ('ros2cli', 'ros2pkg', 'ros2interface'):
                if dist is None or dist.version != '0.32.11':
                    raise ValueError('Unexpected shadowed baseline CLI version: ' + name)
            elif dist is not None:
                raise ValueError('Unexpected pre-existing new CLI plugin: ' + name)
        source_root = self.root / 'ros2/src/ros2/ros2cli'
        self.sources = {name: source_root / name for name in CLI}
        self.sources['ament_copyright'] = self.root / 'ros2/src/ament/ament_lint/ament_copyright'
        self.source_versions = {}
        for name, source in self.sources.items():
            if hashlib.sha256((source / 'setup.py').read_bytes()).hexdigest() != SETUP_HASH[name]:
                raise ValueError('Selected offline setup program changed: ' + name)
            xml = XML.fromstring((source / 'package.xml').read_bytes())
            version = xml.findtext('version')
            if xml.findtext('name') != name or version != ('0.17.5' if name == 'ament_copyright' else '0.32.12'):
                raise ValueError('Source package/version mismatch')
            self.source_versions[name] = version
            self.protect(source)
        for name, (modules, version) in PURE_HOST.items():
            dist = metadata.distribution(name)
            if dist.version != version:
                raise ValueError('Host pure input version conflict: ' + name)
            self.protect(Path(dist._path))
            for module in modules:
                path = Path(dist.locate_file(module))
                if path.is_dir():
                    self.protect(path)
                else:
                    path = path.with_suffix('.py')
                    self.inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        selection = self.root / 'build/ros2-target/r2/BUILD_SELECTION.json'
        selected = json.loads(selection.read_text())
        self.python = Path(selected['python'])
        if Path(sys.executable).resolve() != self.python.resolve() or hashlib.sha256(self.python.read_bytes()).hexdigest() != selected['tool_sha256'][str(self.python)]:
            raise ValueError('Use the pinned local generator Python')
        self.inputs[str(selection)] = hashlib.sha256(selection.read_bytes()).hexdigest()
        self.inputs[str(self.python)] = selected['tool_sha256'][str(self.python)]
        for name, version in [('setuptools', '68.1.2'), ('wheel', '0.46.3'), ('packaging', '24.0')]:
            dist = metadata.distribution(name)
            if dist.version != version:
                raise ValueError('Local wheel tooling version changed: ' + name)
            self.protect(Path(dist._path))
            self.protect(Path(dist.locate_file(name)))
        setuptools = metadata.distribution('setuptools')
        for name in ('pkg_resources', '_distutils_hack'):
            self.protect(Path(setuptools.locate_file(name)))
        pth = Path(setuptools.locate_file('distutils-precedence.pth'))
        self.inputs[str(pth)] = hashlib.sha256(pth.read_bytes()).hexdigest()
        recorder = REPO / 'integrations/ros2/target/project_python.py'
        if hashlib.sha256(recorder.read_bytes()).hexdigest() != 'f4c0891a2ec77e05ae71e170c4ec0e0e5f29c3c8e272025d1a8006dc1b30cbd8':
            raise ValueError('Qualified owned-process build recorder changed')
        self.inputs[str(recorder)] = hashlib.sha256(recorder.read_bytes()).hexdigest()
        self.record = runpy.run_path(str(recorder), run_name='cli_owned_build_recorder')['project_python_record']
        self.save('INPUTS.json', {'sha256': self.inputs, 'protected_trees': self.protected, 'source_versions': self.source_versions,
            'source_metadata_dependencies': {name: [x.text for x in XML.fromstring((path / 'package.xml').read_bytes()).findall('exec_depend')] for name, path in self.sources.items()}})

    def wheel(self, name, source):
        snapshot = self.output / 'src' / name
        snapshot.mkdir(parents=True)
        for relative in self.protected[str(source)]:
            path = snapshot / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, path)
        environment = {k: os.environ[k] for k in ('SYSTEMROOT', 'WINDIR', 'COMSPEC', 'PATHEXT', 'LOCALAPPDATA', 'APPDATA', 'USERPROFILE') if k in os.environ}
        environment.update(PATH=os.pathsep.join([str(self.python.parent), str(self.python.parent / 'Library/bin'), os.environ['SYSTEMROOT'] + '/System32']),
            TEMP=str(self.output / 'temp'), TMP=str(self.output / 'temp'), PIP_NO_INDEX='1', PIP_DISABLE_PIP_VERSION_CHECK='1', PIP_CONFIG_FILE=os.devnull,
            PYTHONDONTWRITEBYTECODE='1', SETUPTOOLS_USE_DISTUTILS='local')
        script = 'import os,runpy,sys; os.chdir(sys.argv[1]); sys.argv=["setup.py","bdist_wheel","--dist-dir",sys.argv[2],"--bdist-dir",sys.argv[3]]; runpy.run_path("setup.py",run_name="__main__")'
        wheel_dir = self.output / 'wheels' / name
        wheel_dir.mkdir(parents=True)
        self.record(self.output, 'wheel-' + name, [self.python, '-I', '-B', '-c', script, snapshot, wheel_dir, self.output / ('bdist-' + name)], environment, timeout_seconds=120)
        wheels = list(wheel_dir.glob('*.whl'))
        if len(wheels) != 1:
            raise ValueError('Exactly one wheel required')
        wheel = wheels[0]
        self.wheels[name] = {'path': str(wheel), 'sha256': hashlib.sha256(wheel.read_bytes()).hexdigest()}
        installed, seen = [], set()
        with zipfile.ZipFile(wheel) as archive:
            wheel_metadata = [n for n in archive.namelist() if n.endswith('.dist-info/WHEEL')]
            if len(wheel_metadata) != 1:
                raise ValueError('One real wheel metadata record required')
            text = archive.read(wheel_metadata[0]).decode()
            if 'Root-Is-Purelib: true\n' not in text or 'Tag: py3-none-any\n' not in text:
                raise ValueError('Only architecture-neutral pure Python wheels are selected')
            for info in archive.infolist():
                if info.filename in seen or ((info.external_attr >> 16) & 0o170000) == 0o120000:
                    raise ValueError('Wheel duplicate/symlink')
                seen.add(info.filename)
                if info.is_dir():
                    continue
                parts = PurePosixPath(info.filename).parts
                if not parts or PurePosixPath(info.filename).is_absolute() or '..' in parts or '\\' in info.filename:
                    raise ValueError('Unsafe wheel path')
                if info.filename.endswith('.dist-info/RECORD'):
                    continue
                if parts[0].endswith('.data'):
                    if len(parts) < 4 or parts[1:3] != ('data', 'share'):
                        raise ValueError('Unexpected wheel data category')
                    destination = '/'.join(parts[2:])
                else:
                    destination = 'python/' + info.filename
                self.put(destination, archive.read(info), {'kind': 'OFFLINE_SOURCE_WHEEL', 'wheel': str(wheel), 'wheel_member': info.filename})
                installed.append(destination)
        self.distributions[name] = installed

    def copy_pure(self, name, modules, version, base):
        dist = self.base_dist[name] if base else metadata.distribution(name)
        if dist.version != version:
            raise ValueError('Pure dependency version conflict')
        directory = Path(dist._path)
        installed = []
        paths = list(directory.rglob('*'))
        for module in modules:
            path = directory.parent / module
            paths.extend(path.rglob('*') if path.is_dir() else [path.with_suffix('.py')])
        for path in sorted(set(paths)):
            if not path.is_file() or '__pycache__' in path.parts or path.suffix == '.pyc' or path.name == 'RECORD':
                continue
            if path.is_symlink():
                raise ValueError('Pure input symlink')
            destination = 'python/' + path.relative_to(directory.parent).as_posix()
            if base and self.base_rows.get(destination, {}).get('sha256') != hashlib.sha256(path.read_bytes()).hexdigest():
                raise ValueError('Dependency absent from bound R24 inventory')
            self.put(destination, path.read_bytes(), {'kind': 'R24_PURE_DEPENDENCY' if base else 'ARCHITECTURE_NEUTRAL_HOST_PURE_INPUT', 'source': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
            installed.append(destination)
        self.distributions[name] = installed

    def finish(self):
        records = list(metadata.distributions(path=[str(self.overlay / 'python')]))
        versions, entries, declared, matrix = {}, {}, {}, {}
        for dist in records:
            name = re.sub('[-_.]+', '-', dist.metadata['Name']).lower()
            if name in versions:
                raise ValueError('Duplicate overlay distribution')
            versions[name] = dist.version
            entries[name] = [{'group': e.group, 'name': e.name, 'value': e.value} for e in dist.entry_points]
            declared[name] = dist.requires or []
            key = next(k for k in self.distributions if re.sub('[-_.]+', '-', k).lower() == name)
            imports = set()
            for member in self.distributions[key]:
                if not member.endswith('.py'):
                    continue
                for node in ast.walk(ast.parse((self.overlay / member).read_text(encoding='utf-8-sig'), filename=member)):
                    if isinstance(node, ast.Import):
                        imports.update(alias.name.split('.')[0] for alias in node.names)
                    elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                        imports.add(node.module.split('.')[0])
            matrix[name] = {'version': dist.version, 'origin_kinds': sorted({self.origins[m]['kind'] for m in self.distributions[key]}),
                'source': str(self.sources[key]) if key in self.sources else str(self.base / 'python') if key in PURE_BASE else str(metadata.distribution(key)._path),
                'static_imports_nonstdlib_all_branches': sorted(imports - sys.stdlib_module_names), 'metadata_requires_dist': dist.requires or [],
                'ros_exec_dependencies': [e.text for e in XML.fromstring((self.sources[key] / 'package.xml').read_bytes()).findall('exec_depend')] if key in self.sources else [],
                'runtime_status': 'UNVERIFIED_NO_TARGET_IMPORT', 'interpretation': 'Static actual source imports include TYPE_CHECKING, platform/older-Python fallback and optional extras; not evidence every branch is required or executed'}
            record_name = Path(dist._path).relative_to(self.overlay).as_posix() + '/RECORD'
            if Path(dist._path).name.endswith('.dist-info'):
                text = io.StringIO(newline='')
                writer = csv.writer(text, lineterminator='\n')
                for member in sorted(self.distributions[key]):
                    data = (self.overlay / member).read_bytes()
                    writer.writerow([os.path.relpath(self.overlay / member, self.overlay / 'python').replace('\\', '/'), 'sha256=' + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).decode().rstrip('='), len(data)])
                writer.writerow([record_name.removeprefix('python/'), '', ''])
                self.put(record_name, text.getvalue().encode(), {'kind': 'GENERATED_INSTALLED_RECORD', 'reason': 'Actual relocated selected files; original wheel/base RECORD retained as input, never falsely describes omitted scripts or wheel .data paths'})
        if any(versions.get(name) != '0.32.12' for name in CLI):
            raise ValueError('CLI versions must be uniform')
        for name, source in self.sources.items():
            resource = self.overlay / 'share/ament_index/resource_index/packages' / name
            package_xml = self.overlay / 'share' / name / 'package.xml'
            if not resource.is_file() or package_xml.read_bytes() != (source / 'package.xml').read_bytes() or resource.read_bytes() != (source / 'resource' / name).read_bytes():
                raise ValueError('Actual package XML/ament resource missing or changed: ' + name)
        commands = [e['name'] for values in entries.values() for e in values if e['group'] == 'ros2cli.command']
        if len(commands) != len(set(commands)) or not {'node', 'topic', 'action', 'pkg', 'interface'} <= set(commands):
            raise ValueError('CLI command entrypoints missing/conflicting')
        replacements, overlaps = {}, {}
        old_metadata = {Path(self.base_dist[n]._path).name for n in ('ros2cli', 'ros2pkg', 'ros2interface')}
        for name, row in self.base_rows.items():
            parts = PurePosixPath(name).parts
            if len(parts) > 1 and parts[0] == 'python' and (parts[1] in CLI or parts[1] in old_metadata):
                replacements[name] = row['sha256']
        for name, row in actionbag_cli_inventory(self.overlay).items():
            if name in self.base_rows:
                previous = self.base_rows[name]['sha256']
                if previous != row['sha256'] and name not in replacements and self.origins[name]['kind'] != 'GENERATED_INSTALLED_RECORD':
                    raise ValueError('Unapproved base collision: ' + name)
                overlaps[name] = {'base_sha256': previous, 'overlay_sha256': row['sha256'], 'kind': 'CLI_VERSION_REPLACEMENT' if name in replacements else 'REGENERATED_RECORD' if previous != row['sha256'] else 'IDENTICAL_PURE_DEPENDENCY'}
        external = {n: self.base_dist[n].version for n in ('rclpy', 'numpy', 'psutil', 'pyyaml', 'rpyutils')}
        from packaging.requirements import Requirement
        from packaging.markers import default_environment
        marker_environment = {**default_environment(), 'sys_platform': 'linux', 'os_name': 'posix', 'platform_machine': 'aarch64',
            'platform_system': 'Linux', 'python_version': '3.12', 'python_full_version': '3.12.7', 'extra': ''}
        missing = {}
        for name, requirements in declared.items():
            for text in requirements:
                requirement = Requirement(text)
                if requirement.marker and not requirement.marker.evaluate(marker_environment):
                    continue
                dependency = re.sub('[-_.]+', '-', requirement.name).lower()
                selected = versions.get(dependency, external.get(dependency))
                if selected is None:
                    missing.setdefault(name, []).append(text)
                elif requirement.specifier and selected not in requirement.specifier:
                    raise ValueError('Declared dependency version conflict: ' + name + ' -> ' + text + ' selected ' + selected)
        self.save('COMPOSITION.json', {'mode': 'PLAN_ONLY_NO_BASE_MUTATION', 'base_manifest_sha256': BASE_HASH, 'remove_from_future_private_copy_only': replacements,
            'overlaps': overlaps, 'external_base_distributions': external, 'external_generated_modules': ['action_msgs', 'std_msgs', 'builtin_interfaces', 'rcl_interfaces'],
            'required_next': 'New combined package must privately copy exact R24, remove these exact old CLI members/metadata, apply source map and create a new full manifest; this builder never performs those writes'})
        self.save('DEPENDENCIES.json', {'versions': versions, 'entry_points': entries, 'package_matrix': matrix, 'metadata_requires_dist': declared, 'missing_declared_distributions': missing,
            'rosidl_adapter': {'version': '4.6.9', 'origin': 'Exact archived-project module already selected in R24; copied unchanged with R24 per-file source map',
                'metadata': 'R24 supplied no distribution metadata; none fabricated', 'actual_import': 'ros2interface queries use rosidl_adapter.parser', 'runtime_status': 'UNVERIFIED_NO_TARGET_IMPORT'},
            'actual_import_scope': ['Six stock CLI packages and their ordinary plugin imports; ROS target modules not imported here', 'rosidl_runtime_py -> rosidl_parser -> lark; argcomplete, packaging, ament_index_python', 'ros2pkg.create -> ament_copyright, catkin_pkg, em, pyparsing'],
            'unverified_declared_dependencies': {'setuptools': 'Declared by catkin-pkg/ament_copyright; no target setuptools selected. Host executable launcher assets are not copied.', 'ament_lint': 'ament_copyright package.xml declares it; stock selected runtime module does not import it. Full lint command qualification excluded.'},
            'optional_not_selected': ['typing_extensions type-checking/older-Python fallback', 'Lark optional regex/interegular/rich/pydot/js2py/PyInstaller/atomicwrites extras', 'importlib_metadata/importlib_resources backports: Python3.12 stdlib routes selected'],
            'upstream_metadata_limit': 'Selected installed empy3.3.4 RECORD contains only em.py plus metadata. Its upstream top_level.txt additionally names absent emlib; this is preserved, not fabricated. Selected stock ROS code imports em only.',
            'status': 'STATIC_PACKAGING_ONLY_ALL_RUNTIME_AND_FULL_PKG_CREATE_UNVERIFIED'})
        self.save('OVERLAY.json', {'scope': 'PURE_CLI_OVERLAY_NOT_COMBINED_RUNTIME_OR_COMMUNICATION', 'files': actionbag_cli_inventory(self.overlay), 'source_map': self.origins,
            'wheels': self.wheels, 'versions': versions, 'entry_points': entries, 'native_files': 0, 'base_modified': False, 'device_calls': 0,
            'blockers': ['Launcher repair/runtime gate belongs to Main', 'Action example_interfaces Python typesupport absent; action execution not enabled', 'No rosbag2_py or ros2bag built', 'No Node/daemon/communication qualified', 'Full pkg-create/lint dependency metadata not closed'],
            'daemon_policy': 'Stock daemon entrypoints preserved; future communicating graph invocations must explicitly use --no-daemon; this build executes no CLI'})

    def build(self):
        try:
            self.collect()
            for name, source in self.sources.items():
                self.wheel(name, source)
            for name, (module, version) in PURE_BASE.items():
                self.copy_pure(name, (module,), version, True)
            for name, row in self.base_rows.items():
                if name.startswith('python/rosidl_adapter/'):
                    self.put(name, (self.base / name).read_bytes(), {'kind': 'R24_ARCHIVED_PROJECT_PURE_MODULE_NO_FABRICATED_METADATA', 'source': str(self.base / name), 'sha256': row['sha256']})
            for name, (modules, version) in PURE_HOST.items():
                self.copy_pure(name, modules, version, False)
            self.finish()
        finally:
            changed = [path for path, before in self.protected.items() if actionbag_cli_inventory(Path(path)) != before]
            changed += [path for path, before in self.inputs.items() if hashlib.sha256(Path(path).read_bytes()).hexdigest() != before]
            self.save('NO_MUTATION.json', {'status': 'PASS' if not changed else 'FAIL', 'changed_inputs': changed, 'scope': 'Selected original source, R24, host pure inputs and tooling; snapshot/build output intentionally writable'})
            if changed:
                raise RuntimeError('Protected selected input changed; no automatic restoration')
        self.save('BUILD_RESULT.json', {'status': 'BUILDS_PURE_OVERLAY_ONLY', 'utc': datetime.now(timezone.utc).isoformat(), 'overlay': str(self.overlay),
            'manifest_sha256': hashlib.sha256((self.output / 'OVERLAY.json').read_bytes()).hexdigest(), 'device_tested': False, 'target_imported': False,
            'base_modified': False, 'versions': self.source_versions})


def actionbag_cli_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    ActionbagCliBuilder(args).build()


if __name__ == '__main__':
    actionbag_cli_main()
