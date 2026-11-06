#!/usr/bin/env python3
"""Add the pure Python half of ROS 2 to a build made with build_ros2_ohos.py --python-target.

    python scripts/release/build_ros2_python.py --work E:/s2

Each package is built into a wheel from its pinned source, unmodified, and unpacked into <work>/ros/install as colcon would
place it. Run after the native build, because the entry scripts go next to native executables in lib/<package>.
"""
import argparse
import configparser
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SITE = 'lib/python3.12/site-packages'
# Repository path under ros2/src, pinned revision, package directories.
SOURCES = [
    ('ros2/ros2cli', '01b564e37eb0c81787601b218f8eb7bce2dba5ee',
     'ros2cli ros2action ros2component ros2doctor ros2interface ros2lifecycle ros2multicast ros2node ros2param ros2pkg ros2run '
     'ros2service ros2topic'.split()),
    ('ros2/launch', 'b63c1b82687038dad9a5d914c631f6681a550abe', 'launch launch_xml launch_yaml'.split()),
    ('ros2/launch_ros', '97b758478d8bf343a1e83ab419210d790da53d80', 'launch_ros ros2launch'.split()),
    ('ros2/rpyutils', '973dbcc0a0a18299d840d089e47372d569264c31', ['.']),
    ('osrf/osrf_pycommon', '7dc6da97b4765af5db43fa475ddee2c3e5999fbd', ['.']),
    ('ament/ament_index', '2ccdcb8fd19a364fc793273b5793459207a440b9', ['ament_index_python']),
    ('ros2/rosidl_runtime_py', '2db2ae52325e0104b5c70ac7f6ff5cf3e77471e2', ['.']),
    ('ros2/rosbag2', '2999680798a94c2cc5d582bcb5227489cd189e82', ['ros2bag']),
    ('ros2/demos', '37e25e0dece32f972b3cc8677b6aab3e891a9365', ['demo_nodes_py']),
    ('ament/ament_lint', '9121ac787058642730206a52daba614f9c86b0fb', ['ament_copyright']),
    ('ros2/sros2', '80a08ffa1bccd2de602c2acd120b209ad5f8ce83', ['sros2']),
]
# Python packages that ament_cmake installs, so they have no wheel: repository, revision, package directories.
COPIED = [('ros2/rosidl', '85fa592b698b0f665e3120f48fac0d35e2f7d8a4', ['rosidl_parser/rosidl_parser', 'rosidl_adapter/rosidl_adapter'])]
# OpenHarmony has no /usr/bin/env.
SCRIPT = """#!/bin/env python3.12
import sys
from {module} import {attribute}
sys.exit({attribute}())
"""


# Python loads every process's extension modules privately by default. Plugins loaded by class_loader must then share C++
# typeinfo with those modules, or rosbag2 finds no storage plugin. Python loads this file by itself at startup.
SITE_CUSTOMIZE = """import os
import sys
sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_GLOBAL)
"""


def export_source(repository, commit, scratch):
    """A clean export of the pinned commit, so the checkout is read and never written."""
    source = REPO / 'ros2/src' / repository
    export = scratch / repository.replace('/', '_')
    if not export.exists():
        export.mkdir(parents=True)
        tar = scratch / 'export.tar'
        subprocess.check_call(['git', '-c', 'core.autocrlf=false', '-C', str(source), 'archive', '--format=tar', '--output=' + str(tar), commit])
        with tarfile.open(tar) as handle:
            handle.extractall(export, filter='data')
        tar.unlink()
    return export


def build_wheel(python, repository, commit, folder, wheels, scratch):
    export = export_source(repository, commit, scratch)
    subprocess.check_call([python, '-m', 'pip', 'wheel', '--quiet', '--no-deps', '--no-build-isolation', '--no-cache-dir', '-w', str(wheels), str(export / folder)])


def unpack(wheel, install):
    site = install / SITE
    site.mkdir(parents=True, exist_ok=True)
    package = None
    with zipfile.ZipFile(wheel) as archive:
        for entry in archive.namelist():
            if entry.endswith('/'):
                continue
            head, _, rest = entry.partition('/')
            if head.endswith('.data'):
                kind, _, relative = rest.partition('/')
                if kind != 'data':
                    continue  # setuptools' own scripts directory; console scripts are written below
                target = install / relative
            else:
                target = site / entry
            target.parent.mkdir(parents=True, exist_ok=True)
            data = archive.read(entry)
            # ament resource files and package.xml may carry CRLF from a Windows checkout setting.
            target.write_bytes(data.replace(b'\r\n', b'\n') if '/ament_index/' in entry else data)
            if head.endswith('.dist-info'):
                package = head
        points = [n for n in archive.namelist() if n.endswith('.dist-info/entry_points.txt')]
        if points:
            config = configparser.ConfigParser(interpolation=None, delimiters=('=',))
            config.optionxform = str
            config.read_string(archive.read(points[0]).decode())
            if config.has_section('console_scripts'):
                name = package.split('-')[0]
                for script, target in config.items('console_scripts'):
                    module, attribute = target.strip().split(':')
                    # setup.cfg sends ROS scripts to lib/<package>; ros2cli has none, so ros2 goes to bin as colcon puts it.
                    path = install / 'bin' / script if name == 'ros2cli' else install / 'lib' / name / script
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(SCRIPT.format(module=module, attribute=attribute), newline='\n')
                    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True, type=Path)
    ap.add_argument('--python', default='C:/pixi_ws/.pixi/envs/default/python.exe')
    a = ap.parse_args()
    install = a.work / 'ros/install'
    if not (install / SITE / 'rclpy').exists():
        sys.exit('no rclpy in %s: run build_ros2_ohos.py --python-target first' % install)
    with tempfile.TemporaryDirectory(dir=a.work) as folder:
        scratch = Path(folder)
        wheels = scratch / 'wheels'
        wheels.mkdir()
        for repository, commit, packages in SOURCES:
            for package in packages:
                build_wheel(a.python, repository, commit, package, wheels, scratch)
        for repository, commit, folders in COPIED:
            export = export_source(repository, commit, scratch)
            for folder in folders:
                target = install / SITE / Path(folder).name
                shutil.rmtree(target, ignore_errors=True)
                shutil.copytree(export / folder, target, ignore=shutil.ignore_patterns('__pycache__', 'test'))
        built = sorted(wheels.glob('*.whl'))
        for wheel in built:
            unpack(wheel, install)
    (install / SITE / 'sitecustomize.py').write_bytes(SITE_CUSTOMIZE.encode())
    print('installed %d Python packages into %s' % (len(built), install))


if __name__ == '__main__':
    sys.exit(main())
