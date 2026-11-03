#!/usr/bin/env python
"""Run the ordinary-ROS closure inside the SECURITY=ON environment.

The positive case is the unchanged rclcpp application travelling through the
project's own factory, Transport and opaque-byte Host seam, with the builtin DDS
Security plugins active on both sides. The negative cases exercise an
unauthenticated peer and a mismatched security dependency.

Each case writes its own output directory under the isolated build tree. The
harness being driven is the project's ordinary ROS Host pair runner; it already
asserts the loaded fastrtps identity, the Host opaque-byte path, participant
deletion and RuntimeClose, so this driver only adds the security-specific
observations and the case-level accounting.

    python build/security-on-ros-closure-20261007/run_ros_security_closure.py ...
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import hashlib
import shutil
from pathlib import Path

def _repo_root(start):
    """Walk up to the repository root so the driver works from any location."""
    for candidate in [start] + list(start.parents):
        if (candidate / 'AGENTS.md').is_file() and (candidate / 'AGENTS.md').parent == candidate:
            return candidate
    raise SystemExit('repository root not found above ' + str(start))


ROOT = _repo_root(Path(__file__).resolve().parent)
RUNNER = ROOT / 'tests' / 'ros2_host' / 'run_pair.py'

# The Host opaque-byte seam must have carried real records both ways.
HOST_IO_PATTERN = r'FDDST_HOST_IO [^\r\n]*sends=[1-9][0-9]* ingress=[1-9][0-9]* send_errors=0'


# Explicit TEST_ONLY (M, B, N) forwarded to run_pair.py; None keeps the legacy unprotected profile.
PAYLOAD_RESOURCE_TEST = None
OBSERVE_FRAGMENTS = False
FRAGMENT_CONTROL = None


def run_case(name, work, exe, bridge, fastdds_prefix, ros_prefix, pixi_prefix,
             run_id, keystore, mode, extra, expect_pass, expect_stderr, activation='secure'):
    work.mkdir(parents=True, exist_ok=True)
    attempt = Path(tempfile.mkdtemp(prefix='case-', dir=work))
    out = attempt / 'run'
    command = [sys.executable, '-B', str(RUNNER), '--exe', str(exe), '--bridge', str(bridge),
               '--fastdds-prefix', str(fastdds_prefix), '--ros-prefix', str(ros_prefix),
               '--pixi-prefix', str(pixi_prefix), '--output', str(out),
               '--mode', mode, '--run-id', run_id]
    if keystore is not None:
        command += ['--secure-keystore', str(keystore)]
    command += extra
    if PAYLOAD_RESOURCE_TEST:
        command += ['--payload-resource-test'] + [str(v) for v in PAYLOAD_RESOURCE_TEST]
    if OBSERVE_FRAGMENTS:
        command += ['--observe-fragments']
    if FRAGMENT_CONTROL:
        command += ['--fragment-control', FRAGMENT_CONTROL, '--paired-close']
    (attempt / 'invocation.json').write_text(json.dumps({'name': name, 'argv': command,
        'activation': activation, 'environment': {key: os.environ.get(key) for key in
        ('PATH', 'AMENT_PREFIX_PATH', 'FDDST_ROS_FRAGMENT_BYTES')}}, indent=2), encoding='utf-8')
    timed_out = False
    try:
        proc = subprocess.run(command, capture_output=True, text=True, timeout=1200)
    except subprocess.TimeoutExpired as error:
        timed_out = True
        proc = subprocess.CompletedProcess(command, -1, error.stdout or '', error.stderr or '')
    except OSError as error:
        proc = subprocess.CompletedProcess(command, -1, '', repr(error))
    for suffix, content in (('stdout', proc.stdout), ('stderr', proc.stderr)):
        (attempt / ('runner.' + suffix)).write_bytes(content if isinstance(content, bytes) else content.encode('utf-8'))
    result = {}
    if (out / 'result.json').is_file():
        result = json.loads((out / 'result.json').read_text(encoding='utf-8'))

    security_evidence = {}
    side_text = {}
    for side in 'AB':
        text = ''.join(
            (out / (side + suffix)).read_text(encoding='utf-8', errors='replace')
            if (out / (side + suffix)).is_file() else ''
            for suffix in ('.stdout', '.stderr'))
        # These are different classes and must not be collapsed. FactoryAbiProbe
        # forwards only entries whose category is exactly SECURITY (authentication
        # and access control); the crypto plugin logs under SECURITY_CRYPTO, which
        # reaches stdout as an ordinary Fast DDS log line. Counting only the first
        # and reporting it as "security messages" would report 0 while the crypto
        # plugin was in fact being driven.
        side_text[side] = text
        security_evidence[side] = {
            'security_category_lines': len(re.findall(r'^FDDST_SECURITY_LOG ', text, re.M)),
            # The probe forwards only category= and message=, never the level, so
            # this counts refused/error-class security texts rather than a level tag.
            'security_category_errorish': len([m for m in re.findall(
                r'^FDDST_SECURITY_LOG .*$', text, re.M)
                if re.search(r'Error|not allowed|not found|PKCS7|failed', m)]),
            'access_control_refusals': len(re.findall(
                r'topic not found in allow rule', text)),
            'crypto_plugin_lines': len(re.findall(r'\[SECURITY_CRYPTO ', text)),
            'abi_mismatch': 'FDDST_SECURITY_ABI_MISMATCH' in text,
            'factory_abi_mismatch': 'FDDST_FACTORY_ABI_MISMATCH' in text,
        }
    # Expected markers may be emitted on either stream: the application's own
    # records go to stdout, the integration's diagnostics to stderr.
    observed_all = ''.join(
        (out / (side + suffix)).read_text(encoding='utf-8', errors='replace')
        if (out / (side + suffix)).is_file() else ''
        for side in 'AB' for suffix in ('.stdout', '.stderr'))

    sanitizer = [line for line in observed_all.splitlines()
                 if 'AddressSanitizer' in line]
    checks = [{'check': 'runner_completed', 'pass': not timed_out and bool(result)},
              {'check': 'runner_pass', 'actual': bool(result.get('pass')),
               'expected': expect_pass, 'pass': bool(result.get('pass')) == expect_pass}]
    # A sanitizer report is a finding wherever it appears. For the cases that must
    # succeed it is a hard failure; for the invalid-configuration case it is
    # recorded, because what it shows is an upstream defect on that path rather
    # than a project defect. Either way it is visible instead of unasserted.
    if expect_pass:
        checks.append({'check': 'no_sanitizer_report', 'actual': len(sanitizer),
                       'expected': 0, 'pass': not sanitizer})
    else:
        checks.append({'check': 'sanitizer_report_recorded', 'actual': len(sanitizer),
                       'expected': 'recorded', 'pass': True})
    for fragment in expect_stderr:
        checks.append({'check': 'stderr_contains(' + fragment + ')',
                       'actual': fragment in observed_all,
                       'expected': True, 'pass': fragment in observed_all})
    if expect_pass:
        # Activation evidence is structural, not log-textual: the child ran with
        # ROS_SECURITY_ENABLE=true and ROS_SECURITY_STRATEGY=Enforce, so a missing
        # or inactive security plugin makes the RMW refuse to create the
        # participant and the runner cannot pass. Security log lines are mostly
        # refusals, so requiring them would ask for a failure.
        # Independent of the runner verdict: each child must report that rcl
        # found and used its security enclave. This fails on its own if the
        # children were ever given no security configuration.
        for side in 'AB':
            found = ('Found security directory:' in side_text[side]
                     and side_text[side].count('Found security directory:') == 1)
            checks.append({'check': 'security_enclave_used_' + side,
                           'actual': found, 'expected': activation == 'secure',
                           'pass': found == (activation == 'secure')})
        access_errors = sum(v['security_category_errorish'] + v['access_control_refusals']
                           for v in security_evidence.values())
        crypto_lines = sum(v['crypto_plugin_lines'] for v in security_evidence.values())
        checks.append({'check': 'no_access_or_auth_control_errors',
                       'actual': access_errors, 'expected': 0, 'pass': access_errors == 0})
        checks.append({'check': 'crypto_plugin_driven',
                       'actual': crypto_lines, 'expected': '>0' if activation == 'secure' else 0,
                       'pass': crypto_lines > 0 if activation == 'secure' else crypto_lines == 0})
        checks.append({'check': 'host_path_carried_bytes',
                       'actual': bool(re.search(HOST_IO_PATTERN, observed_all)),
                       'expected': True,
                       'pass': bool(re.search(HOST_IO_PATTERN, observed_all))})
        checks.append({'check': 'no_abi_mismatch',
                       'actual': any(v['abi_mismatch'] or v['factory_abi_mismatch']
                                     for v in security_evidence.values()),
                       'expected': False,
                       'pass': not any(v['abi_mismatch'] or v['factory_abi_mismatch']
                                       for v in security_evidence.values())})

    if activation == 'invalid':
        loader_codes = {0xC0000135, 0xC0000139, 0xC000007B}
        processes = result.get('processes', {})
        checks += [
            {'check': 'config_or_security_rejection', 'pass': bool(re.search(
                r'(\[SECURITY Error\]|FDDST_SECURITY_LOG .*Error while configuring security plugin|security.*(failed|error)|Failed to create node)', observed_all, re.I))},
            {'check': 'no_business_take', 'pass': not re.search(r'ROS_(TAKE|FRAGMENT_TAKE) ', observed_all)},
            # Excludes only loader/ABI codes; an abnormal exit inside the ON
            # dependency is NOT excluded here and is reported by
            # sanitizer_report_recorded plus the summary.
            {'check': 'not_loader_or_abi_failure', 'pass': len(processes) == 2 and all(
                p.get('exit_code') is not None and p['exit_code'] not in loader_codes for p in processes.values())
                and 'ABI_MISMATCH' not in observed_all},
            {'check': 'plugin_refused_own_configuration', 'pass': all(
                'Error while configuring security plugin' in side_text[s] for s in 'AB')},
            {'check': 'plugin_refused_material_format', 'pass': all(
                ('PKCS7' in side_text[s] and 'S/MIME' in side_text[s]) for s in 'AB')},
            # Recorded, deliberately not asserted to be a clean code: the plugin
            # refuses the configuration and the process then terminates
            # abnormally. A warning exit would not make the refusal less real, and
            # an abnormal exit does not make it less of a refusal.
            {'check': 'failure_exit_codes_recorded', 'actual': sorted(
                p.get('exit_code') for p in processes.values()) if processes else None,
             'expected': 'recorded', 'pass': bool(processes) and all(
                p.get('exit_code') is not None for p in processes.values())},
            {'check': 'on_dependency_loaded', 'pass': all(
                'FDDST_LOADED module=fastrtps-2.14.dll path=' + str((fastdds_prefix/'bin/fastrtps-2.14.dll').resolve())
                in side_text[s] for s in 'AB')},
        ]
    payload = dict(name=name, attempt_dir=str(attempt), output=str(out), activation=activation,
                   sanitizer_report_lines=sanitizer,
                   input_sha256={str(f.resolve()): hashlib.sha256(f.read_bytes()).hexdigest()
                       for f in (exe, bridge, fastdds_prefix/'bin/fastrtps-2.14.dll', ros_prefix/'bin/fastcdr-2.2.dll') if f.is_file()},
                   timed_out=timed_out, expect_pass=expect_pass, runner_exit=proc.returncode,
                   runner_result=result, security_evidence=security_evidence,
                   checks=checks, passed=all(c['pass'] for c in checks),
                   runner_stderr=str(proc.stderr)[-1500:])
    (attempt / 'case.json').write_text(json.dumps(payload, indent=2) + '\n', encoding='utf-8')
    print(('PASS ' if payload['passed'] else 'FAIL ') + name +
          ' runner_pass=%s errors=%s' % (result.get('pass'), result.get('errors')))
    return payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--fastdds-prefix', type=Path, required=True)
    parser.add_argument('--off-fastdds-prefix', type=Path)
    parser.add_argument('--ros-prefix', type=Path, required=True)
    parser.add_argument('--pixi-prefix', type=Path, required=True)
    parser.add_argument('--keystore', type=Path, required=True)
    parser.add_argument('--run-id', default='0123456789abcdef')
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--only', default=None)
    parser.add_argument('--payload-resource-test', nargs=3, type=int, metavar=('M_BYTES', 'B_BYTES', 'N_RECORDS'))
    parser.add_argument('--observe-fragments', action='store_true')
    parser.add_argument('--fragment-control', choices=['late-release', 'drop-last'])
    args = parser.parse_args()
    global PAYLOAD_RESOURCE_TEST
    PAYLOAD_RESOURCE_TEST = args.payload_resource_test
    global OBSERVE_FRAGMENTS
    OBSERVE_FRAGMENTS = args.observe_fragments
    global FRAGMENT_CONTROL
    FRAGMENT_CONTROL = args.fragment_control
    if FRAGMENT_CONTROL and (args.only!='04' or not OBSERVE_FRAGMENTS or not PAYLOAD_RESOURCE_TEST):
        parser.error('fragment control requires only04 and explicit guarded fragment observation')

    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='attempt-', dir=root))
    (work / 'invocation.json').write_text(json.dumps({'argv': sys.argv, 'work': str(work)}, indent=2), encoding='utf-8')
    common = dict(exe=args.exe.resolve(), bridge=args.bridge.resolve(),
                  ros_prefix=args.ros_prefix.resolve(), pixi_prefix=args.pixi_prefix.resolve(),
                  run_id=args.run_id)
    results = []

    def want(name):
        return args.only is None or args.only in name

    if want('01'):
        results.append(run_case(
            '01_secure_dual_pair_20writes_20takes', work,
            fastdds_prefix=args.fastdds_prefix.resolve(), keystore=args.keystore.resolve(),
            mode='dual', extra=[], expect_pass=True,
            expect_stderr=['FDDST_ROS_CLOSED'], **common))

    if want('02'):
        # An unauthenticated peer: one side runs without DDS Security at all.
        results.append(run_case(
            '02_unauthenticated_peer_no_delivery', work,
            fastdds_prefix=args.fastdds_prefix.resolve(), keystore=args.keystore.resolve(),
            mode='dual', extra=['--plain-side', 'A'], expect_pass=False,
            # Both sides must fail for the observed reason. The plain side is
            # never matched by the secured one, so the failure is reported at the
            # MATCH stage rather than as a data-path error. This does not by
            # itself prove the refusal came from the security layer; the secured
            # side's enclave use and the absence of any match are what the run
            # shows, and the session-level cause remains inferred.
            expect_stderr=['REMOTE_MATCH_MISSING'], **common))

    if want('03'):
        # Mixed dependency: the ON application and RMW with the SECURITY=OFF
        # fastrtps. The refusal happens in the Windows loader, which cannot
        # resolve the ON import table against the OFF binary, so the process
        # exits with STATUS_ENTRYPOINT_NOT_FOUND before any project code runs.
        # This case therefore shows that the mix is refused; it does NOT exercise
        # the project's own build-mode gate, and no gate marker is expected here.
        if args.off_fastdds_prefix is None:
            parser.error('case 03 requires an explicit --off-fastdds-prefix')
        results.append(run_case(
            '03_mixed_dependency_refused_by_loader', work,
            fastdds_prefix=args.off_fastdds_prefix.resolve(), keystore=args.keystore.resolve(),
            mode='dual', extra=[], expect_pass=False,
            expect_stderr=[], **common))

    if want('04'):
        # A real fragmented sample through the same secure path. 70000 bytes is
        # above the transport's 65535 message ceiling, so the writer must fragment
        # and the reader must reassemble under security. A 200000-byte payload was
        # tried first and did reassemble exactly (ROS_FRAGMENT_TAKE bytes=200029
        # exact=1 for all five ordinals) but exceeded the run's time budget, so the
        # smaller size is used to keep the case inside it.
        os.environ['FDDST_ROS_FRAGMENT_BYTES'] = '70000'
        try:
            results.append(run_case(
                '04_secure_pair_real_fragmented_sample', work,
                fastdds_prefix=args.fastdds_prefix.resolve(), keystore=args.keystore.resolve(),
                mode='dual', extra=[], expect_pass=True,
                expect_stderr=['FDDST_ROS_CLOSED', 'ROS_FRAGMENT_TAKE'], **common))
        finally:
            os.environ.pop('FDDST_ROS_FRAGMENT_BYTES', None)

    if args.only == 'A':
        results.append(run_case('A_same_on_prefix_development_control', work,
            fastdds_prefix=args.fastdds_prefix.resolve(), keystore=None, mode='dual',
            extra=['--security-development-control'], expect_pass=True, expect_stderr=['FDDST_ROS_CLOSED'],
            activation='development', **common))
    if args.only == 'C':
        broken = work / 'invalid-keystore'
        shutil.copytree(args.keystore, broken)
        for side in 'AB':
            governance = broken / 'enclaves' / side / 'governance.p7s'
            if not governance.is_file():
                raise ValueError('Missing declared synthetic governance input: ' + str(governance))
            governance.write_text('INVALID TEST-ONLY GOVERNANCE SIGNATURE\n', encoding='ascii')
        results.append(run_case('C_required_invalid_governance', work,
            fastdds_prefix=args.fastdds_prefix.resolve(), keystore=broken, mode='dual', extra=[],
            # No ROS_FAILURE marker: the plugin refuses before the application's
            # own failure protocol is reached, so the rejection is observed in the
            # security layer rather than in the app. The exit status is recorded,
            # not required to be clean.
            expect_pass=False, expect_stderr=[], activation='invalid', **common))

    summary = dict(attempt=str(work), cases_total=len(results), cases_passed=sum(1 for x in results if x['passed']), cases=results)
    (work / 'ROS_SECURITY_CLOSURE.json').write_text(json.dumps(summary, indent=2) + '\n',
                                                    encoding='utf-8')
    print(json.dumps(summary))
    return 0 if results and summary['cases_passed'] == summary['cases_total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
