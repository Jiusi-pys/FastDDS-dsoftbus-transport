"""D4 acceptance entrypoint; independent UA encoding, services and history oracles."""
import argparse
import asyncio
from datetime import datetime, timezone, timedelta
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import socket
import psutil
import build_ua_complete as build

ROOT = build.ROOT


def demand(ok, message):
    if not ok:
        raise RuntimeError(message)


def validate(bundle):
    record = json.loads(Path(bundle['build_summary']).read_text(encoding='utf-8'))
    demand(record['status'] == 'PASS' and record['bundle'] == bundle and record['source_snapshot'] == build.snapshot(), 'D4 build/source mismatch')
    demand(len(record['commands']) == 8 and all(c['exit_code'] == 0 for c in record['commands']), 'D4 build commands')
    for key in ('upstream_sha256', 'generated_sha256', 'ua_header_sha256', 'artifact_sha256'):
        demand(all(build.sha(p) == h for p, h in record[key].items()), 'D4 provenance: ' + key)
    for file, digest in ((record['clang'], record['clang_sha256']), (record['archive'], record['archive_sha256']), (record['client_manifest'], record['client_manifest_sha256'])):
        demand(build.sha(file) == digest, 'D4 toolchain changed')
    build.d2.verify_client(record['client_manifest'])
    return record


def text(path):
    return Path(path).read_text(encoding='utf-8', errors='replace') if Path(path).exists() else ''


class Case:
    def __init__(self, path, bundle, rep=2):
        self.path, self.bundle, self.rep = path, bundle, rep
        path.mkdir()
        self.children, self.modules, self.calls = [], [], []
        self.domain = 219 if bundle['abi'] == '3.6.2' else 220
        self.name = 'D4_' + str(time.time_ns())
        self.indices = {}

    async def wait(self, predicate, label, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            demand(all(p.poll() is None or (r.get('stopped_expected') and p.returncode == 0) for p, _, r in self.children), 'early child exit: ' + label)
            await asyncio.sleep(.01)
        raise RuntimeError('timeout: ' + label)

    def start(self, name, argv, bundle=None, ua=False):
        bundle = bundle or self.bundle
        folder = self.path / name
        folder.mkdir()
        argv = [str(x) for x in argv(folder)]
        env = os.environ.copy()
        env['PATH'] = os.pathsep.join(bundle['runtime_dirs']) + os.pathsep + env.get('PATH', '')
        log = folder / 'process.log'
        stream = log.open('wb')
        p = subprocess.Popen(argv, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        record = dict(name=name, argv=argv, abi=bundle['abi'], ua_only=ua, runtime_dirs=bundle['runtime_dirs'], directory=str(folder), log=str(log), pid=p.pid, forced_cleanup=False)
        self.children.append((p, stream, record))
        self.indices[name] = 0
        return record

    def config(self, folder, port, remote, retention=3000, capacity=8):
        config = folder / 'profile.cfg'
        values = dict(domain=self.domain, rep=self.rep, port=port, run_ms=90000, capacity=capacity, retention_ms=retention,
                      service=self.name, topic=self.name, endpoint='opc.tcp://127.0.0.1:' + str(remote), stop=str(folder / 'stop'))
        config.write_text(''.join(str(k) + '=' + str(v) + '\n' for k, v in values.items()), encoding='utf-8')
        return config

    def audit(self):
        for process, _, record in self.children:
            if process.poll() is not None:
                demand(record.get('stopped_expected') and process.returncode == 0, 'unexpected stopped process during module audit')
                continue
            modules = []
            for item in psutil.Process(process.pid).memory_maps():
                path = Path(item.path)
                if path.suffix.lower() == '.dll' and path.name.lower().startswith(('fastdds', 'fastrtps', 'fastcdr', 'foonathan')):
                    demand(not record['ua_only'], 'independent UA process loaded DDS')
                    demand(path.resolve().parent in {Path(p).resolve() for p in record['runtime_dirs']}, 'undeclared DDS module directory')
                    modules.append(dict(path=str(path.resolve()), sha256=build.sha(path)))
            if not record['ua_only']:
                expected = 'fastdds-3.6.dll' if record['abi'] == '3.6.2' else 'fastrtps-2.14.dll'
                demand(any(Path(m['path']).name.lower() == expected for m in modules), 'expected DDS ABI missing')
                demand(not any(Path(m['path']).name.lower().startswith('fastrtps' if record['abi'] == '3.6.2' else 'fastdds') for m in modules), 'opposite DDS ABI')
            self.modules.append(dict(name=record['name'], modules=modules))

    async def command(self, peer, lines):
        index = self.indices[peer['name']]
        folder = Path(peer['directory'])
        tmp = folder / 'next.tmp'
        tmp.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        tmp.replace(folder / ('request-' + str(index) + '.txt'))
        response = folder / ('response-' + str(index) + '.txt')
        await self.wait(response.exists, 'native response ' + str(index))
        values = response.read_text(encoding='utf-8').splitlines()
        demand(len(values) == 3, 'native response format')
        self.indices[peer['name']] += 1
        self.calls.append(dict(peer=peer['name'], index=index, input=lines, response=values))
        return int(values[0]), bytes.fromhex(values[1]), values[2]

    def finish(self):
        # Native request writers stop first, while request readers remain alive.
        order = lambda r: 0 if r['name'].startswith('peer') else 1 if r['name'].startswith(('service', 'history')) else 2 if r['name'].startswith('fixture') else 3
        for process, stream, record in sorted(self.children, key=lambda x: order(x[2])):
            if process.poll() is None:
                (Path(record['directory']) / 'stop').touch()
            try:
                process.wait(timeout=12)
            except subprocess.TimeoutExpired:
                record['forced_cleanup'] = True
                for child in psutil.Process(process.pid).children(recursive=True):
                    child.kill()
                process.kill()
                process.wait(timeout=5)
            record['exit_code'] = process.returncode
            stream.close()
        return [record for _, _, record in self.children]


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


async def worker(case, mode, peers, endpoint, name='client', retention=2000):
    def argv(folder):
        result = [case.bundle['client_python'], '-B', ROOT / 'scripts/standards/d4_ua_client.py', '--mode', mode,
                  '--endpoint', endpoint, '--directory', folder, '--retention-ms', str(retention)]
        for peer in peers:
            result += ['--peers', peer['directory']]
        return result
    client = case.start(name, argv, ua=True)
    await case.wait(lambda: (Path(client['directory']) / 'ready').exists(), 'independent UA oracle loaded')
    case.audit()
    (Path(client['directory']) / 'go').touch()
    result = Path(client['directory']) / 'result.json'
    await case.wait(lambda: result.exists() and next(p for p, _, r in case.children if r is client).poll() == 0, 'UA semantic oracle finished', timeout=40)
    client['stopped_expected'] = True
    receipt = json.loads(result.read_text(encoding='utf-8'))
    demand(receipt['status'] == 'PASS', 'independent UA semantic oracle')
    for call in receipt['calls']:
        log = text(Path(call['peer']) / 'process.log')
        if mode in ('services', 'capacity'):
            token = call['response'][2]
            marker = 'D4_PEER_SENT index=' + str(call['index']) + ' token=' + token + ' op=' + call['input'][0] + ' payload=' + call['input'][2] + '\n'
            demand(marker in log.replace('\r\n', '\n'), 'native request identity/body witness')
        else:
            fields = call['input']
            marker = f'D4_PEER_PUBLISHED index={call["index"]} key={fields[1]} sequence={fields[2]} source_seconds={fields[3]} source_nanos={fields[4]} label={fields[5]} payload={fields[6]}'
            demand(marker in log, 'native DDS source/payload witness')
    return receipt


async def run_case(case, bundles, mode):
    report = dict(name=case.path.name, mode=mode, status='FAIL')
    failure = None
    try:
        if mode in ('services', 'capacity'):
            upstream = port()
            fixture = case.start('fixture', lambda folder: [case.bundle['client_python'], '-B', ROOT / 'scripts/standards/d4_ua_fixture.py', '--port', upstream, '--directory', folder], ua=True)
            await case.wait(lambda: (Path(fixture['directory']) / 'ready.json').exists(), 'independent UA server startup')
            gateway = case.start('service', lambda folder: [case.bundle['service'], case.config(folder, port(), upstream)])
            other = bundles['2.14.5' if case.bundle['abi'] == '3.6.2' else '3.6.2']
            peers = [case.start('peer' + str(index), lambda folder, b=(case.bundle if index % 2 == 0 else other): [b['peer'], 'rpc', case.domain, case.name, case.rep, folder], bundle=case.bundle if index % 2 == 0 else other)
                     for index in range(5 if mode == 'capacity' else 2)]
            await case.wait(lambda: all('D4_PEER_READY' in text(r['log']) for r in peers) and 'D4_SERVICE_READY' in text(gateway['log']), 'all native DDS endpoint directions')
            oracle = await worker(case, mode, peers, 'opc.tcp://127.0.0.1:' + str(upstream))
            journal = json.loads((Path(fixture['directory']) / 'journal.json').read_text(encoding='utf-8'))
            if mode == 'services':
                wanted = {'Browse', 'BrowseNext', 'TranslateBrowsePathsToNodeIds', 'RegisterNodes', 'UnregisterNodes', 'QueryFirst', 'QueryNext', 'Read', 'Write', 'HistoryRead', 'HistoryUpdate', 'Call'}
                demand(wanted <= {j['service'] for j in journal}, 'all four service sets independently observed')
                demand(len({j['session'] for j in journal if j['service'] == 'QueryNext'}) >= 2, 'independent upstream sessions')
            service_log = text(gateway['log'])
            for call in oracle['calls']:
                token = call['response'][2]
                demand(f'D4_SERVICE_REPLY token={token} op={call["input"][0]} status={call["response"][0]} bytes={len(bytes.fromhex(call["response"][1]))}' in service_log, 'native response identity/status/size witness')
            report.update(rpc_calls=len(oracle['calls']), upstream_services=sorted({j['service'] for j in journal}))
        else:
            ua_port = port()
            retention = 2000 if mode == 'expiry' else 60000
            capacity = 8 if mode == 'expiry' else 4
            gateway = case.start('history0', lambda folder: [case.bundle['history'], case.config(folder, ua_port, port(), retention, capacity)])
            peers = [case.start('peer0', lambda folder: [case.bundle['peer'], 'publish', case.domain, case.name, case.rep, folder])]
            await case.wait(lambda: 'D4_HISTORY_READY' in text(gateway['log']) and 'D4_PEER_READY' in text(peers[0]['log']), 'history DDS/UA ready')
            endpoint = 'opc.tcp://127.0.0.1:' + str(ua_port)
            if mode == 'restart':
                before = await worker(case, 'restart-before', peers, endpoint, 'client-before', retention)
                (Path(gateway['directory']) / 'stop').touch()
                process = next(p for p, _, r in case.children if r is gateway)
                process.wait(timeout=12)
                demand(process.returncode == 0, 'first history shutdown')
                gateway['stopped_expected'] = True
                def current_matches():
                    found = re.findall(r'D4_PEER_MATCH event=(\d+) count=(\d+)', text(peers[0]['log']))
                    return int(found[-1][1]) if found else None
                await case.wait(lambda: current_matches() == 0, 'native writer observed old reader leave')
                gateway = case.start('history1', lambda folder: [case.bundle['history'], case.config(folder, ua_port, port(), retention, capacity)])
                await case.wait(lambda: 'D4_HISTORY_READY' in text(gateway['log']) and current_matches() == 1, 'new history generation and native writer ready')
                oracle = await worker(case, 'restart-after', peers, endpoint, 'client-after', retention)
                oracle['calls'] = before['calls'] + oracle['calls']
            else:
                oracle = await worker(case, mode, peers, endpoint, retention=retention)
            history_log = '\n'.join(text(r['log']) for _, _, r in case.children if r['name'].startswith('history'))
            for call in oracle['calls']:
                fields = call['input']
                demand(f'D4_HISTORY_SAMPLE key={fields[1]} sequence={fields[2]}' in history_log or fields[0] == 'dispose', 'native reader historical delivery witness')
            report.update(dds_commands=len(oracle['calls']), retention_ms=retention, history_capacity=capacity)
        report.update(status='PASS')
    except Exception as error:
        failure = error
        report['error'] = str(error)
    finally:
        report['processes'] = case.finish()
        report['modules'] = case.modules
        for record in report['processes']:
            demand_marker = 'CLOSED exit=0 owners=0'
            if record['exit_code'] != 0 or record['forced_cleanup'] or demand_marker not in text(record['log']):
                report['status'] = 'FAIL'
                failure = failure or RuntimeError('child status/ownership/forced cleanup')
        (case.path / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if failure:
        raise RuntimeError(case.path.name + ': ' + str(failure))
    return report


async def main(args):
    out = args.out.resolve()
    demand(not out.exists() and out.is_relative_to((ROOT / 'build').resolve()), 'new workspace output required')
    bundles = {b['bundle']['abi']: b['bundle'] for b in [json.loads(p.read_text(encoding='utf-8')) for p in args.build_summary]}
    demand(set(bundles) == {'3.6.2', '2.14.5'}, 'both exact ABI builds required')
    before = {abi: validate(bundle) for abi, bundle in bundles.items()}
    out.mkdir(parents=True)
    report = dict(status='FAIL', cases=[])
    try:
        for abi, bundle in bundles.items():
            for rep in (0, 2):
                for mode in ('services', 'history'):
                    report['cases'].append(await run_case(Case(out / f'{abi}-rep{rep}-{mode}', bundle, rep), bundles, mode))
            for mode in ('capacity', 'expiry', 'restart'):
                report['cases'].append(await run_case(Case(out / f'{abi}-{mode}', bundle), bundles, mode))
        demand({abi: validate(bundle) for abi, bundle in bundles.items()} == before, 'D4 inputs changed during acceptance')
        report.update(status='PASS', case_count=len(report['cases']), source_unchanged=True)
    finally:
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('D4_ACCEPTANCE PASS cases=' + str(len(report['cases'])), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path, required=True)
    asyncio.run(main(parser.parse_args()))
