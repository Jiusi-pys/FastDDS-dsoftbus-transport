#!/usr/bin/env python3
"""Copy the board's DSoftBus Socket provider (libsoftbus_client.z.so) and its shared-library
closure to the host, for linking only (rpath-link). Nothing is pushed or changed on the board.

    python scripts/release/pull_platform_libs.py <hdc target> <out_dir>

Writes <out_dir>/MANIFEST.txt with the board path and SHA-256 of every pulled file.
"""
import hashlib
import os
import subprocess
import sys
from pathlib import Path

SDK = Path(os.environ.get('OHOS_NATIVE', Path.home() / 'AppData/Local/OpenHarmony/Sdk/23/native'))
HDC = os.environ.get('HDC', str(SDK.parent / 'toolchains' / 'hdc.exe'))
READELF = str(SDK / 'llvm' / 'bin' / 'llvm-readelf.exe')
SEARCH = ['/system/lib64/platformsdk', '/system/lib64/chipset-sdk', '/system/lib64/chipset-pub-sdk',
          '/system/lib64/chipset-sdk-sp', '/system/lib64/ndk', '/system/lib64', '/vendor/lib64']
# Provided by the SDK sysroot / toolchain at link time.
SDK_LIBS = {'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so', 'libc++_shared.so', 'libc++.so',
            'ld-musl-aarch64.so.1', 'libz.so', 'libhilog_ndk.z.so'}


def sh(target, cmd):
    return subprocess.run([HDC, '-t', target, 'shell', cmd], capture_output=True, text=True, timeout=60).stdout


def needed(path):
    out = subprocess.run([READELF, '-d', str(path)], capture_output=True, text=True).stdout
    return [line.split('[', 1)[1].split(']', 1)[0] for line in out.splitlines() if '(NEEDED)' in line]


def main():
    target, out = sys.argv[1], Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    listing = {}
    for d in SEARCH:
        for name in sh(target, 'ls %s 2>/dev/null' % d).split():
            listing.setdefault(name.strip(), d + '/' + name.strip())
    todo, done, manifest = ['libsoftbus_client.z.so'], set(), []
    while todo:
        name = todo.pop()
        if name in done or name in SDK_LIBS:
            continue
        done.add(name)
        if name not in listing:
            print('not on board (expected from SDK):', name)
            continue
        dst = out / name
        if not dst.exists():
            r = subprocess.run([HDC, '-t', target, 'file', 'recv', listing[name], str(dst)], capture_output=True, text=True, timeout=120)
            if not dst.exists():
                sys.exit('pull failed: %s %s' % (listing[name], r.stdout))
        manifest.append('%s %s %s' % (hashlib.sha256(dst.read_bytes()).hexdigest(), listing[name], name))
        todo.extend(needed(dst))
    (out / 'MANIFEST.txt').write_text('\n'.join(sorted(manifest)) + '\n')
    print('pulled %d libraries into %s' % (len(manifest), out))


if __name__ == '__main__':
    main()
