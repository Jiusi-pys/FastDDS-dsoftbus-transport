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


def run_case(name, work, exe, bridge, fastdds_prefix, ros_prefix, pixi_prefix,
             run_id, keystore, mode, extra, expect_pass, expect_stderr):
    out = work / name
    if out.exists():
        import shutil
        shutil.rmtree(out)
    command = [sys.executable, '-B', str(RUNNER), '--exe', str(exe), '--bridge', str(bridge),
               '--fastdds-prefix', str(fastdds_prefix), '--ros-prefix', str(ros_prefix),
               '--pixi-prefix', str(pixi_prefix), '--output', str(out),
               '--mode', mode, '--run-id', run_id]
    if keystore is not None:
        command += ['--secure-keystore', str(keystore)]
    command += extra
    proc = subprocess.run(command, capture_output=True, text=True, timeout=1200)
    result = {}
    if (out / 'result.json').is_file():
        result = json.loads((out / 'result.json').read_text(encoding='utf-8'))

    security_evidence = {}
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
        security_evidence[side] = {
            'security_category_lines': len(re.findall(r'^FDDST_SECURITY_LOG ', text, re.M)),
            'security_category_errors': len(re.findall(
                r'^FDDST_SECURITY_LOG .*\[SECURITY Error\]', text, re.M)),
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

    checks = [{'check': 'runner_pass', 'actual': bool(result.get('pass')),
               'expected': expect_pass, 'pass': bool(result.get('pass')) == expect_pass}]
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
        checks.append({'check': 'security_enforced_run_passed',
                       'actual': bool(result.get('pass')),
                       'expected': True, 'pass': bool(result.get('pass'))})
        access_errors = sum(v['security_category_errors'] + v['access_control_refusals']
                           for v in security_evidence.values())
        crypto_lines = sum(v['crypto_plugin_lines'] for v in security_evidence.values())
        checks.append({'check': 'no_access_or_auth_control_errors',
                       'actual': access_errors, 'expected': 0, 'pass': access_errors == 0})
        checks.append({'check': 'crypto_plugin_driven',
                       'actual': crypto_lines, 'expected': '>0', 'pass': crypto_lines > 0})
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

    payload = dict(name=name, expect_pass=expect_pass, runner_exit=proc.returncode,
                   runner_result=result, security_evidence=security_evidence,
                   checks=checks, passed=all(c['pass'] for c in checks),
                   runner_stderr=proc.stderr[-1500:])
    (work / (name + '.json')).write_text(json.dumps(payload, indent=2) + '\n', encoding='utf-8')
    print(('PASS ' if payload['passed'] else 'FAIL ') + name +
          ' runner_pass=%s errors=%s' % (result.get('pass'), result.get('errors')))
    return payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--fastdds-prefix', type=Path, required=True)
    parser.add_argument('--off-fastdds-prefix', type=Path, required=True)
    parser.add_argument('--ros-prefix', type=Path, required=True)
    parser.add_argument('--pixi-prefix', type=Path, required=True)
    parser.add_argument('--keystore', type=Path, required=True)
    parser.add_argument('--run-id', default='0123456789abcdef')
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--only', default=None)
    args = parser.parse_args()

    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
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
            expect_stderr=[], **common))

    if want('03'):
        # Mixed dependency: the ON application and RMW with the SECURITY=OFF
        # fastrtps. The refusal happens in the Windows loader, which cannot
        # resolve the ON import table against the OFF binary, so the process
        # exits with STATUS_ENTRYPOINT_NOT_FOUND before any project code runs.
        # This case therefore shows that the mix is refused; it does NOT exercise
        # the project's own build-mode gate, and no gate marker is expected here.
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

    summary = dict(cases_total=len(results), cases_passed=sum(1 for x in results if x['passed']))
    (work / 'ROS_SECURITY_CLOSURE.json').write_text(json.dumps(summary, indent=2) + '\n',
                                                    encoding='utf-8')
    print(json.dumps(summary))
    return 0 if summary['cases_passed'] == summary['cases_total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
