"""One bounded, no-node project Python slice diagnostic on a selected RK3588A.

Only writes a fresh remote directory. No shared deployment, service, identity,
network, permission, token, ROS Node or daemon changes. Output must be fresh.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import secrets
import shlex
import subprocess
import tarfile
import tempfile
import time

TARGETS = {'A': '3e01ff55454d202020104033bf453b00',
           'B': '3e01ff55454d202020104433991c3b00'}


def project_run_record(argv, output, rows, label, timeout=75):
    row = {'argv': argv, 'label': label, 'started_utc': datetime.now(timezone.utc).isoformat(),
           'host_exit': None, 'timeout_seconds': timeout, 'timed_out': False}
    start = time.monotonic()
    try:
        done = subprocess.run(argv, capture_output=True, timeout=timeout)
        row.update(host_exit=done.returncode, stdout=done.stdout.decode('utf-8', 'replace'),
                   stderr=done.stderr.decode('utf-8', 'replace'))
    except subprocess.TimeoutExpired as error:
        row.update(timed_out=True, stdout=(error.stdout or b'').decode('utf-8', 'replace'),
                   stderr=(error.stderr or b'').decode('utf-8', 'replace'))
    except OSError as error:
        row.update(stdout='', stderr=str(error))
    row.update(ended_utc=datetime.now(timezone.utc).isoformat(), duration_seconds=time.monotonic() - start)
    rows.append(row)
    (output / 'commands.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
    return row


def project_run_shell(hdc, target, script, output, rows, label):
    marker = 'R24_EXIT_' + secrets.token_hex(16)
    command = 'timeout -s KILL 60 sh -c ' + shlex.quote(script)
    command += '; rc=$?; printf "\\n' + marker + '=%s\\n" "$rc"'
    row = project_run_record([hdc, '-t', target, 'shell', command], output, rows, label)
    exits = re.findall('^' + marker + r'=(\d+)\r?$', row['stdout'], re.M)
    row['remote_exit'] = int(exits[0]) if len(exits) == 1 else None
    row['remote_completed'] = row['host_exit'] == 0 and row['remote_exit'] not in (None, 124, 137, 143)
    (output / 'commands.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
    return row


def project_run_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--side', choices=tuple(TARGETS), required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hdc', default='C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rows, remote, archive_root, archive_path = [], None, None, None
    archive_parent = Path('C:/Users/17715/AppData/Local/Temp').resolve()
    report = {'status': 'BLOCKED', 'qualification': 'NOT_EXECUTED', 'infrastructure': 'PASS',
              'scope': 'PROJECT_LINKED_PYTHON_ABI_NO_NODE', 'device': TARGETS[args.side],
              'dsoftbus_communication_verified': False, 'cleanup': 'NOT_CREATED', 'errors': []}
    target = TARGETS[args.side]
    try:
        if args.package.is_symlink() or (args.package / 'PACKAGE.json').is_symlink():
            raise ValueError('Canonical package/manifest must not be a symlink')
        package = json.loads((args.package / 'PACKAGE.json').read_text())
        files = package['files']
        entries = list(args.package.rglob('*'))
        if any(p.is_symlink() or not (p.is_file() or p.is_dir()) for p in entries):
            raise ValueError('Package must contain only regular files/directories')
        for name, row in files.items():
            relative = PurePosixPath(name)
            if (relative.is_absolute() or str(relative) != name or '..' in relative.parts
                    or any(char in name for char in ('\\', ':', '\r', '\n', '\0'))):
                raise ValueError('Noncanonical package member: ' + name)
            path = (args.package / name).resolve()
            if not path.is_relative_to(args.package.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
                raise ValueError('Package input changed/escaped: ' + name)
        actual = {p.relative_to(args.package).as_posix() for p in entries if p.is_file()}
        if actual != set(files) | {'PACKAGE.json'}:
            raise ValueError('Unrecorded/missing package files')
        report['package_sha256'] = hashlib.sha256((args.package / 'PACKAGE.json').read_bytes()).hexdigest()
        # HDC on Windows cannot open some otherwise valid long source paths.
        # One short local archive preserves every canonical member and its name.
        archive_root = Path(tempfile.mkdtemp(prefix='r24-', dir=archive_parent)).resolve()
        archive_path = archive_root / 'p.tar'
        archive_files = dict(files, **{'PACKAGE.json': {'sha256': report['package_sha256']}})
        with tarfile.open(archive_path, mode='w', format=tarfile.USTAR_FORMAT) as archive:
            for name, row in sorted(archive_files.items()):
                source = args.package / name
                if source.is_symlink() or not source.is_file():
                    raise ValueError('Archive input is not a regular file: ' + name)
                data = source.read_bytes()
                if hashlib.sha256(data).hexdigest() != row['sha256']:
                    raise ValueError('Package changed during archive creation: ' + name)
                member = tarfile.TarInfo(name)
                member.size, member.mode, member.mtime = len(data), 0o644, 0
                member.uid = member.gid = 0
                member.uname = member.gname = ''
                member.type = tarfile.REGTYPE
                # USTAR raises if a name is unrepresentable; never truncate it.
                member.tobuf(format=tarfile.USTAR_FORMAT)
                archive.addfile(member, io.BytesIO(data))
        report['archive'] = {'sha256': hashlib.sha256(archive_path.read_bytes()).hexdigest(),
                             'bytes': archive_path.stat().st_size, 'members': len(archive_files),
                             'format': 'USTAR_REGULAR_FILES_ONLY', 'local_path': str(archive_path)}
        listed = project_run_record([args.hdc, 'list', 'targets', '-v'], args.output, rows, 'targets')
        if listed['host_exit'] != 0 or target not in listed['stdout']:
            raise RuntimeError('Requested board not available')
        made = project_run_shell(args.hdc, target, 'mktemp -d /data/local/tmp/r24-python.XXXXXXXXXX', args.output, rows, 'mkdir')
        candidates = [line.strip() for line in made['stdout'].splitlines()
                      if re.fullmatch(r'/data/local/tmp/r24-python\.[A-Za-z0-9]{10}', line.strip())]
        if made['remote_exit'] != 0 or not made['remote_completed'] or len(candidates) != 1:
            raise RuntimeError('Cannot confirm fresh remote directory')
        remote = candidates[0]
        report['remote_directory'] = remote
        available = project_run_shell(args.hdc, target, 'command -v tar', args.output, rows, 'tar_available')
        if available['remote_exit'] != 0 or not available['remote_completed']:
            raise RuntimeError('Platform tar is unavailable')
        sent = project_run_record([args.hdc, '-t', target, 'file', 'send', str(archive_path), remote + '/p.tar'], args.output, rows, 'send_archive')
        if sent['host_exit'] != 0 or '[Fail]' in sent['stdout'] + sent['stderr']:
            raise RuntimeError('Package transfer failed')
        archive_check = project_run_shell(args.hdc, target,
            '[ "$(sha256sum ' + remote + '/p.tar | cut -d " " -f1)" = ' + report['archive']['sha256'] + ' ]',
            args.output, rows, 'verify_archive')
        if archive_check['remote_exit'] != 0 or not archive_check['remote_completed']:
            raise RuntimeError('Remote archive digest mismatch')
        extracted = project_run_shell(args.hdc, target,
            'mkdir ' + remote + '/slice && tar -xf ' + remote + '/p.tar -C ' + remote + '/slice',
            args.output, rows, 'extract_archive')
        if extracted['remote_exit'] != 0 or not extracted['remote_completed']:
            raise RuntimeError('Owned-directory archive extraction failed')
        checks = []
        checks.append('[ "$(find ' + remote + '/slice -type f | wc -l)" -eq ' + str(len(archive_files)) + ' ]')
        checks.append('[ -z "$(find ' + remote + '/slice -type l -print)" ]')
        for name, row in dict(files, **{'PACKAGE.json': {'sha256': report['package_sha256']}}).items():
            path = shlex.quote(remote + '/slice/' + name)
            checks.append('[ -f ' + path + ' ] && [ ! -L ' + path + ' ] && [ "$(sha256sum ' + path + ' | cut -d " " -f1)" = ' + row['sha256'] + ' ]')
        # Avoid a huge hdc argv: host produces an owned check script, transferred
        # with a separate digest, then executes it using the platform shell.
        checkfile = args.output / 'verify.sh'
        checkfile.write_bytes(('set -e\n' + '\n'.join(checks) + '\n').encode())
        sent = project_run_record([args.hdc, '-t', target, 'file', 'send', str(checkfile), remote + '/verify.sh'], args.output, rows, 'send_verifier')
        if sent['host_exit'] != 0:
            raise RuntimeError('Verifier transfer failed')
        verifier_sha = hashlib.sha256(checkfile.read_bytes()).hexdigest()
        verified = project_run_shell(args.hdc, target,
            '[ "$(sha256sum ' + remote + '/verify.sh | cut -d " " -f1)" = ' + verifier_sha + ' ] && sh ' + remote + '/verify.sh',
            args.output, rows, 'verify_package')
        if verified['remote_exit'] != 0 or not verified['remote_completed']:
            raise RuntimeError('Remote package digest verification failed')
        for mode in ('probe', 'help', 'pkg_prefix', 'interface'):
            command = 'sh ' + remote + '/slice/run ' + mode
            result = project_run_shell(args.hdc, target, command + ' >' + remote + '/' + mode + '.stdout 2>' + remote + '/' + mode + '.stderr', args.output, rows, mode)
            if result['remote_completed'] and result['remote_exit'] not in (0, 70):
                report['qualification'] = 'FAIL'
            elif not result['remote_completed'] or result['remote_exit'] == 70:
                report['infrastructure'] = 'BLOCKED'
            texts = {}
            for stream in ('stdout', 'stderr'):
                dest = args.output / (mode + '.' + stream)
                got = project_run_record([args.hdc, '-t', target, 'file', 'recv', remote + '/' + mode + '.' + stream, str(dest)], args.output, rows, mode + '_recv_' + stream)
                if got['host_exit'] != 0 or not dest.is_file() or '[Fail]' in got['stdout'] + got['stderr']:
                    report['infrastructure'] = 'BLOCKED'
                    raise RuntimeError('Evidence retrieval failed')
                texts[stream] = dest.read_text(encoding='utf-8', errors='replace')
            if result['remote_exit'] != 0 or not result['remote_completed']:
                raise RuntimeError(mode + ': remote or infrastructure failure')
            report['qualification'] = 'FAIL'
            if texts['stderr'].strip() or re.search(r'failed to load|error loading|traceback', texts['stdout'], re.I):
                raise RuntimeError(mode + ': diagnostic output')
            if mode == 'probe' and json.loads(texts['stdout']).get('status') != 'PASS':
                raise RuntimeError('Project Python ABI probe failed')
            if mode == 'help' and ('ros2 is an extensible command-line tool for ROS 2.' not in texts['stdout'] or
                    not all(re.search(r'^  ' + name + r'[ \t]+\S', texts['stdout'], re.M) for name in ('pkg', 'interface'))):
                raise RuntimeError('CLI identity/selected command listings missing')
            if mode == 'pkg_prefix' and texts['stdout'].strip() != remote + '/slice':
                raise RuntimeError('rclpy ament resource escaped project prefix')
            if mode == 'interface' and not re.search(r'^string data\s*$', texts['stdout'], re.M):
                raise RuntimeError('String interface definition missing')
            report['qualification'] = 'INCOMPLETE'
        report['qualification'] = 'PASS'
    except Exception as error:
        report['errors'].append(repr(error))
        if report['qualification'] != 'FAIL':
            report['infrastructure'] = 'BLOCKED'
    finally:
        if remote:
            # Absolute directory was returned by this run's mktemp and checked
            # against the exact fixed prefix. Never clean any shared runtime.
            cleanup = project_run_shell(args.hdc, target,
                '[ -d ' + remote + ' ] && [ ! -L ' + remote + ' ] && rm -rf ' + remote,
                args.output, rows, 'cleanup_owned_directory')
            report['cleanup'] = 'PASS' if cleanup['remote_completed'] and cleanup['remote_exit'] == 0 else 'BLOCKED'
            if report['cleanup'] != 'PASS':
                report['infrastructure'] = 'BLOCKED'
        if archive_root:
            try:
                if (archive_root.parent != archive_parent
                        or not archive_root.name.startswith('r24-')
                        or archive_root.is_symlink() or archive_path != archive_root / 'p.tar'):
                    raise ValueError('Local archive cleanup ownership mismatch')
                archive_path.unlink(missing_ok=True)
                archive_root.rmdir()
                report['local_archive_cleanup'] = 'PASS'
            except Exception as error:
                report['local_archive_cleanup'] = 'BLOCKED'
                report['infrastructure'] = 'BLOCKED'
                report['errors'].append('Local archive cleanup: ' + repr(error))
        report['status'] = ('BLOCKED' if report['infrastructure'] == 'BLOCKED' else
                            'PASS' if report['qualification'] == 'PASS' else 'FAIL')
        (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(project_run_main())
