"""Real Windows Host SendBytes selector/transfer tests; no DDS or device claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--host-source-dir', type=Path)
    parser.add_argument('--pre-fix', action='store_true')
    parser.add_argument('--asan', action='store_true')
    parser.add_argument('--mode', action='append')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    host = (args.host_source_dir or repo/'integrations/ros2').resolve()
    args.output_root.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='host-fragment-', dir=args.output_root)).resolve()
    if os.name != 'nt':
        print('NOT_RUN: Windows Host seam required'); return 77
    env = os.environ.copy()
    raw = subprocess.check_output(['cmd', '/d', '/c', str(repo/'build/payload-resource-guard-20261010/h2/env.bat')], text=True)
    for line in raw.splitlines():
        key, separator, value = line.partition('=')
        if separator and key: env[key.upper()] = value
    inputs = [repo/'tests/ros2_host/host_fragment_control_tests.cpp', host/'HostSocket.cpp', repo/'probes/PublicSocketRtpsObservation.cpp']
    identity_inputs = inputs + [host/'HostSocket.hpp', repo/'probes/PublicSocketRtpsObservation.hpp']
    identities = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in identity_inputs}
    commands = []

    def execute(label, argv, timeout):
        record = {'label': label, 'argv': list(map(str, argv)), 'cwd': str(output), 'timeout_seconds': timeout}
        try:
            result = subprocess.run(record['argv'], cwd=output, env=env, capture_output=True, timeout=timeout)
            record.update(exit_code=result.returncode, timed_out=False)
            stdout, stderr = result.stdout, result.stderr
        except subprocess.TimeoutExpired as error:
            record.update(exit_code=None, timed_out=True)
            stdout, stderr = error.stdout or b'', error.stderr or b''
        (output/(label+'.stdout')).write_bytes(stdout); (output/(label+'.stderr')).write_bytes(stderr)
        commands.append(record)
        return record['exit_code'], stdout.decode('utf-8', errors='replace'), stderr.decode('utf-8', errors='replace')

    binary = output/'host_fragment_control.exe'
    compiler = shutil.which('cl.exe', path=env['PATH'])
    if not compiler:
        raise RuntimeError('configured MSVC compiler unavailable')
    argv = [compiler, '/nologo', '/std:c++17', '/EHsc', '/MD', '/UNDEBUG', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/I'+str(host)]
    if args.pre_fix: argv.append('/DFASTDDS_DSOFTBUS_FRAGMENT_CONTROL_PRE_FIX=1')
    if args.asan: argv += ['/fsanitize=address', '/Zi']
    argv += ['/I'+str(repo/'communication_dsoftbus/interfaces/kits'/directory) for directory in ('transport','bus_center','common')]
    argv += list(map(str, inputs)) + ['/Fe:'+str(binary), '/link', 'ws2_32.lib', '/INCREMENTAL:NO']
    compiled, _, _ = execute('build', argv, 90)
    passed = compiled == 0
    modes = args.mode or ['drop','drop-once','noaction','default','wrong-run','wrong-writer','wrong-sequence','stale-pid','nonlast','invalid-bounds','serialization','multi-submessage']
    if passed:
        for mode in modes:
            run = uuid.uuid4().hex[:16]
            code, stdout, stderr = execute(mode, [binary, mode, run, output/('control-'+mode)], 40)
            expected_drops = 1 if mode in ('drop','drop-once') else 0
            ok = code == 0 and stdout.count('HOST_FRAGMENT_DROP ') == expected_drops and 'HOST_FRAGMENT_TEST result=PASS' in stdout
            if expected_drops:
                ok = ok and 'api_called=1 actual_host_write=0 native_result=0 expected_negative=1 drops=1 identity=ORIGINAL_BORROWED_ARGS' in stdout
            ok = ok and 'ERROR: AddressSanitizer' not in stdout+stderr
            commands[-1]['check_pass'] = ok; passed = passed and ok
    receipt = {'scope': 'REAL_HOST_SEAM_SELECTOR_AND_TCP_TRANSFER_NOT_DDS_DEVICE', 'pass': passed, 'inputs': identities,
               'commands': commands, 'pre_fix': args.pre_fix, 'asan': args.asan, 'device_operations': 0}
    (output/'RESULT.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps({'pass': passed, 'receipt': str(output/'RESULT.json')}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
