"""Replay the real adapters against an explicitly supplied local upstream Git tree.

No download, checkout, SDK execution or modification of the supplied repository.
"""
import argparse
import contextlib
import difflib
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'integrations/ros2'))
from prepare_fastdds_source import HEADERS, SCRIPTS, lf_sha256, prepare  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--upstream-git', type=Path, required=True)
    args = parser.parse_args()
    integration = Path(__file__).resolve().parents[2] / 'integrations/ros2'
    identity = json.loads((integration / 'UPSTREAM.json').read_text())
    originals = {}
    for name in [x['path'] for x in identity['files']] + ['CMakeLists.txt']:
        originals[name] = subprocess.check_output([
            'git', '-C', str(args.upstream_git), 'show', identity['commit'] + ':' + name])
    for item in identity['files']:
        assert hashlib.sha256(originals[item['path']]).hexdigest() == item['base_sha256']
    # Later transforms patch more upstream files. Each file's first listing is its
    # pristine base; a later listing may be the previous transform's output, which
    # the transform itself verifies when it runs.
    for name in ['TEARDOWN_UPSTREAM.json', 'RESIDUAL_UPSTREAM.json', 'TOPIC_RTTI_UPSTREAM.json']:
        extra = json.loads((integration / name).read_text(encoding='utf-8-sig'))
        assert extra['commit'] == identity['commit'], name
        for item in extra['files']:
            if item['path'] in originals:
                continue
            blob = subprocess.check_output([
                'git', '-C', str(args.upstream_git), 'show', identity['commit'] + ':' + item['path']])
            # Same exact-EOL mapping as target/rebuild.py: the recorded base hash is
            # accepted only if exactly one pure-EOL form of the pinned blob matches it.
            lf = blob.replace(b'\r\n', b'\n')
            forms = {hashlib.sha256(x).hexdigest(): x for x in (blob, lf, lf.replace(b'\n', b'\r\n'))}
            assert item['base_sha256'] in forms, item['path']
            originals[item['path']] = forms[item['base_sha256']]
    for header, key in [('FastddsHook.hpp', 'hook_sha256'), ('PlatformModule.hpp', 'platform_sha256'),
                        ('StartupDiagnostics.hpp', 'startup_diagnostics_sha256'),
                        ('TeardownDiagnostics.hpp', 'teardown_diagnostics_sha256'),
                        ('ResidualDiagnostics.hpp', 'residual_diagnostics_sha256'),
                        ('NativePrefix.hpp', 'native_prefix_sha256')]:
        assert lf_sha256(integration / header) == identity[key], header
    # Independent of the module under test: the chain is exactly these five transforms, in this order,
    # and the vendor tree needs exactly these six headers. A dropped or reordered step must fail here.
    assert list(SCRIPTS) == ['apply_fastdds_hook.py', 'apply_endpoint_guard.py', 'apply_teardown_observation.py',
                             'apply_residual_observation.py', 'apply_topic_rtti_key.py'], SCRIPTS
    assert sorted(HEADERS) == sorted(['FastddsHook.hpp', 'PlatformModule.hpp', 'StartupDiagnostics.hpp',
                                      'TeardownDiagnostics.hpp', 'ResidualDiagnostics.hpp', 'NativePrefix.hpp']), HEADERS
    build = integration.parents[1] / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fdds-ros2-patch-', dir=build) as directory:
        target = Path(directory).resolve()
        for name, content in originals.items():
            destination = target / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        scripts = SCRIPTS[:2]
        for script in scripts:
            subprocess.run([sys.executable, str(integration / script), '--source', str(target)], check=True)
        diff = []
        for item in identity['files']:
            name = item['path']
            patched = (target / name).read_bytes()
            assert hashlib.sha256(patched).hexdigest() == item['patched_sha256'], name
            diff.extend(difflib.unified_diff(
                originals[name].decode().replace('\r\n', '\n').splitlines(True),
                patched.decode().replace('\r\n', '\n').splitlines(True),
                fromfile='a/' + name, tofile='b/' + name, n=0))
        assert ''.join(diff) == (integration / 'fastdds-2.14.5-factory.patch').read_text()
        # The remaining transforms must accept the output above, and the complete
        # chain must leave every header the vendor tree needs next to the factory.
        for script in SCRIPTS[2:]:
            subprocess.run([sys.executable, str(integration / script), '--source', str(target)], check=True)
        domain = target / 'src/cpp/fastdds/domain'
        for header in HEADERS:
            assert (domain / header).is_file(), header
            assert lf_sha256(domain / header) == lf_sha256(integration / header), header
        # The single supported entry itself, on raw git blobs exactly as `git archive` would extract them
        # (no mapping done here). It must report every EOL conversion it makes, produce the same tree as
        # the chain applied directly above, refuse a repeat, and refuse drift in the first prerequisite file before editing anything.
        # `git archive` with autocrlf off is the documented extraction; its bytes differ from `git show`.
        archive = subprocess.check_output([
            'git', '-c', 'core.autocrlf=false', '-C', str(args.upstream_git), 'archive', '--format=tar',
            identity['commit'], *originals])
        raw = {}
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            for name in originals:
                raw[name] = tar.extractfile(name).read()
        # Rows whose recorded base hash names the pristine upstream file (the factory row of the
        # teardown/residual lists names an already-patched state, so it is not a pristine base).
        factory = 'src/cpp/fastdds/domain/DomainParticipantFactory.cpp'
        pristine = [(x['path'], x['base_sha256']) for x in identity['files']]
        for name in ('TEARDOWN_UPSTREAM.json', 'RESIDUAL_UPSTREAM.json', 'TOPIC_RTTI_UPSTREAM.json'):
            rows = json.loads((integration / name).read_text(encoding='utf-8-sig'))['files']
            pristine += [(x['path'], x['base_sha256']) for x in rows if x['path'] != factory]
        expected_mapped = sorted({path for path, digest in pristine if hashlib.sha256(raw[path]).hexdigest() != digest})
        assert expected_mapped, 'the archive form needed no mapping: this check would prove nothing'
        with tempfile.TemporaryDirectory(prefix='fdds-ros2-entry-', dir=build) as directory:
            entry = Path(directory).resolve()
            for name, content in raw.items():
                destination = entry / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                prepare(entry)
            mapped = sorted(line.split()[1] for line in captured.getvalue().splitlines() if line.startswith('EOL_MAPPED '))
            assert mapped == expected_mapped, (mapped, expected_mapped)
            assert (entry / 'FDDST_TOPIC_RTTI_PATCH.json').is_file(), 'the last transform did not run'
            files = sorted(p.relative_to(target).as_posix() for p in target.rglob('*') if p.is_file())
            assert files == sorted(p.relative_to(entry).as_posix() for p in entry.rglob('*') if p.is_file())
            # CMakeLists.txt is only the version anchor: no transform edits it or maps its line endings,
            # so each tree keeps the form it was seeded with. Every file a transform touches must match.
            for relative in files:
                if relative == 'CMakeLists.txt':
                    continue
                assert (target / relative).read_bytes() == (entry / relative).read_bytes(), relative
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    prepare(entry)
            except (subprocess.CalledProcessError, ValueError):
                pass
            else:
                raise AssertionError('a repeated preparation of a patched tree was accepted')
        with tempfile.TemporaryDirectory(prefix='fdds-ros2-entry-drift-', dir=build) as directory:
            drifted = Path(directory).resolve()
            for name, content in raw.items():
                destination = drifted / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
            name = identity['files'][0]['path']
            (drifted / name).write_bytes(raw[name] + b'\n// drift\n')
            before = {p.relative_to(drifted).as_posix(): p.read_bytes() for p in drifted.rglob('*') if p.is_file()}
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    prepare(drifted)
            except (subprocess.CalledProcessError, ValueError):
                pass
            else:
                raise AssertionError('drifted upstream content was accepted by the entry')
            after = {p.relative_to(drifted).as_posix(): p.read_bytes() for p in drifted.rglob('*') if p.is_file()}
            assert before == after, 'the entry edited a tree it then refused'
        # Repeat application must not quietly overwrite a previously patched tree.
        for script in scripts:
            assert subprocess.run([sys.executable, str(integration / script), '--source', str(target)],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode != 0
        # A version label with modified upstream content is not an accepted base.
        name = identity['files'][0]['path']
        (target / name).write_bytes(originals[name] + b'\n// drift\n')
        assert subprocess.run([sys.executable, str(integration / scripts[0]), '--source', str(target)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode != 0
    count = len(identity['files'])
    print(f'PATCH_DELIVERY_PASS exact_base={count} exact_output={count} chain_steps={len(SCRIPTS)} headers={len(HEADERS)} entry_tree_identical=1 entry_repeat_rejected=1 entry_drift_rejected=1 repeated_rejected=2 drift_rejected=1')


if __name__ == '__main__':
    main()
