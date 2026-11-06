#!/usr/bin/env python3
"""Build the native libraries rosbag2 and pluginlib use, for OpenHarmony, into <work>/third-party/install.

    python scripts/release/build_ros2_thirdparty.py --work E:/s2

ROS vendor packages first look for the library, and only download and build their own copy when it is missing. Their
downloaded copies install under opt/, which the unmodified packages then fail to find when cross compiled. Providing the
libraries here satisfies that first step, so no vendor package needs a change. Versions match the vendor packages.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

SDK = Path(os.environ.get('OHOS_NATIVE', Path.home() / 'AppData/Local/OpenHarmony/Sdk/23/native'))
GIT = [('yaml-cpp', 'https://github.com/jbeder/yaml-cpp.git', '0.8.0', 'f7320141120f720aecc4c32be25586e7da9eb978', '.',
        ['-DYAML_CPP_BUILD_CONTRIB=OFF', '-DYAML_CPP_BUILD_TESTS=OFF', '-DYAML_CPP_BUILD_TOOLS=OFF', '-DYAML_BUILD_SHARED_LIBS=ON']),
       ('zstd', 'https://github.com/facebook/zstd.git', 'v1.5.5', '63779c798237346c2b245c546c40b72a5a5913fe', 'build/cmake',
        ['-DZSTD_BUILD_STATIC=OFF', '-DZSTD_BUILD_SHARED=ON', '-DZSTD_BUILD_PROGRAMS=OFF']),
       ('lz4', 'https://github.com/lz4/lz4.git', 'v1.9.4', '5ff839680134437dbf4678f3d0c7b371d84f4964', 'build/cmake',
        ['-DBUILD_STATIC_LIBS=OFF', '-DBUILD_SHARED_LIBS=ON', '-DLZ4_BUILD_LEGACY_LZ4C=OFF', '-DLZ4_BUILD_CLI=OFF'])]
SQLITE_URL = 'https://www.sqlite.org/2022/sqlite-amalgamation-3370200.zip'
SQLITE_SHA256 = 'cb25df0fb90b77be6660f6ace641bbea88f3d0441110d394ce418f35f7561bb0'
SQLITE_CMAKE = """cmake_minimum_required(VERSION 3.20)
project(sqlite3 C)
include("${CMAKE_PROJECT_INCLUDE_FILE}" OPTIONAL)
add_library(sqlite3 SHARED sqlite3.c)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_COLUMN_METADATA=1)
install(TARGETS sqlite3 LIBRARY DESTINATION lib)
install(FILES sqlite3.h DESTINATION include)
"""


def configure_and_install(cmake, ninja, source, build, install, extra):
    subprocess.check_call([cmake, '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
                           '-DCMAKE_TOOLCHAIN_FILE=' + str(SDK / 'build/cmake/ohos.toolchain.cmake'), '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared',
                           '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DCMAKE_INSTALL_LIBDIR=lib', '-DCMAKE_INSTALL_PREFIX=' + str(install),
                           '-DCMAKE_SHARED_LIBRARY_SONAME_C_FLAG=-Wl,-soname,', '-DCMAKE_SHARED_LIBRARY_SONAME_CXX_FLAG=-Wl,-soname,',
                           '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW'] + extra)
    subprocess.check_call([cmake, '--build', str(build), '--parallel', '16'])
    subprocess.check_call([cmake, '--install', str(build)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True, type=Path)
    ap.add_argument('--cmake', default=str(SDK / 'build-tools/cmake/bin/cmake.exe'))
    ap.add_argument('--ninja', default=str(SDK / 'build-tools/cmake/bin/ninja.exe'))
    a = ap.parse_args()
    root = a.work / 'third-party'
    install = root / 'install'
    for name, url, tag, commit, subdirectory, flags in GIT:
        source = root / 'src' / name
        if not source.exists():
            subprocess.check_call(['git', '-c', 'core.autocrlf=false', 'clone', '--quiet', '--depth', '1', '--branch', tag, url, str(source)])
        found = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
        if found != commit:  # a moved tag, or an older source directory
            sys.exit('%s is at %s, expected %s' % (name, found, commit))
        configure_and_install(a.cmake, a.ninja, source / subdirectory, root / 'build' / name, install, flags)
    archive = root / 'sqlite.zip'
    if not archive.exists():
        urllib.request.urlretrieve(SQLITE_URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SQLITE_SHA256:
        sys.exit('sqlite archive hash %s is not the expected one' % digest)
    source = root / 'src/sqlite'
    if not source.exists():
        with zipfile.ZipFile(archive) as handle:
            handle.extractall(root / 'src')
        shutil.move(str(root / 'src/sqlite-amalgamation-3370200'), str(source))
    (source / 'CMakeLists.txt').write_text(SQLITE_CMAKE)
    configure_and_install(a.cmake, a.ninja, source, root / 'build/sqlite', install, [])
    print('third-party libraries in %s' % install)


if __name__ == '__main__':
    main()
