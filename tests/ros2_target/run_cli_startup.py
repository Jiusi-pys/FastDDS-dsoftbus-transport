"""Qualify three fixed no-node CLI commands using the existing RK3588A deployment.

Requires --inventory containing the reviewed existing-entry, existing-environment,
and existing-bootstrap snapshots (A-ros2/A-env.sh/A-sitecustomize.py, likewise B).
Only a fresh /data/local/tmp/r23-cli.* directory is written and removed.
PASS covers native CLI startup with the existing UDPv4 profile, never DSoftBus.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import secrets
import shlex
import subprocess
import time

TARGETS = {'A': '3e01ff55454d202020104033bf453b00',
           'B': '3e01ff55454d202020104433991c3b00'}
ENV = '/data/local/tmp/ros2-generic/env.sh'
BOOTSTRAP = '/data/local/tmp/ros2-core-config/python-16920f1f6460d75a65be821e3ddac34cdc5f2a01b65e427414c7f7118adc438d/sitecustomize.py'
PYTHON = '/data/python312-rk3588a-verify-c646982b346f/usr/bin/python3.12'
COMMANDS = [('help', ['/usr/local/bin/ros2', '--help']),
            ('pkg_prefix', ['/usr/local/bin/ros2', 'pkg', 'prefix', 'rclpy']),
            ('interface', ['/usr/local/bin/ros2', 'interface', 'show', 'std_msgs/msg/String'])]


def cli_startup_record(argv, records, output, label, timeout=60):
    row = {'label': label, 'argv': argv,
           'started_utc': datetime.now(timezone.utc).isoformat(), 'timeout_seconds': timeout,
           'host_exit': None, 'timed_out': False}
    start = time.monotonic()
    try:
        child = subprocess.run(argv, capture_output=True, timeout=timeout)
        row.update(host_exit=child.returncode, stdout=child.stdout.decode('utf-8', 'replace'),
                   stderr=child.stderr.decode('utf-8', 'replace'))
    except subprocess.TimeoutExpired as error:
        row.update(timed_out=True, stdout=(error.stdout or b'').decode('utf-8', 'replace'),
                   stderr=(error.stderr or b'').decode('utf-8', 'replace'))
    except OSError as error:
        row.update(stdout='', stderr=str(error))
    row.update(ended_utc=datetime.now(timezone.utc).isoformat(),
               duration_seconds=time.monotonic() - start)
    records.append(row)
    (output / 'commands.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    return row


def cli_startup_remote(hdc, target, script, records, output, label):
    marker = 'R23_EXIT_' + secrets.token_hex(16)
    command = 'timeout -s KILL 45 sh -c ' + shlex.quote(script)
    command += '; result=$?; printf "\\n' + marker + '=%s\\n" "$result"'
    row = cli_startup_record([hdc, '-t', target, 'shell', command], records, output, label)
    matches = re.findall(r'^' + marker + r'=(\d+)\r?$', row['stdout'], re.M)
    row['remote_exit'] = int(matches[0]) if len(matches) == 1 else None
    row['remote_timeout_or_signal'] = row['remote_exit'] in (124, 137, 143)
    row['remote_completion_confirmed'] = row['remote_exit'] is not None and not row['remote_timeout_or_signal']
    (output / 'commands.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    if row['host_exit'] != 0 or row['remote_exit'] != 0:
        raise RuntimeError(label + ': host/remote failure or missing completion marker')
    return row


def cli_startup_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hdc', default='C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe')
    parser.add_argument('--side', choices=tuple(TARGETS), required=True)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='Fresh host evidence directory')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    records, remote, collected = [], None, []
    target = TARGETS[args.side]
    report = {'schema': 1, 'scope': 'NATIVE_ROS2_CLI_STARTUP_ONLY', 'target': target,
              'status': 'BLOCKED', 'dsoftbus_communications_verified': False,
              'qualification': 'NOT_EXECUTED', 'infrastructure': 'PASS',
              'stop_condition': 'first failure; no retry; remote 45s and host 60s limits',
              'cleanup': 'NOT_CREATED', 'errors': []}
    probe = Path(__file__).resolve().parents[2] / 'integrations/ros2/target/cli_runtime_probe.py'
    try:
        expected = {}
        for label, relative, filename in (
                ('wrapper', 'existing-entry', args.side + '-ros2'),
                ('environment', 'existing-environment', args.side + '-env.sh'),
                ('bootstrap', 'existing-bootstrap', args.side + '-sitecustomize.py')):
            snapshot = args.inventory / relative / filename
            expected[label] = hashlib.sha256(snapshot.read_bytes()).hexdigest()
        expected['probe'] = hashlib.sha256(probe.read_bytes()).hexdigest()
        report['expected_sha256'] = expected
        report['runner_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        listed = cli_startup_record([args.hdc, 'list', 'targets', '-v'], records, args.output, 'targets')
        if listed['host_exit'] != 0 or target not in listed['stdout']:
            raise RuntimeError('requested board not present in successful target listing')
        # Check exact reviewed files before sourcing anything. No fallback environment.
        checks = []
        for label, filename in (('wrapper', '/usr/local/bin/ros2'), ('environment', ENV), ('bootstrap', BOOTSTRAP)):
            quoted = shlex.quote(filename)
            checks.append('[ -f ' + quoted + ' ] && [ ! -L ' + quoted + ' ] && '
                          '[ "$(sha256sum ' + quoted + ' | cut -d " " -f1)" = ' + expected[label] + ' ]')
        guard = ' && '.join(checks)
        cli_startup_remote(args.hdc, target, guard, records, args.output, 'reviewed_files')
        made = cli_startup_remote(args.hdc, target,
                                  'mktemp -d /data/local/tmp/r23-cli.XXXXXXXXXX', records, args.output, 'mkdir')
        candidates = [line.strip() for line in made['stdout'].splitlines()
                      if re.fullmatch(r'/data/local/tmp/r23-cli\.[A-Za-z0-9]{10}', line.strip())]
        if len(candidates) != 1:
            raise RuntimeError('cannot identify exact fresh directory; no cleanup guessed')
        remote = candidates[0]
        report['remote_directory'] = remote
        sent = cli_startup_record([args.hdc, '-t', target, 'file', 'send', str(probe), remote + '/probe.py'],
                                  records, args.output, 'send_probe')
        if sent['host_exit'] != 0:
            raise RuntimeError('probe transfer failed')
        # The deployed wrapper clears inherited loader/Python paths itself. The
        # standalone probe uses the same reviewed env and no additional paths.
        clean = 'unset PYTHONHOME PYTHONPATH PYTHONUSERBASE LD_PRELOAD LD_LIBRARY_PATH AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH ROS2_HOME; '
        probe_command = clean + '. ' + ENV + ' && exec ' + PYTHON + ' -B ' + remote + '/probe.py'
        tasks = [('probe', None)] + COMMANDS
        for label, argv in tasks:
            outpath, errpath = remote + '/' + label + '.stdout', remote + '/' + label + '.stderr'
            collected.extend([outpath, errpath])
            if label == 'probe':
                payload = 'sh -c ' + shlex.quote(probe_command)
            else:
                payload = shlex.join(argv)
            # Record semantic argv as well as exact shell/hdc argv.
            report.setdefault('remote_commands', []).append({'label': label, 'argv': argv or [PYTHON, '-B', remote + '/probe.py'],
                                                             'shell': payload})
            script = guard + ' && [ "$(sha256sum ' + remote + '/probe.py | cut -d " " -f1)" = ' + expected['probe'] + ' ] || exit 70; '
            script += payload + ' >' + outpath + ' 2>' + errpath
            failure = None
            try:
                cli_startup_remote(args.hdc, target, script, records, args.output, label)
            except RuntimeError as error:
                failure = error
                executed = records[-1]
                if (executed['host_exit'] == 0 and executed['remote_completion_confirmed']
                        and executed['remote_exit'] != 70):
                    report['qualification'] = 'FAIL'
                else:
                    report['infrastructure'] = 'BLOCKED'
            texts = {}
            for stream, path in (('stdout', outpath), ('stderr', errpath)):
                destination = args.output / (label + '.' + stream)
                recv = cli_startup_record([args.hdc, '-t', target, 'file', 'recv', path, str(destination)],
                                          records, args.output, label + '_recv_' + stream)
                if recv['host_exit'] != 0 or not destination.is_file():
                    report['infrastructure'] = 'BLOCKED'
                    raise RuntimeError('cannot collect ' + label + ' ' + stream)
                texts[stream] = destination.read_text(encoding='utf-8', errors='replace')
            if failure:
                raise failure
            # Once complete output exists, failed diagnostics/oracles are an
            # executed qualification failure, independently of later cleanup.
            report['qualification'] = 'FAIL'
            # ros2cli may print plugin load failures yet return zero. Any stderr
            # is left visible and fails this narrow clean-start qualification.
            if texts['stderr'].strip() or re.search(r'failed to load|error loading|traceback', texts['stdout'], re.I):
                raise RuntimeError(label + ': CLI/probe diagnostic output requires investigation')
            if label == 'probe':
                runtime = json.loads(texts['stdout'])
                if runtime.get('status') != 'PASS':
                    raise RuntimeError('runtime probe failed')
                for key, digest in expected.items():
                    if runtime['artifacts'][key].get('sha256') != digest:
                        raise RuntimeError(key + ': probe artifact digest differs')
            elif label == 'help':
                # The reviewed env invokes ros2cli.main through python -c;
                # argparse therefore derives its program name from argv[0]=-c.
                # Require actual ROS help identity and core plugin listings too.
                help_text = texts['stdout']
                if (not re.search(r'^usage: -c \[', help_text, re.M)
                        or 'ros2 is an extensible command-line tool for ROS 2.' not in help_text
                        or not re.search(r'^Commands:\r?$', help_text, re.M)
                        or any(not re.search(r'^  ' + command + r'[ \t]+\S', help_text, re.M)
                               for command in ('pkg', 'interface', 'topic', 'daemon'))):
                    raise RuntimeError('missing reviewed python -c ROS CLI help identity/commands')
            elif label == 'pkg_prefix' and texts['stdout'].strip() != '/data/local/tmp/ros2-generic':
                raise RuntimeError('unexpected rclpy package prefix')
            elif label == 'interface' and not re.search(r'^string data\s*$', texts['stdout'], re.M):
                raise RuntimeError('missing std_msgs/msg/String definition')
            report['qualification'] = 'INCOMPLETE'
        report['qualification'] = 'PASS'
        report['status'] = 'PASS'
    except Exception as error:
        report['errors'].append(repr(error))
        if report['qualification'] != 'FAIL':
            report['infrastructure'] = 'BLOCKED'
    finally:
        if remote:
            try:
                # Delete only known files in the exact mktemp directory, then
                # rmdir. Unexpected contents cause cleanup failure, not rm -r.
                paths = [remote + '/probe.py'] + collected
                script = '[ -d ' + remote + ' ] && [ ! -L ' + remote + ' ] && rm -f ' + shlex.join(paths) + ' && rmdir ' + remote
                cli_startup_remote(args.hdc, target, script, records, args.output, 'cleanup')
                report['cleanup'] = 'PASS'
            except Exception as error:
                report['cleanup'] = 'BLOCKED'
                report['infrastructure'] = 'BLOCKED'
                report['errors'].append('cleanup: ' + repr(error))
        if report['errors']:
            report['status'] = 'BLOCKED' if report['infrastructure'] == 'BLOCKED' else 'FAIL'
        (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(cli_startup_main())
