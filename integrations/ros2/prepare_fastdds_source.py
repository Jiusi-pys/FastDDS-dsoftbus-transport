"""Prepare a separately archived Fast DDS 2.14.5 tree with the complete patch chain.

This is the single supported entry for the Host/ROS vendor copy. It only runs the
version-bound transforms below, in order; each transform still verifies its own
exact upstream hashes and refuses a tree that was already patched or drifted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
# Order matters: every step's prerequisite hash is the previous step's output.
SCRIPTS = ['apply_fastdds_hook.py', 'apply_endpoint_guard.py', 'apply_teardown_observation.py',
           'apply_residual_observation.py', 'apply_topic_rtti_key.py']
# Headers the chain places next to DomainParticipantFactory.cpp (fastdds/domain).
HEADERS = ['FastddsHook.hpp', 'PlatformModule.hpp', 'StartupDiagnostics.hpp', 'TeardownDiagnostics.hpp',
           'ResidualDiagnostics.hpp', 'NativePrefix.hpp']


def lf_sha256(path):
    """Hash with CRLF folded to LF so the result does not depend on clone autocrlf."""
    return hashlib.sha256(Path(path).read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def exact_eol(path, expected):
    """Accept a file only if it equals `expected` or differs from it by line endings alone.

    The recorded hashes name different byte forms (the UPSTREAM/TEARDOWN rows name the LF blob;
    the RESIDUAL/TOPIC_RTTI rows name the CRLF form), so no single `git archive` line-ending
    setting satisfies all of them. This is the same per-file rule as target/rebuild.py
    (exact_eol): a pure line-ending conversion of the extracted copy to the exact form the hash
    names, nothing else. Content drift, a partial patch or a different revision matches neither
    form and is rejected. Returns (before_sha256, after_sha256) when the file was converted,
    else None; the caller reports every conversion.
    """
    before = Path(path).read_bytes()
    before_hash = hashlib.sha256(before).hexdigest()
    if before_hash == expected:
        return None
    lf = before.replace(b'\r\n', b'\n')
    matches = [x for x in (lf, lf.replace(b'\n', b'\r\n')) if hashlib.sha256(x).hexdigest() == expected]
    if len(matches) != 1:
        raise ValueError('exact source prerequisite mismatch: ' + str(path))
    Path(path).write_bytes(matches[0])
    return before_hash, expected


def prerequisites(source, script):
    """(path, expected_sha256) pairs the given transform requires before it runs."""
    def meta(name):
        return json.loads((HERE / name).read_text(encoding='utf-8-sig'))
    factory = 'src/cpp/fastdds/domain/DomainParticipantFactory.cpp'
    if script == 'apply_fastdds_hook.py':
        return [(x['path'], x['base_sha256']) for x in meta('UPSTREAM.json')['files']]
    if script == 'apply_teardown_observation.py':
        teardown = meta('TEARDOWN_UPSTREAM.json')
        return [(x['path'], x['base_sha256']) for x in teardown['files']] + [(factory, teardown['factory_after_existing_hook_sha256'])]
    if script == 'apply_residual_observation.py':
        return [(x['path'], x['base_sha256']) for x in meta('RESIDUAL_UPSTREAM.json')['files']]
    if script == 'apply_topic_rtti_key.py':
        return [(x['path'], x['base_sha256']) for x in meta('TOPIC_RTTI_UPSTREAM.json')['files']]
    return []  # apply_endpoint_guard.py reuses the UPSTREAM.json rows checked for the hook


def prepare(source):
    source = Path(source).absolute()
    # apply_topic_rtti_key.py only accepts an unlinked tree inside <repo>/build;
    # fail before any transform edits the tree, so a bad location leaves it pristine.
    if source != source.resolve() or not source.is_relative_to(HERE.parents[1] / 'build'):
        raise ValueError('--source must be a real (unlinked) directory inside <repo>/build')
    for script in SCRIPTS:
        for relative, expected in prerequisites(source, script):
            mapped = exact_eol(source / relative, expected)
            if mapped:
                print('EOL_MAPPED %s before=%s after=%s' % (relative, mapped[0], mapped[1]))
        subprocess.run([sys.executable, str(HERE / script), '--source', str(source)], check=True)
    domain = source / 'src/cpp/fastdds/domain'
    for header in HEADERS:
        if not (domain / header).is_file() or lf_sha256(domain / header) != lf_sha256(HERE / header):
            raise ValueError('prepared tree is missing or has a stale header: ' + header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    prepare(parser.parse_args().source)
    print('FASTDDS_SOURCE_PREPARED steps=%d headers=%d' % (len(SCRIPTS), len(HEADERS)))
