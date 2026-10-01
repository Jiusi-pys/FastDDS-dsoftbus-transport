"""Replay the real adapters against an explicitly supplied local upstream Git tree.

No download, checkout, SDK execution or modification of the supplied repository.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


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
    assert hashlib.sha256((integration / 'FastddsHook.hpp').read_bytes()).hexdigest() == identity['hook_sha256']
    assert hashlib.sha256((integration / 'PlatformModule.hpp').read_bytes()).hexdigest() == identity['platform_sha256']
    assert hashlib.sha256((integration / 'StartupDiagnostics.hpp').read_bytes()).hexdigest() == identity['startup_diagnostics_sha256']
    with tempfile.TemporaryDirectory(prefix='fdds-ros2-patch-') as directory:
        target = Path(directory)
        for name, content in originals.items():
            destination = target / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        scripts = ['apply_fastdds_hook.py', 'apply_endpoint_guard.py']
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
    print(f'PATCH_DELIVERY_PASS exact_base={count} exact_output={count} repeated_rejected=2 drift_rejected=1')


if __name__ == '__main__':
    main()
