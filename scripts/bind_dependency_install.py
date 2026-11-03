#!/usr/bin/env python3
"""Create a content-correspondence receipt in an explicitly selected new prefix.

This project-generated receipt is not an upstream signature or publisher trust.
It never loads a library, starts a runtime, or modifies a system SDK.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bind(prefix, library, build_cache, version):
    prefix = prefix.resolve(strict=True)
    library = library.resolve(strict=True)
    if not library.is_relative_to(prefix):
        raise ValueError('library must belong to the explicit installation prefix')
    if library.parent.parent != prefix:
        raise ValueError('binding supports installed bin/lib directly under prefix')
    header = prefix / 'include/fastrtps/config.h'
    if not header.is_file():
        raise ValueError('installed fastrtps/config.h missing')
    text = header.read_text(encoding='utf-8')
    macros = {}
    for name in ('FASTRTPS_VERSION_MAJOR', 'FASTRTPS_VERSION_MINOR', 'FASTRTPS_VERSION_MICRO', 'HAVE_SECURITY'):
        match = re.search(r'^\s*#define\s+' + name + r'\s+(\d+)\s*$', text, re.M)
        if not match:
            raise ValueError('installed configuration macro missing: ' + name)
        macros[name] = int(match.group(1))
    actual = '.'.join(str(macros['FASTRTPS_VERSION_' + x]) for x in ('MAJOR', 'MINOR', 'MICRO'))
    if actual != version or macros['HAVE_SECURITY'] not in (0, 1):
        raise ValueError('installed version/security does not match declared input')
    cache = build_cache.read_text(encoding='utf-8')
    selected = re.search(r'^SECURITY:BOOL=(ON|OFF)$', cache, re.M)
    if not selected or (selected.group(1) == 'ON') != bool(macros['HAVE_SECURITY']):
        raise ValueError('actual build cache and installed security configuration disagree')
    files = sorted(x for x in (prefix / 'include').rglob('*') if x.is_file())
    configs = sorted((prefix / 'share/fastrtps/cmake').glob('*.cmake'))
    if not configs:
        raise ValueError('installed fastrtps CONFIG export missing')
    for file in files + configs:
        if file.is_symlink() or not file.resolve().is_relative_to(prefix):
            raise ValueError('linked/external installed input not supported: ' + str(file))
    row = lambda p: dict(file=p.relative_to(prefix).as_posix(), sha256=sha(p))
    record = dict(schema=1, dependency='fastrtps', version=actual,
                  library_file=library.relative_to(prefix).as_posix(), library_sha256=sha(library),
                  include_root='include', headers=[row(x) for x in files],
                  configuration_files=[row(x) for x in configs],
                  build=dict(security=macros['HAVE_SECURITY'], cache_sha256=sha(build_cache),
                             configuration='Release'), publisher_trust='UNVERIFIED')
    output = prefix / 'share/fddst-dependency-binding/fastrtps.json'
    if output.exists():
        raise ValueError('binding already exists; do not overwrite an old installation receipt')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--build-cache', type=Path, required=True)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    print(bind(args.prefix, args.library, args.build_cache, args.version))
