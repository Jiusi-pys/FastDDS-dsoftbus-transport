#!/usr/bin/env python3
"""Run unmodified ROS 2 nodes between two OpenHarmony boards over the DSoftBus Transport.

    python scripts/release/run_ros2_cross_board.py --work E:/s2 --a <hdc target A> --b <hdc target B> --mode MODE
        MODE: suite | smoke | components | route | restart | negative
        [--groups all|topic,service,action,parameter,lifecycle] [--no-deploy] [--out DIR]

The libraries go to their own board directory (/data/local/tmp/fdds_dsb/2.14.5-ros), because Fast CDR
SONAMEs collide between Fast DDS lines. The autoload library is the only thing that selects DSoftBus;
stage2_node contains no Transport or Fast DDS code. Exit code 0 only when every check on both boards
passed, both processes exited 0 and both Runtimes closed.

Path evidence: the process holds no UDP socket of its own, sampled through the run, and the Transport's
own counters at close show the SDK carried at least as many records as the application delivered.
"""
import argparse
import json
import re
import secrets
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_cross_board import CXX_SHARED, Run, ipv4, send, sh, soname, token_of, wait_until  # noqa: E402

VERSION = '2.14.5-ros'
AUTOLOAD = 'libfastdds_dsoftbus_ros2_autoload.so'
REFUSED_EXIT = 71
GROUP_CASES = {'topic': 4, 'service': 1, 'action': 2, 'parameter': 1, 'lifecycle': 1}  # the graph case always runs
OUTAGE_SECONDS = 15
ALLOWED_TAIL_LOSS = 10  # samples a peer may still send after the receiver stopped
PROGRAMS = ('stage2_node', 'minimal_publisher', 'minimal_subscriber', 'talker', 'listener')


def artifacts(work):
    """(lib name -> file) for everything a ROS process loads, and (bin name -> file)."""
    prefix, install = Path(work) / VERSION / 'prefix', Path(work) / VERSION / 'transport-install'
    ros = Path(work) / 'ros/install'
    libs = {}
    for folder in [prefix / 'lib', install / 'lib', ros / 'lib', *sorted((ros / 'opt').glob('*/lib'))]:
        # Longest name first: the fully versioned file is the installed one, without the build RUNPATH.
        for f in sorted(folder.glob('*.so*'), reverse=True):
            if f.is_file() and not f.is_symlink():
                libs.setdefault(soname(f) or f.name, f)
    libs['libc++_shared.so'] = CXX_SHARED
    bins = {'dsb_suite': install / 'bin/dsb_suite', 'stage2_node': ros / 'bin/stage2_node'}
    for name, path in [('talker', ros / 'lib/demo_nodes_cpp/talker'), ('listener', ros / 'lib/demo_nodes_cpp/listener'),
                       ('minimal_publisher', ros / 'lib/examples_rclcpp_minimal_publisher/publisher_member_function'),
                       ('minimal_subscriber', ros / 'lib/examples_rclcpp_minimal_subscriber/subscriber_member_function')]:
        if path.exists():
            bins[name] = path
    return libs, bins


def kill_leftovers(target):
    """A timed-out run only loses its hdc client, so its board process has to be stopped here."""
    sh(target, 'p=$(pidof %s); [ -z "$p" ] || kill -9 $p' % ' '.join(PROGRAMS))


def local_sha(path):
    import hashlib
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def deploy(target, root, libs, bins):
    sh(target, 'mkdir -p %s/lib %s/bin %s/work' % (root, root, root))
    remote = dict(reversed(l.split()) for l in sh(target, 'cd %s && sha256sum lib/* bin/* 2>/dev/null' % root, 300).splitlines() if len(l.split()) == 2)
    sent = 0
    for folder, items in (('lib', libs), ('bin', bins)):
        for name, f in sorted(items.items()):
            if remote.get('%s/%s' % (folder, name)) != local_sha(f):
                send(target, f, '%s/%s/%s' % (root, folder, name)); sent += 1
    sh(target, 'chmod 755 %s/bin/*' % root)
    return sent


def node_info(target, env, token):
    text = sh(target, '%s ./bin/dsb_suite node token=%s' % (env, token))
    m = re.search(r'DSB_NODE network=(\S+) udid=(\S+)', text)
    if not m:
        sys.exit('node query failed on %s: %s' % (target, text))
    return {'network': m.group(1), 'udid': m.group(2)}


class Pair:
    """Per-run configs and the launch line for one ROS process on each board."""

    def __init__(self, a, root, boards, nodes, tokens, log):
        self.a, self.root, self.boards, self.nodes, self.tokens, self.log = a, root, boards, nodes, tokens, log
        self.run_id = secrets.token_hex(8)
        self.work = '%s/work/%s' % (root, self.run_id)
        for index, (side, target) in enumerate(boards.items()):
            peer = nodes['B' if side == 'A' else 'A']
            text = ''.join('%s=%s\\n' % kv for kv in [
                ('side', index), ('run', self.run_id), ('domain', a.domain), ('local_udid', nodes[side]['udid']),
                ('peer_udid', peer['udid']), ('peer_network', peer['network']), ('token', tokens[side]),
                ('go_file', self.work + '/go')])
            sh(target, 'mkdir -p %s/log && printf "%s" > %s/s2.conf' % (self.work, text, self.work))

    def line(self, side, program, overrides=None):
        env = {'LD_LIBRARY_PATH': self.root + '/lib', 'AMENT_PREFIX_PATH': self.root, 'HOME': self.work, 'ROS_HOME': self.work,
               'ROS_LOG_DIR': self.work + '/log', 'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp', 'ROS_DOMAIN_ID': self.a.domain,
               'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SYSTEM_DEFAULT', 'FASTDDS_DSOFTBUS_AUTOLOAD_CONFIG': self.work + '/s2.conf',
               'LD_PRELOAD': (self.a.diagnostic_preload + ':' if self.a.diagnostic_preload else '') + '%s/lib/%s' % (self.root, AUTOLOAD)}
        env.update(overrides or {})
        prefix = ' '.join('%s=%s' % kv for kv in env.items() if kv[1] is not None)
        return 'cd %s && %s ./bin/%s; echo S2_EXIT=$?' % (self.root, prefix, program)

    def start(self, side, program, overrides=None):
        return Run(self.boards[side], self.line(side, program, overrides), self.log)

    def go(self):
        for target in self.boards.values():
            sh(target, 'touch %s/go' % self.work)


def exit_code(run):
    for line in run.lines:
        m = re.match(r'S2_EXIT=(\d+)', line)
        if m:
            return int(m.group(1))
    return None


def cases(run, side):
    rows = []
    for line in run.lines:
        if line.startswith('S2_CASE'):
            row = dict(x.split('=', 1) for x in line.split()[1:] if '=' in x); row['board'] = side; rows.append(row)
    return rows


def closed(run):
    return any(l.startswith('FDDS_AUTOLOAD_CLOSED ok=1') for l in run.lines) and not any('CLOSE_INCOMPLETE' in l for l in run.lines)


def send_failure_with_peer_alive(log_path):
    """A send may fail once the peer has shut down; before that it is a Transport failure."""
    finished, early = set(), []
    for line in Path(log_path).read_text(encoding='utf-8').splitlines():
        board = line[1:7]
        if 'S2_SUMMARY' in line:
            finished.add(board)
        elif 'FDDS_AUTOLOAD_SEND_FAILURE' in line and not finished - {board}:
            early.append(line)
    return early


def udp_socket_count(target, program):
    """UDP sockets of the live process, or None if it could not be read."""
    script = ('p=$(pidof %s | cut -d" " -f1); [ -n "$p" ] || { echo NOPID; exit; }; '
              'f=$(ls -l /proc/$p/fd) || { echo FDERR; exit; }; n=0; '
              'for i in $(echo "$f" | grep -o "socket:\\[[0-9]*\\]" | grep -o "[0-9]*"); do '
              'grep -q " $i " /proc/net/udp /proc/net/udp6 && n=$((n+1)); done; '
              'echo SOCKETS udp=$n' % program)
    m = re.search(r'SOCKETS udp=(\d+)', sh(target, script))
    return int(m.group(1)) if m else None


class SocketWatch:
    """Samples both boards until stopped, so a socket opened late in a run is seen too."""

    def __init__(self, boards_programs):
        self.boards_programs, self.samples, self.stopping = boards_programs, {s: [] for s in boards_programs}, threading.Event()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _loop(self):
        while not self.stopping.wait(1):
            for side, (target, program) in self.boards_programs.items():
                sample = udp_socket_count(target, program)
                if sample is not None:
                    self.samples[side].append(sample)

    def verdict(self, runs, minimum_records):
        """runs: {side: Run}. Each side's Transport must have carried at least minimum_records each way."""
        self.stopping.set(); self.thread.join()
        sockets = {s: {'samples': len(v), 'max_udp': max(v, default=None)} for s, v in self.samples.items()}
        transport = {s: transport_counts(run) for s, run in runs.items()}
        ok = (all(v['samples'] > 0 and v['max_udp'] == 0 for v in sockets.values()) and
              all(v['sdk_sends'] >= minimum_records and v['received_records'] >= minimum_records for v in transport.values()))
        return {'sockets': sockets, 'transport': transport, 'minimum_records': minimum_records, 'path_ok': ok}


def transport_counts(run):
    for line in run.lines:
        m = re.match(r'FDDS_AUTOLOAD_CLOSED ok=1 sdk_sends=(\d+) received_records=(\d+)', line)
        if m:
            return {'sdk_sends': int(m.group(1)), 'received_records': int(m.group(2))}
    return {'sdk_sends': 0, 'received_records': 0}


def stream_numbers(run):
    for line in run.lines:
        m = re.match(r'S2_STREAM sent=(\d+) received=(\d+) gaps=(\d+) max_silence=([0-9.]+)', line)
        if m:
            return {'sent': int(m.group(1)), 'received': int(m.group(2)), 'gaps': int(m.group(3)), 'max_silence': float(m.group(4))}
    return None


def wait_runs(runs, seconds):
    end = time.time() + seconds
    for run in runs:
        while run.p.poll() is None and time.time() < end:
            time.sleep(0.2)
        if run.p.poll() is None:
            run.p.kill()


def check_pair(runs, names):
    """Common verdict: every case passed, exit 0, normal close."""
    rows = [c for name, run in zip(names, runs) for c in cases(run, name)]
    return {'cases': rows, 'exit': [exit_code(r) for r in runs], 'closed': [closed(r) for r in runs], 'names': names,
            'ok': bool(rows) and all(c.get('pass') == '1' for c in rows) and all(exit_code(r) == 0 for r in runs) and all(closed(r) for r in runs)}


def mode_suite(a, pair):
    runs = [pair.start('A', 'stage2_node suite 0 ' + a.groups), pair.start('B', 'stage2_node suite 1 ' + a.groups)]
    wait_until(lambda: all(r.seen('FDDS_AUTOLOAD_READY') for r in runs), 60)
    time.sleep(3)
    pair.go()
    watch = SocketWatch({s: (t, 'stage2_node') for s, t in pair.boards.items()})
    wait_runs(runs, a.timeout)
    result = check_pair(runs, ['A', 'B'])
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 20))  # the smallest group writes 20 samples each way
    pair.log.flush()
    result['send_failures_with_peer_alive'] = send_failure_with_peer_alive(pair.log.name)
    groups = list(GROUP_CASES) if a.groups == 'all' else a.groups.split(',')
    result['expected_cases'] = 2 * (1 + sum(GROUP_CASES[g] for g in groups))
    result['ok'] = (result['ok'] and result['path_ok'] and not result['send_failures_with_peer_alive'] and
                    len(result['cases']) == result['expected_cases'])
    return result


def run_unmodified(pair, sender, receiver):
    """Two unmodified ROS binaries, one per board, stopped by SIGINT like an operator would."""
    runs = [pair.start('A', sender), pair.start('B', receiver)]
    wait_until(lambda: all(r.seen('FDDS_AUTOLOAD_READY') or r.p.poll() is not None for r in runs), 60)
    time.sleep(3)
    pair.go()
    watch = SocketWatch({'A': (pair.boards['A'], sender), 'B': (pair.boards['B'], receiver)})
    heard = wait_until(lambda: sum('I heard' in l for l in runs[1].lines) >= 5, 60)
    time.sleep(8)  # long enough for the socket watcher to sample a short run
    for side, name in (('A', sender), ('B', receiver)):
        sh(pair.boards[side], 'kill -INT $(pidof %s)' % name)
    wait_runs(runs, 30)
    result = {'heard': sum('I heard' in l for l in runs[1].lines), 'exit': [exit_code(r) for r in runs], 'closed': [closed(r) for r in runs]}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, result['heard']))
    result['ok'] = heard and all(c == 0 for c in result['exit']) and all(result['closed']) and result['path_ok']
    return result


def mode_smoke(a, pair):
    """Upstream sample binaries that link rclcpp directly."""
    return run_unmodified(pair, 'minimal_publisher', 'minimal_subscriber')


def mode_components(a, pair):
    """demo_nodes_cpp talker/listener, which load their node through class_loader."""
    return run_unmodified(pair, 'talker', 'listener')


def mode_route(a, pair):
    seconds = 60
    runs = [pair.start('A', 'stage2_node stream 0 %d' % seconds), pair.start('B', 'stage2_node stream 1 %d' % seconds)]
    wait_until(lambda: all(r.seen('FDDS_AUTOLOAD_READY') for r in runs), 60)
    time.sleep(3)
    pair.go()
    matched = wait_until(lambda: all(r.seen('S2_STREAM_MATCHED') for r in runs), 120)
    watch = SocketWatch({s: (t, 'stage2_node') for s, t in pair.boards.items()})
    ips = ipv4(pair.boards['A'])
    add = '; '.join('iptables -I INPUT -s %s -j DROP; iptables -I OUTPUT -d %s -j DROP' % (p, p) for p in ips)
    rm = '; '.join('iptables -D INPUT -s %s -j DROP; iptables -D OUTPUT -d %s -j DROP' % (p, p) for p in ips)
    time.sleep(6)
    sh(pair.boards['B'], add); time.sleep(OUTAGE_SECONDS); sh(pair.boards['B'], rm)
    wait_runs(runs, seconds + 90)
    result = check_pair(runs, ['A', 'B'])
    a_numbers, b_numbers = stream_numbers(runs[0]), stream_numbers(runs[1])
    result.update(matched=matched, numbers={'A': a_numbers, 'B': b_numbers})
    delivered = bool(a_numbers and b_numbers) and (a_numbers['received'] >= b_numbers['sent'] - ALLOWED_TAIL_LOSS and
                                                   b_numbers['received'] >= a_numbers['sent'] - ALLOWED_TAIL_LOSS)
    # A stall shorter than the outage means the outage did not touch this data path.
    stalled = bool(a_numbers and b_numbers) and min(a_numbers['max_silence'], b_numbers['max_silence']) >= OUTAGE_SECONDS - 5
    result.update(delivered_all=delivered, stalled_by_outage=stalled)
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, min(a_numbers['received'], b_numbers['received']) if a_numbers and b_numbers else 1))
    result['ok'] = result['ok'] and matched and delivered and stalled and result['path_ok']
    return result


def mode_restart(a, pair):
    """A runs throughout; B exits and a new B process rejoins, reusing the deployment."""
    long_run = pair.start('A', 'stage2_node stream 0 75')
    first = pair.start('B', 'stage2_node stream 1 15')
    wait_until(lambda: long_run.seen('FDDS_AUTOLOAD_READY') and first.seen('FDDS_AUTOLOAD_READY'), 60)
    time.sleep(3)
    pair.go()
    watch = SocketWatch({s: (t, 'stage2_node') for s, t in pair.boards.items()})
    wait_runs([first], 90)
    second = pair.start('B', 'stage2_node stream 1 15')
    wait_runs([second], 90)
    wait_runs([long_run], 120)
    result = check_pair([long_run, first, second], ['A', 'B1', 'B2'])
    numbers = {name: stream_numbers(run) for name, run in (('A', long_run), ('B1', first), ('B2', second))}
    result.update(watch.verdict({'A': long_run, 'B1': first, 'B2': second}, 100))
    result['numbers'] = numbers
    # A must hear both incarnations of B, and the second B must hear A.
    delivered = all(numbers.values()) and (numbers['A']['received'] >= numbers['B1']['sent'] + numbers['B2']['sent'] - 2 * ALLOWED_TAIL_LOSS and
                                           numbers['B2']['received'] > 0)
    result['delivered_all'] = delivered
    result['ok'] = result['ok'] and delivered and len(result['cases']) == 3 and result['path_ok']
    return result


def mode_negative(a, pair):
    rows = []
    for side, index in (('A', 0), ('B', 1)):
        for name, overrides in [('missing_config', {'FASTDDS_DSOFTBUS_AUTOLOAD_CONFIG': None}), ('wrong_domain', {'ROS_DOMAIN_ID': '152'}),
                                ('localhost_range', {'ROS_AUTOMATIC_DISCOVERY_RANGE': 'LOCALHOST'}), ('subnet_range', {'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SUBNET'}),
                                ('static_peers', {'ROS_STATIC_PEERS': '192.168.77.202'}), ('localhost_only', {'ROS_LOCALHOST_ONLY': '1'}),
                                ('other_rmw', {'RMW_IMPLEMENTATION': 'rmw_cyclonedds_cpp'}), ('fastrtps_profiles', {'FASTRTPS_DEFAULT_PROFILES_FILE': '/data/none.xml'}),
                                ('fastdds_profiles', {'FASTDDS_DEFAULT_PROFILES_FILE': '/data/none.xml'}), ('builtin_transports', {'FASTDDS_BUILTIN_TRANSPORTS': 'UDPv4'}),
                                ('environment_file', {'FASTDDS_ENVIRONMENT_FILE': '/data/none.json'}), ('discovery_server', {'ROS_DISCOVERY_SERVER': '192.168.77.202:11811'})]:
            run = pair.start(side, 'stage2_node suite %d topic' % index, overrides)
            wait_runs([run], 30)
            rows.append({'board': side, 'case': name, 'exit': exit_code(run), 'refused': run.seen('FDDS_AUTOLOAD_REFUSED'), 'cases_run': len(cases(run, side))})
    return {'rows': rows, 'ok': all(r['exit'] == REFUSED_EXIT and r['refused'] and r['cases_run'] == 0 for r in rows)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True); ap.add_argument('--a', required=True); ap.add_argument('--b', required=True)
    ap.add_argument('--mode', required=True, choices=['suite', 'smoke', 'components', 'route', 'restart', 'negative'])
    ap.add_argument('--groups', default='all'); ap.add_argument('--domain', default='151'); ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--board-root', default='/data/local/tmp/fdds_dsb')
    ap.add_argument('--token-process', default='mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16')
    ap.add_argument('--diagnostic-preload', default='', help='board path of an extra library loaded before the autoload library')
    ap.add_argument('--no-deploy', action='store_true'); ap.add_argument('--out')
    a = ap.parse_args()
    if a.groups != 'all' and not set(a.groups.split(',')) <= set(GROUP_CASES):
        sys.exit('unknown group in %s (known: %s)' % (a.groups, ', '.join(GROUP_CASES)))
    out = Path(a.out or Path(a.work) / 'runs' / ('%s-%s' % (a.mode, secrets.token_hex(4)))); out.mkdir(parents=True, exist_ok=True)
    log = open(out / 'run.log', 'w', encoding='utf-8')
    root = '%s/%s' % (a.board_root, VERSION)
    boards = {'A': a.a, 'B': a.b}
    libs, a.bins = artifacts(a.work)
    if not a.no_deploy:
        for side, target in boards.items():
            print('deploy', side, 'sent', deploy(target, root, libs, a.bins), 'of', len(libs) + len(a.bins), flush=True)
    for target in boards.values():
        kill_leftovers(target)
    env = 'cd %s && LD_LIBRARY_PATH=%s/lib' % (root, root)
    tokens = {s: token_of(t, a.token_process) for s, t in boards.items()}
    nodes = {s: node_info(t, env, tokens[s]) for s, t in boards.items()}
    pair = Pair(a, root, boards, nodes, tokens, log)
    outcome = {'suite': mode_suite, 'smoke': mode_smoke, 'components': mode_components, 'route': mode_route, 'restart': mode_restart, 'negative': mode_negative}[a.mode](a, pair)
    outcome.update(mode=a.mode, version=VERSION, run=pair.run_id, targets=boards, artifacts={'stage2_node': local_sha(a.bins['stage2_node']), 'autoload': local_sha(libs[AUTOLOAD])})
    for target in boards.values():
        kill_leftovers(target)
    (out / 'result.json').write_text(json.dumps(outcome, indent=1))
    print(json.dumps({k: v for k, v in outcome.items() if k != 'artifacts'}, indent=1))
    print('result:', 'PASS' if outcome['ok'] else 'FAIL', 'out=%s' % out)
    return 0 if outcome['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
