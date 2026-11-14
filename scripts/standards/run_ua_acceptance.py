"""D1 real UA server -> gateway -> plain public-DDS probe acceptance, both exact ABIs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time

import psutil
from ua_fault_proxy import SetupFaultProxy

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def text(path):
    return path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''


def demand(ok, message):
    if not ok:
        raise RuntimeError(message)


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def config(path, port, domain, topic, rep, stop, items=None, **overrides):
    fields = dict(direction='ua_to_dds', endpoint=f'opc.tcp://127.0.0.1:{port}',
                  domain=domain, topic=topic, representation=rep, queue_capacity=16,
                  request_timeout_ms=500, reconnect_ms=200, retry_limit=50, publish_ms=50,
                  sampling_ms=10, ua_queue_size=16, match_timeout_ms=2000, run_ms=60000,
                  stop_file=stop)
    fields.update(overrides)
    content = ''.join(f'{k}={v}\n' for k, v in fields.items())
    content += ''.join(f'item={item}\n' for item in (items or ['1;1001;42;sensorA', '1;1002;43;sensorB']))
    path.write_text(content, encoding='utf-8')


class Case:
    def __init__(self, path, bundle, domain, rep):
        self.path, self.bundle, self.domain, self.rep = path, bundle, domain, rep
        path.mkdir()
        self.processes = []
        self.modules = []
        self.port = free_port()
        self.env = os.environ.copy()
        self.env['PATH'] = os.pathsep.join(bundle['runtime_dirs']) + os.pathsep + self.env.get('PATH', '')
        self.topic = 'd1_' + path.name.replace('-', '_').replace('.', '_')
        self.stop = path / 'gateway.stop'
        self.probe_stop = path / 'probe.stop'

    def start(self, name, argv, stop):
        log = self.path / (name + '.log')
        stream = log.open('wb')
        process = subprocess.Popen(list(map(str, argv)), cwd=ROOT, env=self.env,
                                   stdout=stream, stderr=subprocess.STDOUT)
        record = dict(name=name, command=list(map(str, argv)), pid=process.pid,
                      log=str(log), stop=str(stop), forced_cleanup=False)
        self.processes.append((process, stream, record))
        return process, log

    def wait(self, predicate, what, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.02)
        raise RuntimeError(what + ' timed out')

    def ended(self, process, expected, what):
        code = process.wait(timeout=10)
        demand(code == expected, f'{what} exit {code}, expected {expected}')

    def audit_modules(self, process, name, abi=None):
        modules = []
        for mapping in psutil.Process(process.pid).memory_maps():
            file = Path(mapping.path)
            lower = file.name.lower()
            if lower.startswith(('fastdds', 'fastrtps', 'fastcdr', 'foonathan')) and file.suffix.lower() == '.dll':
                demand(abi is not None, 'independent UA server loaded a DDS module')
                allowed = {Path(p).resolve() for p in self.bundle['runtime_dirs']}
                demand(file.resolve().parent in allowed, 'DDS module from undeclared runtime directory')
                demand(not (abi == '3.6.2' and lower.startswith('fastrtps')) and
                       not (abi == '2.14.5' and lower.startswith('fastdds')), 'opposite DDS ABI loaded')
                modules.append(dict(path=str(file.resolve()), sha256=sha(file)))
        if abi:
            expected = 'fastdds-3.6.dll' if abi == '3.6.2' else 'fastrtps-2.14.dll'
            demand(any(Path(m['path']).name.lower() == expected for m in modules), 'expected DDS ABI not loaded')
            demand(any(Path(m['path']).name.lower().startswith('fastcdr') for m in modules), 'Fast CDR not loaded')
        report = dict(name=name, pid=process.pid, modules=modules, independent_ua=abi is None)
        self.modules.append(report)
        (self.path / (name + '-modules.json')).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def probe(self):
        process, log = self.start('probe', [self.bundle['probe'], self.domain, self.topic,
                                          self.rep, self.probe_stop, 90000], self.probe_stop)
        self.wait(lambda: 'D1_PROBE_READY' in text(log), 'plain DDS probe startup')
        self.audit_modules(process, 'probe', self.bundle['abi'])
        return process, log

    def gateway(self, **options):
        cfg = self.path / 'gateway.cfg'
        config(cfg, self.port, self.domain, self.topic, self.rep, self.stop, **options)
        return self.start('gateway', [self.bundle['gateway'], cfg], self.stop)

    def server(self, phase):
        directory = self.path / ('server-' + str(phase))
        directory.mkdir()
        process, log = self.start('server-' + str(phase), [self.bundle['server'], self.port, directory], directory / 'stop')
        self.wait(lambda: 'D1_UA_SERVER_READY version=1.4.21' in text(log), 'independent UA server startup')
        self.audit_modules(process, 'server-' + str(phase))
        return process, log, directory

    def finish(self, status, extra):
        # Every owned child is collected; forced termination can never yield PASS.
        for _, _, record in self.processes:
            Path(record['stop']).touch()
        for process, stream, record in self.processes:
            try:
                code = process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait(timeout=5)
                record['forced_cleanup'] = True
                status = 'FAIL'
            stream.close()
            record['exit_code'] = code
        report = dict(status=status, abi=self.bundle['abi'], rep=self.rep,
                      processes=[r for _, _, r in self.processes], module_reports=self.modules, **extra)
        (self.path / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        return report


def data_case(case):
    probe, probe_log = case.probe()
    gateway, gateway_log = case.gateway()
    case.wait(lambda: 'D1_CONNECT_FAILED' in text(gateway_log), 'initial unavailable server observed')
    case.audit_modules(gateway, 'gateway', case.bundle['abi'])
    expected = {(42, 7), (43, 8)}

    def seen():
        found = set()
        for key, value, label, payload in re.findall(r'D1_SAMPLE key=(\d+) value=(\d+) label=([0-9a-f]*) payload_size=(\d+)', text(probe_log)):
            key, value = int(key), int(value)
            demand(key in (42, 43) and label == ('sensorA' if key == 42 else 'sensorB').encode().hex()
                   and payload == '0', 'plain DDS probe observed invalid key/label/payload')
            demand((key, value) in expected, 'unexpected DDS value')
            found.add((key, value))
        return found

    for phase in (1, 2):
        server, server_log, directory = case.server(phase)
        case.wait(lambda: f'D1_SUBSCRIBED generation={phase} ' in text(gateway_log), 'new subscription generation')
        case.wait(lambda: {(42, 7), (43, 8)} <= seen(), 'initial monitored values')
        values = [(1001, 0), (1002, 4294967295), (1001, 42), (1002, 123456789)] if phase == 1 else [
            (1001, 100), (1002, 4294967294), (1001, 101), (1002, 200)]
        for serial, (node, value) in enumerate(values, 1):
            key = 42 if node == 1001 else 43
            expected.add((key, value))
            pending = directory / f'command-{serial}.pending'
            pending.write_text(f'{serial} {node} {value}\n', encoding='ascii')
            pending.replace(directory / f'command-{serial}')
            case.wait(lambda: f'D1_UA_WRITE serial={serial} node={node} value={value}' in text(server_log), 'one UA source write')
            case.wait(lambda: (key, value) in seen(), 'exact UA source value at plain DDS probe')
        if phase == 1:
            retry_before = text(gateway_log).count('D1_STATE RETRY_WAIT')
            (directory / 'stop').touch()
            case.ended(server, 0, 'first UA server normal shutdown')
            case.wait(lambda: text(gateway_log).count('D1_STATE RETRY_WAIT') > retry_before, 'live UA disconnect and reconnect state')
        else:
            case.stop.touch()
            case.ended(gateway, 0, 'gateway graceful connected shutdown')
            demand('D1_DDS_ACK code=0 ' in text(gateway_log), 'successful native DDS acknowledgment gate')
            result = re.search(r'D1_RESULT status=PASS reason=none accepted=(\d+) published=(\d+) discarded=0 peak=(\d+) connections=2 attempts=(\d+) ua_owners=0 dds_owners=0 sample_owners=0', text(gateway_log))
            demand(result is not None, 'gateway terminal counters/ownership')
            accepted, published, peak, attempts = map(int, result.groups())
            demand(accepted == published and accepted >= 12 and 0 < peak <= 16 and attempts >= 3, 'queue/count/reconnect invariants')
            case.wait(lambda: len(re.findall(r'D1_SAMPLE ', text(probe_log))) == published, 'every accepted notification consumed at DDS')
            demand(expected <= seen(), 'complete source value set')
            case.probe_stop.touch()
            case.ended(probe, 0, 'DDS probe shutdown')
            (directory / 'stop').touch()
            case.ended(server, 0, 'second UA server normal shutdown')
    return dict(expected_distinct_values=len(expected), accepted=accepted, published=published,
                observed_samples=len(re.findall(r'D1_SAMPLE ', text(probe_log))), reconnect_generations=2,
                source_writes=8, initial_server_unavailable_observed=True)


def negative_case(case, scenario):
    probe = None
    if scenario != 'dds-unmatched':
        probe, probe_log = case.probe()
    options = {}
    expected = {'missing-node': 'ua_setup', 'wrong-type': 'ua_type', 'bad-quality': 'ua_quality',
                'retry-limit': 'retry_limit', 'deadline': 'deadline', 'dds-unmatched': 'dds_operation'}
    if scenario in ('missing-node', 'wrong-type', 'bad-quality'):
        node = {'missing-node': 9999, 'wrong-type': 2001, 'bad-quality': 2002}[scenario]
        options['items'] = [f'1;{node};42;sensorA']
        server, _, directory = case.server(1)
    if scenario == 'retry-limit':
        options['retry_limit'] = 2
    if scenario == 'deadline':
        options['run_ms'] = 1000
    if scenario == 'dds-unmatched':
        options['match_timeout_ms'] = 300
    gateway, gateway_log = case.gateway(**options)
    if scenario == 'stop-unavailable':
        case.wait(lambda: 'D1_CONNECT_FAILED' in text(gateway_log), 'unavailable stop precondition')
        case.stop.touch()
        case.ended(gateway, 0, 'shutdown while disconnected')
        demand('D1_RESULT status=PASS reason=none accepted=0 published=0 discarded=0' in text(gateway_log), 'disconnected shutdown data counters')
    else:
        case.ended(gateway, 3, 'explicit rejection ' + scenario)
        demand(f'D1_RESULT status=FAIL reason={expected[scenario]} ' in text(gateway_log), 'correct explicit failure attribution')
    demand('ua_owners=0 dds_owners=0 sample_owners=0' in text(gateway_log), 'safe local shutdown ownership')
    if probe:
        demand('D1_SAMPLE ' not in text(probe_log), 'invalid input must not produce DDS samples')
        case.probe_stop.touch()
        case.ended(probe, 0, 'negative probe normal shutdown')
    if scenario in ('missing-node', 'wrong-type', 'bad-quality'):
        (directory / 'stop').touch()
        case.ended(server, 0, 'negative UA fixture shutdown')
    return dict(expected_gateway_exit=0 if scenario == 'stop-unavailable' else 3,
                observed_invalid_dds_samples=0, scenario=scenario)


def setup_fault_case(case, scenario):
    service = 787 if scenario == 'fault-subscription' else 751
    server, _, directory = case.server(1)
    probe, probe_log = case.probe()
    proxy = SetupFaultProxy(case.port, service)
    try:
        gateway, log = case.gateway(endpoint=f'opc.tcp://127.0.0.1:{proxy.port}')
        case.wait(lambda: proxy.drops == 1, 'one setup-window TCP fault')
        case.wait(lambda: 'D1_SUBSCRIBED generation=1 ' in text(log), 'complete fresh setup after transient fault')
        case.wait(lambda: 'D1_SAMPLE key=42 value=7 ' in text(probe_log) and
                  'D1_SAMPLE key=43 value=8 ' in text(probe_log), 'actual DDS delivery after setup recovery')
        case.audit_modules(gateway, 'gateway', case.bundle['abi'])
        case.stop.touch()
        case.ended(gateway, 0, 'recovered gateway graceful shutdown')
        result = re.search(r'D1_RESULT status=PASS reason=none accepted=(\d+) published=(\d+) discarded=0 peak=(\d+) connections=1 attempts=(\d+) ua_owners=0 dds_owners=0 sample_owners=0', text(log))
        demand(result is not None and 'D1_SETUP_FAILED ' in text(log) and ' retry=1' in text(log)
               and 'D1_DDS_ACK code=0 ' in text(log), 'setup retry and shutdown ownership/ACK gates')
        accepted, published, peak, attempts = map(int, result.groups())
        demand(accepted == published and accepted >= 2 and 0 < peak <= 16 and attempts >= 2, 'recovered queue/count invariants')
        case.wait(lambda: text(probe_log).count('D1_SAMPLE ') == published, 'all recovered notifications observed')
        samples = re.findall(r'D1_SAMPLE key=(\d+) value=(\d+) label=([0-9a-f]*) payload_size=(\d+)', text(probe_log))
        demand(len(samples) == published and all((k, v, label, payload) in (
            ('42', '7', b'sensorA'.hex(), '0'), ('43', '8', b'sensorB'.hex(), '0'))
            for k, v, label, payload in samples), 'exact recovered data mapping')
        case.probe_stop.touch()
        case.ended(probe, 0, 'recovered probe shutdown')
        (directory / 'stop').touch()
        case.ended(server, 0, 'healthy independent server shutdown')
    finally:
        report = proxy.close()
        (case.path / 'fault-proxy.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    demand(report['injected_drops'] == 1 and report['connections'] >= 2 and
           report['threads_closed'] and not report['errors'], 'fault fixture completed and closed')
    return dict(scenario=scenario, accepted=accepted, published=published, observed_samples=len(samples),
                attempts=attempts, fault_service=service, injected_drops=1, recovered=True)


def run(args):
    out = args.out.resolve()
    demand(os.name == 'nt' and not out.exists() and out.is_relative_to((ROOT / 'build').resolve()), 'use a new Windows build evidence directory')
    bundles = json.loads(args.bundles.read_text(encoding='utf-8-sig'))
    demand(len(bundles) == 2 and {b['abi'] for b in bundles} == {'2.14.5', '3.6.2'}, 'both exact ABI bundles required')
    hashes, source = {}, {}
    for bundle in bundles:
        build = json.loads(Path(bundle['build_summary']).read_text(encoding='utf-8-sig'))
        demand(build['status'] == 'PASS' and build['bundle'] == bundle and build['source_unchanged'], 'build provenance')
        hashes.update(build['artifact_sha256'])
        hashes.update(build['open62541']['source_sha256'])
        for path, digest in build['source_snapshot'].items():
            demand(path not in source or source[path] == digest, 'both ABIs use identical source')
            source[path] = digest
    demand(all(sha(path) == digest for path, digest in hashes.items()), 'input artifact/upstream source changed')
    demand(all(sha(ROOT / path) == digest for path, digest in source.items()), 'input gateway source changed')
    out.mkdir(parents=True)
    summary = dict(status='FAIL', cases=[], source_snapshot=source)
    try:
        for index, bundle in enumerate(bundles):
            for scenario, rep in [('data', 0), ('data', 2), *[(s, 0) for s in (
                    'missing-node', 'wrong-type', 'bad-quality', 'retry-limit', 'deadline', 'stop-unavailable', 'dds-unmatched',
                    'fault-subscription', 'fault-monitor')]]:
                case = Case(out / f'{bundle["abi"]}-{scenario}-rep{rep}', bundle, 219 + index, rep)
                status, extra = 'FAIL', {}
                try:
                    if scenario == 'data':
                        extra = data_case(case)
                    elif scenario.startswith('fault-'):
                        extra = setup_fault_case(case, scenario)
                    else:
                        extra = negative_case(case, scenario)
                    status = 'PASS'
                except Exception as error:
                    extra['error'] = str(error)
                    raise
                finally:
                    receipt = case.finish(status, extra)
                    summary['cases'].append(receipt)
                    if receipt['status'] != 'PASS' and status == 'PASS':
                        raise RuntimeError('forced process cleanup is FAIL')
        demand(len(summary['cases']) == 22, 'complete 22-case census')
        demand(all(sha(path) == digest for path, digest in hashes.items()), 'artifact/upstream hash drift during acceptance')
        demand(all(sha(ROOT / path) == digest for path, digest in source.items()), 'source drift during acceptance')
        summary.update(status='PASS', source_unchanged=True, artifact_unchanged=True,
                       data_lanes=4, negative_and_shutdown_cases=14, setup_fault_recovery_cases=4)
    finally:
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D1_ACCEPTANCE PASS cases=22 data_lanes=4 setup_fault_lanes=4', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundles', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    try:
        run(parser.parse_args())
    except Exception as error:
        parser.exit(1, str(error) + '\n')
