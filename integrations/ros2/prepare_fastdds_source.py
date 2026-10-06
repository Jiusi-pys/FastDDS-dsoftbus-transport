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


def prepare_candidate(source):
    """Apply the reviewed 2.14.7 port; old transforms/manifests remain untouched."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('topic_path_contract', HERE / 'apply_topic_rtti_key.py')
    paths = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(paths)
    unlinked_member = paths.unlinked_member
    candidate = HERE / 'candidates/2.14.7'
    meta = json.loads((candidate / 'UPSTREAM.json').read_text(encoding='utf-8'))
    if meta['version'] != '2.14.7' or meta['upstream_commit'] != '010b908dc3bb2b6f2818d61ac8db462d01b79a1f':
        raise ValueError('wrong maintenance candidate identity')
    patch = candidate / 'integration.patch'
    if hashlib.sha256(patch.read_bytes()).hexdigest() != meta['patch_sha256']:
        raise ValueError('candidate patch digest mismatch')
    receipt = source / 'FDDST_MAINTENANCE_PREPARED.json'
    if receipt.exists():
        raise ValueError('candidate already prepared')
    original_raw = {}
    for row in meta['files']:
        file = source / row['path']
        unlinked_member(source, file)
        original_raw[row['path']] = hashlib.sha256(file.read_bytes()).hexdigest()
        if original_raw[row['path']] not in {row['before_sha256'], row['official_blob_sha256']}:
            raise ValueError('candidate raw prerequisite mismatch: ' + row['path'])
    domain = source / 'src/cpp/fastdds/domain'
    for row in meta['headers']:
        unlinked_member(source, domain / row['name'])
        if (domain / row['name']).exists() or lf_sha256(HERE / row['name']) != row['lf_sha256']:
            raise ValueError('candidate header missing/drift/already present')
    eol_mappings = []
    for row in meta['files']:
        file = source / row['path']
        raw = file.read_bytes()
        lf = raw.replace(b'\r\n', b'\n')
        if hashlib.sha256(lf).hexdigest() != row['before_lf_sha256']:
            raise ValueError('candidate exact EOL mapping mismatch')
        if lf != raw:
            eol_mappings.append(dict(path=row['path'], original_raw_sha256=hashlib.sha256(raw).hexdigest(), mapped_lf_sha256=row['before_lf_sha256']))
            file.write_bytes(lf)
    repo = HERE.parents[1]
    args = ['git', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf', 'apply', '--directory=' + source.relative_to(repo).as_posix(), str(patch)]
    subprocess.run(args[:6] + ['--check'] + args[6:], cwd=repo, check=True)
    subprocess.run(args, cwd=repo, check=True)
    import shutil
    for row in meta['headers']:
        shutil.copyfile(HERE / row['name'], domain / row['name'])
    changed = []
    for row in meta['files']:
        digest = hashlib.sha256((source / row['path']).read_bytes()).hexdigest()
        if digest != row['after_sha256']:
            raise ValueError('candidate output mismatch: ' + row['path'])
        if row['path'] != 'CMakeLists.txt':
            changed.append(dict(row,actual_original_raw_sha256=original_raw[row['path']],actual_raw_after_sha256=digest))
    result = dict(version=meta['version'], upstream_commit=meta['upstream_commit'],
                  interface_abi=meta['interface_abi'], files=changed,
                  headers=[dict(row,actual_raw_sha256=hashlib.sha256((domain/row['name']).read_bytes()).hexdigest()) for row in meta['headers']],
                  steps=meta['steps'], eol_mappings=eol_mappings,
                  eol_policy='EXACT_ORIGINAL_RAW_PIN; EXPLICIT_PER_FILE_LF_MAP; EXACT_LF_OUTPUT')
    unlinked_member(source, receipt)
    receipt.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result


def prepare(source, version='2.14.5'):
    source = Path(source).absolute()
    # apply_topic_rtti_key.py only accepts an unlinked tree inside <repo>/build;
    # fail before any transform edits the tree, so a bad location leaves it pristine.
    if source != source.resolve() or not source.is_relative_to(HERE.parents[1] / 'build'):
        raise ValueError('--source must be a real (unlinked) directory inside <repo>/build')
    if version == '2.14.7':
        return prepare_candidate(source)
    if version != '2.14.5':
        raise ValueError('unsupported version-bound source preparation')
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
    parser.add_argument('--version', choices=['2.14.5', '2.14.7'], default='2.14.5')
    args = parser.parse_args()
    prepare(args.source, version=args.version)
    print('FASTDDS_SOURCE_PREPARED steps=%d headers=%d' % (len(SCRIPTS), len(HEADERS)))
