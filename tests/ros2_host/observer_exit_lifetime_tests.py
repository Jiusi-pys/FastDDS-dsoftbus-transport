"""Real observer source, bounded Linux child/finalization regression; no SDK/HDC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--wsl-distro', default='Ubuntu-20.04')
    parser.add_argument('--compiler', default='/usr/bin/g++')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.output_root.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='observer-exit-', dir=args.output_root)).resolve()
    if os.name == 'nt':
        wsl = shutil.which('wsl.exe')
        if not wsl:
            print('SKIP: Linux/WSL compiler environment required')
            return 77
        prefix = [wsl, '-d', args.wsl_distro, '--exec']

        def native(path):
            value = subprocess.run(prefix + ['wslpath', '-a', str(path.resolve())],
                                   capture_output=True, check=True, timeout=15)
            return value.stdout.decode('utf-8').strip()
    else:
        prefix = []

        def native(path):
            return str(path.resolve())

    inputs = [repo/'tests/ros2_host/observer_exit_lifetime.cpp',
              repo/'integrations/ros2/target/NativeObservation.cpp',
              repo/'probes/PublicSocketRtpsObservation.cpp']
    identities = {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in inputs + [repo/'integrations/ros2/target/NativeObservation.hpp',
                                     repo/'integrations/ros2/StartupDiagnostics.hpp',repo/'integrations/ros2/NativePrefix.hpp']}
    records = []

    def execute(label, argv, limit):
        record = {'label': label, 'argv': argv, 'cwd': str(repo), 'timeout_seconds': limit}
        try:
            result = subprocess.run(argv, cwd=repo, capture_output=True, timeout=limit)
            record.update(exit_code=result.returncode, timed_out=False)
            stdout, stderr = result.stdout, result.stderr
        except subprocess.TimeoutExpired as error:
            record.update(exit_code=None, timed_out=True)
            stdout, stderr = error.stdout or b'', error.stderr or b''
        (output/(label+'.stdout')).write_bytes(stdout)
        (output/(label+'.stderr')).write_bytes(stderr)
        records.append(record)
        return record['exit_code'] == 0, stdout.decode('utf-8', errors='replace')

    binary = output/'observer_exit'
    command = prefix + [args.compiler, '-std=c++17', '-O0', '-g', '-pthread', '-rdynamic',
                        '-DFASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST=1', '-Wl,--wrap=__cxa_atexit',
                        '-I'+native(repo/'include')] + [native(p) for p in inputs] + ['-ldl', '-o', native(binary)]
    compiled, _ = execute('build', command, 60)
    passed = compiled
    if compiled:
        for mode in ('lifetime', 'prebegin', 'invalid', 'duplicate', 'concurrent','prefix'):
            ok, text = execute(mode, prefix + ['timeout', '12', native(binary), mode], 20)
            passed = passed and ok and text.count('OBSERVER_EXIT_LIFETIME result=PASS ') == 1
            passed = passed and 'result=FAIL' not in text
    receipt = {'scope': 'REAL_OBSERVER_HOST_COMPONENT_NOT_ROS_SDK_DEVICE', 'pass': passed,
               'inputs': identities, 'commands': records, 'device_operations': 0}
    (output/'RESULT.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps({'pass': passed, 'receipt': str(output/'RESULT.json')}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
