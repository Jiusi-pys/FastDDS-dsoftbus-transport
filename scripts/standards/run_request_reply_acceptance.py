"""D3 real DDS acceptance: library actors versus independently linked SDK peers."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import subprocess
import time

import psutil
from build_request_reply import ROOT, sha, snapshot


def demand(ok, message):
    if not ok:
        raise RuntimeError(message)


def text(path):
    return path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''


def validate(bundle):
    built = json.loads(Path(bundle['build_summary']).read_text(encoding='utf-8'))
    demand(built['status'] == 'PASS' and built['bundle'] == bundle, 'D3 build bundle mismatch')
    demand(built['source_snapshot'] == snapshot(), 'D3 source changed after build')
    demand(len(built['commands']) == 4 and all(c['exit_code'] == 0 for c in built['commands']), 'D3 build commands')
    for path, digest in {**built['artifact_sha256'], **built['generated_sha256']}.items():
        demand(sha(path) == digest, 'D3 artifact/generated change: ' + path)
    demand(sha(built['clang']) == built['clang_sha256'], 'D3 clang changed')
    return built


def messages(log, event):
    pattern = re.compile(r'^' + event + r' token=([0-9a-f]{32}:\d+) operation=(\d+) status=(\d+) label=([0-9a-f]+|-) payload=([0-9a-f]+|-)$', re.M)
    found = pattern.findall(log)
    demand(len({m[0] for m in found}) == len(found), 'duplicate event identity: ' + event)
    return {m[0]: dict(operation=int(m[1]), status=int(m[2]), label=m[3], payload=m[4]) for m in found}


class Case:
    def __init__(self, path, domain=219, representation=2):
        self.path, self.domain, self.rep = path, domain, representation
        path.mkdir()
        self.service = 'D3_' + str(time.time_ns())
        self.children = []
        self.modules = []

    def start(self, name, bundle, role, mode, count=4, peers=1, capacity=16, native=False):
        directory = self.path / name
        directory.mkdir()
        argv = [bundle['peer' if native else 'actor'], role, str(self.domain), self.service, str(self.rep), mode, str(directory), str(peers)]
        if not native:
            argv += [str(capacity)]
        argv += [str(count)]
        env = os.environ.copy()
        env['PATH'] = os.pathsep.join(bundle['runtime_dirs']) + os.pathsep + env.get('PATH', '')
        log = directory / 'process.log'
        stream = log.open('wb')
        process = subprocess.Popen(argv, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        record = dict(name=name, abi=bundle['abi'], role=role, native_peer=native, argv=argv, pid=process.pid,
                      log=str(log), directory=str(directory), runtime_dirs=bundle['runtime_dirs'], expected_exit=0, forced_cleanup=False)
        self.children.append((process, stream, record))
        return record

    def wait(self, predicate, what, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            if any(p.poll() is not None for p, _, _ in self.children):
                raise RuntimeError('child exited before ' + what)
            time.sleep(.01)
        raise RuntimeError('timeout: ' + what)

    def audit(self):
        for process, _, record in self.children:
            modules = []
            for item in psutil.Process(process.pid).memory_maps():
                path = Path(item.path)
                low = path.name.lower()
                if path.suffix.lower() == '.dll' and low.startswith(('fastdds', 'fastrtps', 'fastcdr', 'foonathan')):
                    demand(path.resolve().parent in {Path(p).resolve() for p in record['runtime_dirs']}, 'DDS dependency outside declared runtime directories')
                    abi = record['abi']
                    demand(not (abi == '3.6.2' and low.startswith('fastrtps')) and not (abi == '2.14.5' and low.startswith('fastdds')), 'opposite DDS ABI loaded')
                    modules.append(dict(path=str(path.resolve()), sha256=sha(path)))
            wanted = 'fastdds-3.6.dll' if record['abi'] == '3.6.2' else 'fastrtps-2.14.dll'
            demand(any(Path(m['path']).name.lower() == wanted for m in modules), 'expected DDS ABI absent')
            demand(any(Path(m['path']).name.lower().startswith('fastcdr') for m in modules), 'Fast CDR absent')
            self.modules.append(dict(name=record['name'], abi=record['abi'], modules=modules))

    def ready(self):
        self.wait(lambda: all('D3_READY ' in text(Path(r['log'])) for _, _, r in self.children), 'all four endpoint directions discovered')
        self.audit()
        for _, _, record in self.children:
            (Path(record['directory']) / 'go').touch()

    def finish(self):
        # Request writers close while the corresponding request readers remain alive.
        for process, stream, record in sorted(self.children, key=lambda row: row[2]['role'] != 'client'):
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


def run_pair(case, actor_bundle, peer_bundle, role, mode):
    report = dict(name=case.path.name, role=role, mode=mode, status='FAIL')
    failure = None
    try:
        count = 1 if mode in ('wrong', 'timeout', 'cancel', 'duplicate', 'closed', 'drop') else 3 if mode == 'capacity' else 4
        capacity = 2 if mode == 'capacity' else 16
        actor = case.start('actor', actor_bundle, role, mode, count, capacity=capacity)
        if mode == 'not-ready':
            case.wait(lambda: 'D3_NOT_READY_REJECTED' in text(Path(actor['log'])), 'not-ready rejection before peer exists')
        peer_mode = dict(timeout='hold', cancel='hold', closed='drop', basic='echo', **{'not-ready': 'echo'}).get(mode, mode)
        peer = case.start('peer', peer_bundle, 'server' if role == 'client' else 'client', peer_mode, count, native=True)
        case.ready()
        release = {'wrong': 'D3_REJECTIONS pending=1', 'timeout': 'D3_TIMEOUT ', 'cancel': 'D3_CANCELLED ',
                   'duplicate': 'D3_REPLY_READY ', 'capacity': 'D3_CAPACITY_REJECTED'}
        if role == 'client' and mode in release:
            case.wait(lambda: release[mode] in text(Path(actor['log'])), 'application rejection/terminal-state barrier')
            (Path(peer['directory']) / 'continue').touch()
        case.wait(lambda: all('D3_DONE ' in text(Path(row['log'])) for row in (actor, peer)), 'actors finished protocol')
        actorlog, peerlog = text(Path(actor['log'])), text(Path(peer['log']))
        if role == 'client':
            submitted, observed = messages(actorlog, 'D3_SUBMITTED'), messages(peerlog, 'D3_PEER_REQUEST')
            demand(len(submitted) == len(observed) == count and submitted == observed, 'independent native request identity/body witness')
            if mode not in ('timeout', 'cancel', 'closed'):
                results, replies = messages(actorlog, 'D3_RESULT'), messages(peerlog, 'D3_PEER_REPLY')
                demand(len(results) == count and results == replies and set(results) == set(submitted), 'independent native reply identity/body witness')
            else:
                outcome = dict(timeout='Timeout', cancel='Cancelled', closed='Closed')[mode]
                demand('outcome=' + outcome in actorlog and not messages(actorlog, 'D3_RESULT'), 'expected business outcome')
            if mode == 'wrong':
                demand('D3_PEER_BAD_SENT count=3' in peerlog and 'D3_REJECTIONS pending=1' in actorlog, 'wrong/missing related identity oracle')
            if mode == 'duplicate':
                demand('D3_DUPLICATE_REJECTED' in actorlog and 'D3_PEER_DUPLICATE_SENT' in peerlog, 'duplicate oracle')
            if mode == 'capacity':
                demand('D3_COMPLETED_CAPACITY_REJECTED' in actorlog, 'completed slot retains backpressure')
        else:
            submitted, received = messages(peerlog, 'D3_PEER_SUBMITTED'), messages(actorlog, 'D3_RECEIVED')
            demand(len(submitted) == len(received) == count and submitted == received, 'independent request delivery')
            if mode == 'drop':
                demand('D3_PEER_TIMEOUT count=1' in peerlog and 'D3_DISCARDED ' in actorlog and not messages(peerlog, 'D3_PEER_RESULT'), 'explicit discard/local timeout')
            else:
                replies, observed = messages(actorlog, 'D3_REPLIED'), messages(peerlog, 'D3_PEER_RESULT')
                demand(len(replies) == len(observed) == count and replies == observed, 'independent native correlated results')
            if mode == 'capacity':
                demand('D3_SERVER_CAPACITY_REJECTED' in actorlog, 'server inflight bound')
        report.update(status='PASS', request_count=count)
    except Exception as error:
        failure = error
        report['error'] = str(error)
    finally:
        report['processes'] = case.finish()
        report['modules'] = case.modules
        for record in report['processes']:
            marker = 'D3_PEER_CLOSED exit=0 owners=0' if record['native_peer'] else 'D3_CLOSED exit=0 owners=0'
            if record['forced_cleanup'] or record['exit_code'] != 0 or marker not in text(Path(record['log'])):
                failure = failure or RuntimeError('child exit/ownership/forced cleanup')
                report['status'] = 'FAIL'
        (case.path / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if failure:
        raise RuntimeError(case.path.name + ': ' + str(failure))
    return report


def run_multi(case, bundles, library_server):
    report = dict(name=case.path.name, status='FAIL', multi_client=True)
    failure = None
    try:
        server = case.start('server', bundles['3.6.2'], 'server', 'reverse', 8, peers=2, native=not library_server)
        clients = [case.start('clientA', bundles['3.6.2'], 'client', 'basic', 4, native=library_server),
                   case.start('clientB', bundles['2.14.5'], 'client', 'basic', 4, native=library_server)]
        case.ready()
        case.wait(lambda: all('D3_DONE ' in text(Path(r['log'])) for r in [server, *clients]), 'two simultaneous clients', timeout=15)
        seen = messages(text(Path(server['log'])), 'D3_RECEIVED' if library_server else 'D3_PEER_REQUEST')
        replies = messages(text(Path(server['log'])), 'D3_REPLIED' if library_server else 'D3_PEER_REPLY')
        all_requests, all_results = {}, {}
        for client in clients:
            log = text(Path(client['log']))
            requests = messages(log, 'D3_PEER_SUBMITTED' if library_server else 'D3_SUBMITTED')
            results = messages(log, 'D3_PEER_RESULT' if library_server else 'D3_RESULT')
            demand(len(requests) == len(results) == 4 and not set(requests) & set(all_requests), 'distinct full request identities')
            all_requests.update(requests)
            all_results.update(results)
        demand(len(seen) == 8 and seen == all_requests and replies == all_results, 'multi-client native values/correlation')
        guids = {token.split(':')[0] for token in all_requests}
        sequences = Counter(token.split(':')[1] for token in all_requests)
        demand(len(guids) == 2 and max(sequences.values()) >= 2, 'same sequence with different writer GUIDs was not exercised')
        report.update(status='PASS', request_count=8, distinct_writers=2, shared_sequence_exercised=True)
    except Exception as error:
        failure = error
        report['error'] = str(error)
    finally:
        report['processes'] = case.finish()
        report['modules'] = case.modules
        if any(p['forced_cleanup'] or p['exit_code'] != 0 or 'owners=0' not in text(Path(p['log'])) for p in report['processes']):
            report['status'] = 'FAIL'
            failure = failure or RuntimeError('multi-client child cleanup')
        (case.path / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if failure:
        raise RuntimeError(case.path.name + ': ' + str(failure))
    return report


def main(args):
    out = args.out.resolve()
    demand(not out.exists() and out.is_relative_to((ROOT / 'build').resolve()), 'new workspace output required')
    bundles = {item['bundle']['abi']: item['bundle'] for item in [json.loads(p.read_text(encoding='utf-8')) for p in args.build_summary]}
    demand(set(bundles) == {'3.6.2', '2.14.5'}, 'both exact ABI builds required')
    before = {abi: validate(b) for abi, b in bundles.items()}
    out.mkdir(parents=True)
    summary = dict(status='FAIL', cases=[])
    try:
        for abi, bundle in bundles.items():
            domain = 219 if abi == '3.6.2' else 220
            other = bundles['2.14.5' if abi == '3.6.2' else '3.6.2']
            for rep in (0, 2):
                for role, mode in [('client', 'basic'), ('client', 'reverse'), ('server', 'reverse')]:
                    name = f'{abi}-rep{rep}-{role}-{mode}'
                    summary['cases'].append(run_pair(Case(out / name, domain, rep), bundle, bundle, role, mode))
                for role in ('client', 'server'):
                    name = f'{abi}-rep{rep}-{role}-mixed'
                    summary['cases'].append(run_pair(Case(out / name, domain, rep), bundle, other, role, 'reverse'))
            for mode in ('wrong', 'timeout', 'cancel', 'duplicate', 'capacity', 'closed', 'not-ready'):
                name = f'{abi}-client-{mode}'
                summary['cases'].append(run_pair(Case(out / name, domain), bundle, bundle, 'client', mode))
            for mode in ('capacity', 'drop'):
                name = f'{abi}-server-{mode}'
                summary['cases'].append(run_pair(Case(out / name, domain), bundle, bundle, 'server', mode))
        for server in (False, True):
            name = 'multi-' + ('library-server' if server else 'library-clients')
            summary['cases'].append(run_multi(Case(out / name), bundles, server))
        demand({abi: validate(b) for abi, b in bundles.items()} == before, 'D3 inputs changed during acceptance')
        summary.update(status='PASS', case_count=len(summary['cases']), source_unchanged=True)
    finally:
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D3_ACCEPTANCE PASS cases=' + str(len(summary['cases'])), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', action='append', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    main(parser.parse_args())
