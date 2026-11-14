"""Real independent asyncua <-> DDS->UA gateway <-> native DDS acceptance, both ABIs."""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time

import psutil
from build_dds_ua_gateway import ROOT, sha, snapshot, verify_client


def demand(ok, message):
    if not ok:
        raise RuntimeError(message)


def text(path):
    return path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def validate(bundle):
    summary = json.loads(Path(bundle['build_summary']).read_text(encoding='utf-8'))
    demand(summary['status'] == 'PASS' and summary['bundle'] == bundle, 'build bundle mismatch')
    demand(summary['source_snapshot'] == snapshot(), 'built source differs from current tree')
    demand(len(summary['commands']) == 4 and all(c['exit_code'] == 0 for c in summary['commands']), 'build command gate')
    demand(sha(summary['dependency_summary']) == summary['dependency_summary_sha256'], 'dependency summary changed')
    dependency = json.loads(Path(summary['dependency_summary']).read_text(encoding='utf-8'))
    demand(dependency['status'] == 'PASS' and all(c['exit_code'] == 0 for c in dependency['commands']), 'dependency failed')
    for p, digest in {**summary['artifact_sha256'], **dependency['open62541']['source_sha256']}.items():
        demand(sha(p) == digest, 'artifact/upstream changed: ' + p)
    demand(sha(bundle['client_manifest']) == summary['client_manifest_sha256'], 'client manifest changed')
    client = verify_client(bundle['client_manifest'])
    demand(client['python'] == bundle['client_python'], 'client interpreter mismatch')
    return summary


class Case:
    def __init__(self, path, bundle, domain, rep):
        self.path, self.bundle, self.domain, self.rep = path, bundle, domain, rep
        path.mkdir()
        self.control = path / 'control'
        self.control.mkdir()
        self.processes, self.modules = [], []
        self.env = os.environ.copy()
        self.env['PATH'] = os.pathsep.join(bundle['runtime_dirs']) + os.pathsep + self.env.get('PATH', '')
        self.port, self.serial = free_port(), 0
        self.topic = 'D2_' + path.name.replace('-', '_').replace('.', '_') + '_' + str(time.time_ns())

    def start(self, name, argv, stop=None, log=None):
        log = log or self.path / (name + '.log')
        stream = log.open('wb')
        process = subprocess.Popen(list(map(str, argv)), cwd=ROOT, env=self.env, stdout=stream, stderr=subprocess.STDOUT)
        record = dict(name=name, command=list(map(str, argv)), pid=process.pid, log=str(log),
                      stop=str(stop) if stop else None, forced_cleanup=False)
        self.processes.append((process, stream, record))
        return process, log

    def wait(self, predicate, what, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.02)
        raise RuntimeError(what + ' timed out')

    def ended(self, process, expected, timeout=12):
        code = process.wait(timeout=timeout)
        demand(code == expected, f'child exit {code}, expected {expected}')
        next(record for p, _, record in self.processes if p == process)['expected_exit'] = expected

    def audit(self, process, name, abi=None):
        modules = []
        for item in psutil.Process(process.pid).memory_maps():
            file = Path(item.path)
            lower = file.name.lower()
            if lower.startswith(('fastdds', 'fastrtps', 'fastcdr', 'foonathan')) and file.suffix.lower() == '.dll':
                demand(abi is not None, 'independent UA client loaded DDS')
                demand(file.resolve().parent in {Path(p).resolve() for p in self.bundle['runtime_dirs']}, 'undeclared DDS runtime')
                demand(not (abi == '3.6.2' and lower.startswith('fastrtps')) and not (abi == '2.14.5' and lower.startswith('fastdds')), 'opposite DDS ABI')
                modules.append(dict(path=str(file.resolve()), sha256=sha(file)))
        if abi:
            expected = 'fastdds-3.6.dll' if abi == '3.6.2' else 'fastrtps-2.14.dll'
            demand(any(Path(m['path']).name.lower() == expected for m in modules), 'DDS ABI not loaded')
            demand(any(Path(m['path']).name.lower().startswith('fastcdr') for m in modules), 'Fast CDR not loaded')
        self.modules.append(dict(name=name, pid=process.pid, modules=modules, independent_ua=abi is None))

    def peer(self):
        process, log = self.start('peer', [self.bundle['peer'], self.domain, self.topic, self.rep, self.control], self.control / 'stop', self.control / 'peer.log')
        self.wait(lambda: 'D2_PEER_READY' in text(log) or process.poll() is not None, 'DDS peer startup')
        demand(process.poll() is None, 'DDS peer failed startup')
        self.audit(process, 'peer', self.bundle['abi'])
        return process, log

    def gateway(self, name='gateway', **options):
        stop = self.path / (name + '.stop')
        fields = dict(direction='dds_to_ua', domain=self.domain, topic=self.topic, representation=self.rep,
                      port=self.port, max_instances=16, run_ms=60000, stop_file=stop)
        fields.update(options)
        cfg = self.path / (name + '.cfg')
        cfg.write_text(''.join(f'{k}={v}\n' for k, v in fields.items()), encoding='utf-8')
        return self.start(name, [self.bundle['gateway'], cfg], stop)

    def ready(self, process, log):
        self.wait(lambda: 'D2_READY' in text(log) or process.poll() is not None, 'gateway startup')
        demand(process.poll() is None and 'D2_READY' in text(log), 'gateway failed startup')
        self.audit(process, log.stem, self.bundle['abi'])

    def client(self, mode):
        receipt = self.path / ('client-' + mode + '.json')
        process, log = self.start('client-' + mode, [self.bundle['client_python'], '-I', '-B', ROOT / 'scripts/standards/d2_ua_client.py',
                '--url', f'opc.tcp://127.0.0.1:{self.port}', '--domain', self.domain, '--topic', self.topic,
                '--control', self.control, '--receipt', receipt, '--mode', mode])
        self.wait(lambda: 'D2_UA_CONNECTED stack=2.1.0' in text(log) or process.poll() is not None, 'independent UA client session')
        demand(process.poll() is None and 'D2_UA_CONNECTED stack=2.1.0' in text(log), 'UA client failed before module audit')
        self.audit(process, 'client-' + mode)
        # Normal mode can exercise several separately bounded native five-second ACK fences.
        self.ended(process, 0, timeout=45)
        result = json.loads(receipt.read_text(encoding='utf-8'))
        demand(result['status'] == 'PASS' and result['mode'] == mode and 'D2_UA_CLIENT PASS' in text(log), 'independent client gate')
        return result

    def send(self, key):
        self.serial += 1
        pending = self.control / f'command-{self.serial}.pending'
        pending.write_text(f'{self.serial} write {key} 0 - -\n', encoding='ascii')
        pending.replace(self.control / f'command-{self.serial}')

    def stop_gateway(self, process, log):
        record = next(record for p, _, record in self.processes if p == process)
        Path(record['stop']).touch()
        self.ended(process, 0)
        self.check_result(log, 'PASS')

    def check_result(self, log, status):
        lines = text(log).splitlines()
        results = [line for line in lines if line.startswith('D2_RESULT ')]
        demand(len(results) == 1 and f'status={status}' in results[0] and results[0].endswith('ua_owners=0 dds_owners=0'), 'gateway status/ownership result')

    def finish(self):
        # Keep DDS peer alive until gateway ACK/cleanup finishes.
        for process, _, record in sorted(self.processes, key=lambda item: item[2]['name'] == 'peer'):
            if process.poll() is None and record['stop']:
                Path(record['stop']).touch()
            try:
                process.wait(timeout=12)
            except subprocess.TimeoutExpired:
                record['forced_cleanup'] = True
                for child in psutil.Process(process.pid).children(recursive=True):
                    child.kill()
                process.kill()
                process.wait(timeout=5)
            record['exit_code'] = process.returncode
        for _, stream, _ in self.processes:
            stream.close()
        return [record for _, _, record in self.processes]


def run_case(case, kind):
    report = dict(name=case.path.name, kind=kind, status='FAIL')
    error = None
    try:
        if kind in ('normal', 'capacity', 'max-capacity', 'overflow', 'restart'):
            peer, peerlog = case.peer()
        if kind == 'invalid':
            gateway, log = case.gateway(direction='ua_to_dds')
            case.ended(gateway, 2)
            demand('D2_CONFIG_FAIL' in text(log) and 'D2_READY' not in text(log), 'configuration rejection')
        elif kind == 'deadline':
            gateway, log = case.gateway(run_ms=1000)
            case.ended(gateway, 3)
            demand('D2_FAIL D2 run deadline' in text(log), 'explicit deadline failure')
            case.check_result(log, 'FAIL')
        elif kind == 'bind-failure':
            with socket.socket() as blocker:
                blocker.bind(('127.0.0.1', case.port))
                blocker.listen()
                gateway, log = case.gateway()
                case.ended(gateway, 3)
                demand('D2_READY' not in text(log) and 'D2_FAIL' in text(log), 'UA bind failure')
                case.check_result(log, 'FAIL')
        else:
            gateway, log = case.gateway(max_instances=2 if kind in ('capacity', 'overflow') else 64 if kind == 'max-capacity' else 16)
            case.ready(gateway, log)
            if kind in ('normal', 'capacity', 'max-capacity'):
                report['client'] = case.client(kind)
            elif kind == 'restart':
                report['before_restart'] = case.client('restart1')
                case.stop_gateway(gateway, log)
                gateway, log = case.gateway('gateway-restarted')
                case.ready(gateway, log)
                report['after_restart'] = case.client('restart2')
            elif kind == 'overflow':
                for key in (301, 302):
                    case.send(key)
                    case.wait(lambda: f'D2_DDS key={key} valid=1' in text(log), 'bounded DDS instance')
                case.send(303)
                case.ended(gateway, 3)
                demand('D2_FAIL instance capacity exceeded' in text(log), 'external DDS capacity failure')
                case.check_result(log, 'FAIL')
            if kind != 'overflow':
                case.stop_gateway(gateway, log)
        if kind in ('normal', 'capacity', 'max-capacity', 'overflow', 'restart'):
            (case.control / 'stop').touch()
            case.ended(peer, 0)
            demand('D2_PEER_CLOSED exit=0' in text(peerlog), 'native peer cleanup')
        report['status'] = 'PASS'
    except Exception as failure:
        error = failure
        report['error'] = str(failure)
    finally:
        report['processes'] = case.finish()
        report['modules'] = case.modules
        if any(record['forced_cleanup'] or record['exit_code'] != record.get('expected_exit', 0) for record in report['processes']):
            report['status'] = 'FAIL'
            error = error or RuntimeError('unexpected child exit/forced cleanup')
        (case.path / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if error:
        raise RuntimeError(case.path.name + ': ' + str(error))
    return report


def main(args):
    out = args.out.resolve()
    demand(not out.exists() and out.is_relative_to((ROOT / 'build').resolve()), 'use a new workspace build directory')
    bundles = [json.loads(path.read_text(encoding='utf-8'))['bundle'] for path in args.build_summary]
    demand(len(bundles) == 2 and {b['abi'] for b in bundles} == {'3.6.2', '2.14.5'}, 'both ABI bundles required')
    before = [validate(b) for b in bundles]
    out.mkdir(parents=True)
    summary = dict(status='FAIL', cases=[], build_summaries=[str(p.resolve()) for p in args.build_summary])
    try:
        for bundle in bundles:
            domain = 219 if bundle['abi'] == '3.6.2' else 220
            for rep in (0, 2):
                name = bundle['abi'] + '-rep' + str(rep) + '-normal'
                summary['cases'].append(run_case(Case(out / name, bundle, domain, rep), 'normal'))
            for kind in ('capacity', 'max-capacity', 'overflow', 'deadline', 'invalid', 'empty', 'restart', 'bind-failure'):
                name = bundle['abi'] + '-' + kind
                summary['cases'].append(run_case(Case(out / name, bundle, domain, 2), kind))
        demand([validate(b) for b in bundles] == before, 'inputs changed during acceptance')
        summary.update(status='PASS', case_count=len(summary['cases']), source_unchanged=True, toolchains_unchanged=True)
    finally:
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D2_ACCEPTANCE PASS cases=' + str(len(summary['cases'])), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', action='append', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    main(parser.parse_args())
