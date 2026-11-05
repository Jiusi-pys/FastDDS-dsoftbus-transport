#!/usr/bin/env python3
"""Build unmodified ROS 2 Jazzy for OpenHarmony against the Fast DDS 2.14.5-ros prefix.

    python scripts/release/build_ros2_ohos.py --work E:/s2

Sources are archived from ros2/src at fixed revisions into <work>/ros-src, so the checkout is only
read and no patch is applied. Output: <work>/ros/install.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get('OHOS_NATIVE', Path.home() / 'AppData/Local/OpenHarmony/Sdk/23/native'))
# Every repository is pinned: rmw_fastrtps, rcl and rclcpp to the earlier device-accepted build, the
# rest to the revisions this build was validated with.
PINS = {
    'ament_index': '2ccdcb8fd19a364fc793273b5793459207a440b9',
    'class_loader': 'c404b82f04e6b0cce5c6205626b2d35fdf4dc882',
    'common_interfaces': 'a941f14bb318d8d904505ed935ccbb97f24a70a4',
    'console_bridge_vendor': 'f25fd1103fcd839e796822892e267559f5e336a7',
    'demos': '37e25e0dece32f972b3cc8677b6aab3e891a9365',
    'example_interfaces': '4ed406d1277df78be01a31005306d69e7592a993',
    'examples': '07008852303f2a35a91c65d78046b274a35477ea',
    'libstatistics_collector': '150e1b0e1fbf920a97e4319f98eaf02be27fa892',
    'libyaml_vendor': '57b7deca1fa34ea95a6456c0cc08070c4ab389c2',
    'pluginlib': 'd09dc3eb672b6b4ea3a7c8f2c1189e08bbbfa1e1',
    'rcl': '22c0b957140035c675f0a061daaab5fd6b2080e7',
    'rcl_interfaces': '7aa3caf43377ea6ad615bc1040832e2c7566bfbe',
    'rcl_logging': '727920c2592be6deb6983f0fe1b57cbc929cbb70',
    'rclcpp': '3aa906a2c7ad13d1623b31a55731993f56538e72',
    'rcpputils': '67590324311ce4c60fef774205d55c93fe3cf1b6',
    'rcutils': '04a453a1739d4301da636229c51a9b5eca6a4d23',
    'rmw': '8cb94a87cf3f4049abe2d563af893517bf336930',
    'rmw_dds_common': '2e3ebf31850a8b05430fb44230ead2d566b8ff1d',
    'rmw_fastrtps': 'bbbb90a98b65f1f678945ef69273edf8b09200ae',
    'rmw_implementation': '835ff87c676cd65634edd3ad7ba88c8d8a0452e7',
    'ros2_tracing': '615d3dc1ea9ce39784f36ab8bce3b362cd530a07',
    'ros_environment': '700bc3fe477cbe64cf50863968a4fbee2833ecff',
    'rosidl': '85fa592b698b0f665e3120f48fac0d35e2f7d8a4',
    'rosidl_core': 'c6eb22b0e77cf57b19e9c6ffd114a00749404215',
    'rosidl_defaults': '9749b5919add7e5960cd931ec547d45f06bd0025',
    'rosidl_dynamic_typesupport': 'cb8c54d12c678daa70410bb0626ed8626f561e45',
    'rosidl_dynamic_typesupport_fastrtps': 'a88d2517f19dd219e81828688324c80dfa1fd096',
    'rosidl_typesupport': '9fcbb58b797b35a69dcc8ba111049a08eaa4aba1',
    'rosidl_typesupport_fastrtps': 'b883555055cce17982a22172a42ef557ef40bac5',
    'spdlog_vendor': 'cd399009a655b88a16b2a2c3a9cd0c9f64140d38',
    'unique_identifier_msgs': '901599ee92c2ee949f42a5c5d821c92b3e515d69',
}
REPOS = {
    'ros2': ('common_interfaces demos example_interfaces libyaml_vendor rcl rcl_interfaces rcl_logging rclcpp '
             'rcpputils rcutils rmw rmw_dds_common rmw_fastrtps rmw_implementation ros2_tracing rosidl rosidl_core '
             'rosidl_defaults rosidl_dynamic_typesupport rosidl_dynamic_typesupport_fastrtps rosidl_typesupport '
             'rosidl_typesupport_fastrtps spdlog_vendor unique_identifier_msgs console_bridge_vendor examples').split(),
    'ament': ['ament_index'],
    'ros-tooling': ['libstatistics_collector'],
    'ros': ['ros_environment', 'class_loader', 'pluginlib'],
}
# The Fast RTPS typesupport generators come first: message packages only generate for the typesupports
# already installed, and rmw_fastrtps_cpp needs that output. ament_index_cpp is explicit because the
# builder otherwise resolves every ament_* package from the Host tools, and this one is a runtime library.
PACKAGES = ('ament_index_cpp rosidl_typesupport_fastrtps_c rosidl_typesupport_fastrtps_cpp '
            'rosidl_typesupport_introspection_c rosidl_typesupport_introspection_cpp rosidl_typesupport_c '
            'rosidl_typesupport_cpp builtin_interfaces unique_identifier_msgs service_msgs action_msgs rcl_interfaces '
            'rmw_fastrtps_cpp rmw_implementation rcl rcl_action rcl_lifecycle rclcpp rclcpp_action rclcpp_lifecycle '
            'std_msgs example_interfaces lifecycle_msgs ros2_stage2 demo_nodes_cpp examples_rclcpp_minimal_publisher '
            'examples_rclcpp_minimal_subscriber').split()


def archive(group, name, out):
    source = REPO / 'ros2/src' / group / name
    destination = out / name
    record = out / (name + '.source.json')
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', PINS[name] + '^{commit}'], text=True).strip()
    if record.exists():
        if json.loads(record.read_text())['commit'] != commit:
            sys.exit('source revision changed: ' + name)
        return
    destination.mkdir(parents=True, exist_ok=False)
    tar = out / (name + '.tar')
    subprocess.check_call(['git', '-c', 'core.autocrlf=false', '-C', str(source), 'archive', '--format=tar',
                           '--output=' + str(tar), commit])
    with tarfile.open(tar) as handle:
        handle.extractall(destination, filter='data')
    tar.unlink()
    record.write_text(json.dumps({'repository': str(source), 'commit': commit}, indent=1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True, type=Path)
    ap.add_argument('--cmake', default='C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe')
    ap.add_argument('--python', default='C:/pixi_ws/.pixi/envs/default/python.exe')
    ap.add_argument('--ninja', default='C:/Users/17715/scoop/shims/ninja.exe')
    ap.add_argument('--host-tools', default='C:/pixi_ws/ros2-windows')
    ap.add_argument('--package', action='append', default=[], help='additional package (default set always built)')
    ap.add_argument('--rebuild-package', action='append', default=[], help='rebuild this already installed package')
    a = ap.parse_args()
    prefix = a.work / '2.14.5-ros/prefix'
    if not (prefix / 'lib/libfastrtps.so').exists():
        sys.exit('missing %s: run build_fastdds_ohos.sh 2.14.5-ros first' % prefix)
    source = a.work / 'ros-src'
    source.mkdir(parents=True, exist_ok=True)
    for group, names in REPOS.items():
        for name in names:
            archive(group, name, source)
    # The workload is built like any other ROS package. It is refreshed every run, so it is always rebuilt.
    workload = source / 'ros2_stage2'
    shutil.rmtree(workload, ignore_errors=True)
    workload.mkdir()
    for name in ('CMakeLists.txt', 'package.xml', 'stage2_node.cpp'):
        shutil.copyfile(REPO / 'tests/ros2_stage2' / name, workload / name)
    argv = [a.python, REPO / 'integrations/ros2/target/build_ros_slice.py', '--source', source, '--output', a.work / 'ros',
            '--target-prefix', prefix, '--dependency-prefix', prefix, '--host-tools', a.host_tools, '--sdk', SDK, '--cmake', a.cmake,
            '--python', a.python, '--ninja', a.ninja]
    for name in PACKAGES + a.package:
        argv += ['--package', name]
    # class_loader casts across the executable and its plugin. Without this flag the executable's typeinfo
    # stays out of .dynsym, the plugin keeps a second copy, and the OHOS libc++ cast returns null.
    argv.append('--exe-linker-flag=-Wl,--export-dynamic')
    for name in ['ros2_stage2'] + a.rebuild_package:
        argv += ['--rebuild-package', name]
    if (a.work / 'ros').exists():
        argv.append('--resume')
    return subprocess.call([str(x) for x in argv], cwd=REPO)


if __name__ == '__main__':
    sys.exit(main())
