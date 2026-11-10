#!/usr/bin/env python3
"""Copy the OpenHarmony Python 3.12 runtime and its third-party modules from a board to the host.

    python scripts/release/pull_python_target.py <hdc target> <work_dir>

Python is an input, like the platform libraries of pull_platform_libs.py: the ROS Python layer compiles against its
headers and libpython, and the deployment carries the runtime. The result is <work_dir>/python-target/{usr,third_party}
with MANIFEST.txt, the board path and SHA-256 of every file. Nothing on the board changes.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

SDK = Path(os.environ.get('OHOS_NATIVE', Path.home() / 'AppData/Local/OpenHarmony/Sdk/23/native'))
HDC = os.environ.get('HDC', str(SDK.parent / 'toolchains' / 'hdc.exe'))
RUNTIME = '/data/local/tmp/python312-rk3588a/usr'
# Modules ROS needs at run time and rosdep would normally install. ROS packages in that directory are left out: they
# come from the pinned sources.
# What this repository was verified with. Another build of Python or NumPy needs its own verification, so it is refused.
EXPECTED = {'usr/lib/libpython3.12.so.1.0': '49313f420d7439f3f11ef7d819f5481b24dda9b5d64bf370409c07e7499d65d8',
            'third_party/numpy/core/include/numpy/numpyconfig.h': '36be7d904ddc5e67a7eb2d14ca6201694ec5d41488b8fc20299e20755e50e495'}
THIRD_PARTY = ('argcomplete', 'catkin_pkg', 'dateutil', 'docutils', 'em.py', 'lark', 'numpy', 'numpy.libs', 'packaging', 'psutil',
               'pyparsing', 'six.py', 'yaml')


def sh(target, command, seconds=120):
    return subprocess.run([HDC, '-t', target, 'shell', command], capture_output=True, text=True, timeout=seconds).stdout


def fetch_tar(target, folder, names, destination, label):
    """A tar on the board, because hdc file recv would take one round trip per file."""
    board = '/data/local/tmp/pull_python_%s.tar' % label
    if 'PULL_OK' not in sh(target, 'cd %s && tar cf %s %s && echo PULL_OK' % (folder, board, ' '.join(names)), 600):
        sys.exit('could not archive %s on the board' % folder)
    local = destination.parent / (label + '.tar')
    subprocess.run([HDC, '-t', target, 'file', 'recv', board, str(local)], check=True, capture_output=True, timeout=600)
    sh(target, 'rm -f %s' % board)
    with tarfile.open(local) as archive:
        archive.extractall(destination, filter='tar')
    local.unlink()


def main():
    target, work = sys.argv[1], Path(sys.argv[2])
    out = work / 'python-target'
    shutil.rmtree(out, ignore_errors=True)
    (out / 'usr').mkdir(parents=True, exist_ok=True)
    (out / 'third_party').mkdir(exist_ok=True)
    packaged = sh(target, 'ls -d /data/local/tmp/mdds_dds_*/package/python | tail -1').strip()
    if not packaged:
        sys.exit('no third-party module directory on this board')
    fetch_tar(target, RUNTIME, ['bin', 'include', 'lib'], out / 'usr', 'runtime')
    present = [n for n in THIRD_PARTY if n in sh(target, 'ls %s' % packaged).split()]
    missing = set(THIRD_PARTY) - set(present)
    if missing:
        sys.exit('missing third-party modules: %s' % ', '.join(sorted(missing)))
    fetch_tar(target, packaged, present, out / 'third_party', 'third_party')
    for name, digest in EXPECTED.items():
        if hashlib.sha256((out / name).read_bytes()).hexdigest() != digest:
            sys.exit('%s differs from the verified build' % name)
    rows = []
    for path in sorted(p for p in out.rglob('*') if p.is_file() and p.name != 'MANIFEST.txt'):
        rows.append('%s  %s' % (hashlib.sha256(path.read_bytes()).hexdigest(), path.relative_to(out).as_posix()))
    (out / 'MANIFEST.txt').write_text('runtime %s\nthird_party %s\n%s\n' % (RUNTIME, packaged, '\n'.join(rows)))
    print('pulled %d files to %s' % (len(rows), out))


if __name__ == '__main__':
    main()
