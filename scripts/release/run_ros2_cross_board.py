#!/usr/bin/env python3
"""Run unmodified ROS 2 nodes between two OpenHarmony boards over the DSoftBus Transport.

    python scripts/release/run_ros2_cross_board.py --work E:/s2 --a <hdc target A> --b <hdc target B> --mode MODE
        MODE: suite | smoke | components | route | restart | negative | features | demos | kill_peer | sigterm
              multiprocess | hub_restart | no_hub (these three need --hub)
        [--groups all|<groups of the mode>] [--hub] [--no-deploy] [--out DIR]

--hub runs each ROS process through the board's hub (integrations/ros2/hub): the hub owns the DSoftBus link and
any number of processes attach to it. Without it each process owns the link, one process per board.

The libraries go to their own board directory (/data/local/tmp/fdds_dsb/2.14.5-ros), because Fast CDR
SONAMEs collide between Fast DDS lines. The autoload library is the only thing that selects DSoftBus;
stage2_node contains no Transport or Fast DDS code. Exit code 0 only when every check on both boards
passed, both processes exited 0 and both Runtimes closed.

Path evidence: the process holds no UDP socket of its own, sampled through the run, and the Transport's
own counters at close show the SDK carried at least as many records as the application delivered.
"""
import argparse
import io
import json
import re
import secrets
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_cross_board import CXX_SHARED, Run, ipv4, send, sh, soname, token_of, wait_until  # noqa: E402

VERSION = '2.14.5-ros'
AUTOLOAD = 'libfastdds_dsoftbus_ros2_autoload.so'
HUB_PRELOAD = 'libfastdds_dsoftbus_ros2_hub.so'
REFUSED_EXIT = 71
GROUP_CASES = {'topic': 4, 'service': 1, 'action': 2, 'parameter': 1, 'lifecycle': 1}  # the graph case always runs
OUTAGE_SECONDS = 15
ALLOWED_TAIL_LOSS = 10  # samples a peer may still send after the receiver stopped
FEATURE_CASES = {'qos': 5, 'data': 4, 'large': 3, 'scale': 1, 'graph': 4, 'calls': 4, 'exec': 7, 'context': 1, 'domain2': 1, 'services': 6}
DIRECT_ONLY_LIMIT = {'domain2'}  # a direct process serves the one domain its link was configured for
PROGRAMS = ['stage2_node', 'stage2_features', 'stage2_dispose_probe', 'minimal_publisher', 'minimal_subscriber', 'minimal_action_server', 'fdds_dsb_hub']
# Unmodified upstream binaries: (case, board A program, board B program, line the check counts, side that prints it, count).
DEMOS = [('loaned', 'talker_loaned_message', 'listener', 'I heard', 'B', 5),
         ('serialized', 'talker_serialized_message', 'listener_serialized_message', 'I heard data of length', 'B', 5),
         ('service', 'add_two_ints_server', 'add_two_ints_client', 'Result of add_two_ints', 'B', 1),
         ('service_async', 'add_two_ints_server', 'add_two_ints_client_async', 'Result of add_two_ints', 'B', 1),
         ('introspection', 'introspection_service', 'introspection_client', 'Result of add_two_ints', 'B', 1),
         ('content_filter', 'content_filtering_publisher', 'content_filtering_subscriber', 'I receive an emergency temperature', 'B', 2),
         ('composition', 'linktime_composition', 'listener', 'I heard', 'B', 5),
         ('component_dlopen', 'listener', 'manual_composition', 'I heard', 'A', 5)]


def artifacts(work):
    """(lib name -> file) for everything a ROS process loads, and (bin name -> file)."""
    prefix, install = Path(work) / VERSION / 'prefix', Path(work) / VERSION / 'transport-install'
    ros = Path(work) / 'ros/install'
    libs = {}
    for folder in [prefix / 'lib', install / 'lib', ros / 'lib', *sorted((ros / 'opt').glob('*/lib')), Path(work) / 'third-party/install/lib']:
        # Longest name first: the fully versioned file is the installed one, without the build RUNPATH.
        for f in sorted(folder.glob('*.so*'), reverse=True):
            if f.is_file() and not f.is_symlink():
                libs.setdefault(soname(f) or f.name, f)
    libs['libc++_shared.so'] = CXX_SHARED
    bins = {'dsb_suite': install / 'bin/dsb_suite', 'fdds_dsb_hub': install / 'bin/fdds_dsb_hub', 'stage2_dispose_probe': install / 'bin/stage2_dispose_probe',
            'stage2_node': ros / 'bin/stage2_node',
            'stage2_features': ros / 'bin/stage2_features'}
    for name, path in [('minimal_publisher', ros / 'lib/examples_rclcpp_minimal_publisher/publisher_member_function'),
                       ('minimal_subscriber', ros / 'lib/examples_rclcpp_minimal_subscriber/subscriber_member_function'),
                       ('minimal_action_server', ros / 'lib/examples_rclcpp_minimal_action_server/action_server_member_functions')]:
        if path.exists():
            bins[name] = path
    for package in ('demo_nodes_cpp', 'composition', 'rclcpp_components', 'lifecycle', 'examples_rclcpp_minimal_action_server'):
        for path in sorted((ros / 'lib' / package).glob('*')):
            if path.is_file() and '.so' not in path.name:
                bins[path.name] = path
                if path.name not in PROGRAMS:
                    PROGRAMS.append(path.name)
    return libs, bins


def kill_ros(target):
    """A timed-out run only loses its hdc client, so its board process has to be stopped here."""
    sh(target, 'p=$(pidof %s); [ -z "$p" ] || kill -9 $p' % ' '.join(p for p in PROGRAMS if p != 'fdds_dsb_hub'))


def kill_leftovers(target):
    sh(target, 'p=$(pidof %s); [ -z "$p" ] || kill -9 $p' % ' '.join(PROGRAMS))


def local_sha(path):
    import hashlib
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def deploy(target, root, libs, bins, ros_install):
    sh(target, 'mkdir -p %s/lib %s/bin %s/work' % (root, root, root))
    remote = dict(reversed(l.split()) for l in sh(target, 'cd %s && sha256sum lib/* bin/* 2>/dev/null' % root, 300).splitlines() if len(l.split()) == 2)
    sent = deploy_python_layer(target, root, ros_install)
    for folder, items in (('lib', libs), ('bin', bins)):
        for name, f in sorted(items.items()):
            if remote.get('%s/%s' % (folder, name)) != local_sha(f):
                send(target, f, '%s/%s/%s' % (root, folder, name)); sent += 1
    sh(target, 'chmod 755 %s/bin/*' % root)
    if (Path(ros_install) / 'lib/python3.12/site-packages/rclpy').exists():
        return sent  # the layer carries share/, the index included
    return sent + deploy_index(target, root, ros_install)


def deploy_python_layer(target, root, ros_install):
    """The Python runtime and the installed ROS tree beside the libraries. Absent from a C++ only build."""
    work = Path(ros_install).parents[1]
    python = work / 'python-target'
    if not (Path(ros_install) / 'lib/python3.12/site-packages/rclpy').exists():
        return 0
    # Rewritten on every deploy, in a fixed order with fixed metadata: equal content gives an equal digest, and a rebuilt
    # package can never deploy beside older Python files.
    layer = work / PYTHON_LAYER
    partial = layer.with_suffix('.partial')
    install = Path(ros_install)
    members = [('bin/ros2', install / 'bin/ros2')]
    for name, source in [('python/usr/bin', python / 'usr/bin'), ('python/usr/lib', python / 'usr/lib'), ('python/third_party', python / 'third_party'),
                         ('lib/python3.12', install / 'lib/python3.12')]:
        members += [('%s/%s' % (name, p.relative_to(source).as_posix()), p) for p in sorted(source.rglob('*')) if p.is_file() and '__pycache__' not in p.parts]
    for folder in sorted((install / 'lib').iterdir()):
        if folder.is_dir() and folder.name != 'python3.12' and not folder.name.startswith('cmake'):
            members += [('lib/%s/%s' % (folder.name, p.relative_to(folder).as_posix()), p) for p in sorted(folder.rglob('*')) if p.is_file() and not p.name.endswith('.so')]
    members += [('share/' + p.relative_to(install / 'share').as_posix(), p) for p in sorted((install / 'share').rglob('*'))
                if p.is_file() and not p.name.endswith(('.cmake', '.in', '.pc')) and '/cmake/' not in p.as_posix()]
    with tarfile.open(partial, 'w') as tar:
        for name, path in members:
            data = path.read_bytes()
            if '/ament_index/' in name:
                data = data.replace(b'\r\n', b'\n')  # a CR left in a library path breaks dlopen
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(data), 0o755
            tar.addfile(info, io.BytesIO(data))
    partial.replace(layer)
    digest = local_sha(layer)
    # The marker is written after extraction, so a failed extraction is never taken for a deployed layer.
    if sh(target, 'cat %s/python-layer.done 2>/dev/null' % root).strip() == digest:
        return 0
    send(target, layer, '%s/%s' % (root, PYTHON_LAYER))
    # Files archived on Windows carry no execute bit, and ros2 run only offers executable files.
    text = sh(target, 'cd %s && rm -f python-layer.done && rm -rf python share lib/python3.12 && tar xf %s && echo %s > python-layer.done && '
                      'rm -f %s && echo LAYER_OK' % (root, PYTHON_LAYER, digest, PYTHON_LAYER), 600)
    if 'LAYER_OK' not in text:
        sys.exit('python layer did not extract on %s: %s' % (target, text[-200:]))
    return 1


def deploy_index(target, root, ros_install):
    """The ament resource index: generic pub/sub and class_loader look packages up through it."""
    index = Path(ros_install) / 'share/ament_index'
    with tempfile.TemporaryDirectory() as folder:
        archive = Path(folder) / 'ament_index.tar'
        with tarfile.open(archive, 'w') as tar:
            for path in sorted(index.rglob('*')):
                info = tar.gettarinfo(path, arcname='share/ament_index/' + path.relative_to(index).as_posix())
                info.mtime, info.uid, info.gid, info.uname, info.gname = 0, 0, 0, '', ''
                info.mode = 0o755 if path.is_dir() else 0o644
                if path.is_dir():
                    tar.addfile(info)
                else:
                    # Written on the Windows build host. A CR left in rclcpp_components' library path breaks dlopen.
                    data = path.read_bytes().replace(b'\r\n', b'\n')
                    info.size = len(data)
                    tar.addfile(info, io.BytesIO(data))
        if sh(target, 'sha256sum %s/ament_index.tar 2>/dev/null' % root).split()[:1] == [local_sha(archive)]:
            return 0
        send(target, archive, root + '/ament_index.tar')
    sh(target, 'cd %s && rm -rf share/ament_index && tar xf ament_index.tar' % root, 120)
    return 1


def node_info(target, env, token):
    text = sh(target, '%s ./bin/dsb_suite node token=%s' % (env, token))
    m = re.search(r'DSB_NODE network=(\S+) udid=(\S+)', text)
    if not m:
        sys.exit('node query failed on %s: %s' % (target, text))
    return {'network': m.group(1), 'udid': m.group(2)}


def write_config(target, work, index, run_id, a, nodes, side, tokens, deployed=False):
    """deployed writes what a real deployment has: no go file, and a network id the hub replaces from the peer's UDID."""
    peer = nodes['B' if side == 'A' else 'A']
    text = ''.join('%s=%s\\n' % kv for kv in [
        ('side', index), ('run', run_id), ('domain', a.domain), ('local_udid', nodes[side]['udid']),
        ('peer_udid', peer['udid']), ('peer_network', 'resolved_at_start' if deployed else peer['network']), ('token', tokens[side]),
        ('go_file', '/dev/null' if deployed else work + '/go')])
    sh(target, 'mkdir -p %s/log && printf "%s" > %s/s2.conf' % (work, text, work))


class Hubs:
    """One hub per board, as a deployment runs them: supervised and independent of any ROS process."""

    def __init__(self, a, root, boards, nodes, tokens, log):
        self.boards, self.log, self.root, self.recover_after = boards, log, root, a.recover_bus_after
        self.run_id = secrets.token_hex(8)
        self.work = '%s/work/hub-%s' % (root, self.run_id)
        self.socket = self.work + '/hub.sock'
        self.lib = '%s/lib' % root
        for index, (side, target) in enumerate(boards.items()):
            write_config(target, self.work, index, self.run_id, a, nodes, side, tokens, deployed=a.start_apart > 0)
        self.start_apart = a.start_apart
        self.runs = {}
        self.start()

    def start(self):
        for side, target in self.boards.items():
            recovery = '--recover-bus-after %d ' % self.recover_after if self.recover_after else ''
            self.runs[side] = Run(target, 'cd %s && LD_LIBRARY_PATH=%s ./bin/fdds_dsb_hub --supervise %s%s/s2.conf %s; echo S2_EXIT=$?'
                                  % (self.root, self.lib, recovery, self.work, self.socket), self.log)
            time.sleep(self.start_apart)  # with a deployed config nothing coordinates the boards
        if not self.start_apart:
            if not wait_until(lambda: all(r.seen('FDDS_HUB_LISTENING') for r in self.runs.values()), 60):
                sys.exit('hub not listening: %s' % {s: r.lines[-3:] for s, r in self.runs.items()})
            for target in self.boards.values():
                sh(target, 'touch %s/go' % self.work)
        if not wait_until(lambda: all(r.seen('FDDS_HUB_LINK_READY') for r in self.runs.values()), 90):
            sys.exit('hub link not ready: %s' % {s: r.lines[-3:] for s, r in self.runs.items()})
        time.sleep(1)

    def stats(self):
        """Counters of every worker now, {side: dict}. Workers answer on their own output, so this polls it."""
        before = {s: sum('FDDS_HUB_STATS' in l for l in r.lines) for s, r in self.runs.items()}
        for target in self.boards.values():
            sh(target, 'p=$(pidof fdds_dsb_hub); [ -z "$p" ] || kill -USR1 $p')
        wait_until(lambda: all(sum('FDDS_HUB_STATS' in l for l in r.lines) > before[s] for s, r in self.runs.items()), 10)
        result = {}
        for side, run in self.runs.items():
            rows = [l for l in run.lines if 'FDDS_HUB_STATS' in l]
            result[side] = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', rows[-1])} if rows else {}
        return result

    def stop(self):
        for target in self.boards.values():
            sh(target, 'p=$(pidof fdds_dsb_hub); [ -z "$p" ] || kill -TERM $p')
        wait_runs(list(self.runs.values()), 15)


class Pair:
    """Per-run configs and the launch line for one ROS process on each board."""

    def __init__(self, a, root, boards, nodes, tokens, log, hubs=None):
        self.a, self.root, self.boards, self.nodes, self.tokens, self.log, self.hubs = a, root, boards, nodes, tokens, log, hubs
        self.run_id = secrets.token_hex(8)
        self.work = '%s/work/%s' % (root, self.run_id)
        if hubs:
            for target in boards.values():
                sh(target, 'mkdir -p %s/log' % self.work)
            return
        for index, (side, target) in enumerate(boards.items()):
            write_config(target, self.work, index, self.run_id, a, nodes, side, tokens)

    def line(self, side, program, overrides=None):
        env = {'LD_LIBRARY_PATH': self.root + '/lib', 'AMENT_PREFIX_PATH': self.root, 'HOME': self.work, 'ROS_HOME': self.work,
               'ROS_LOG_DIR': self.work + '/log', 'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp', 'ROS_DOMAIN_ID': self.a.domain,
               'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SYSTEM_DEFAULT'}
        if self.hubs:
            env.update({'FASTDDS_DSOFTBUS_HUB': self.hubs.socket, 'LD_PRELOAD': '%s/lib/%s' % (self.root, HUB_PRELOAD)})
        else:
            env.update({'FASTDDS_DSOFTBUS_AUTOLOAD_CONFIG': self.work + '/s2.conf',
                        'LD_PRELOAD': (self.a.diagnostic_preload + ':' if self.a.diagnostic_preload else '') + '%s/lib/%s' % (self.root, AUTOLOAD)})
        env.update(overrides or {})
        prefix = ' '.join('%s=%s' % kv for kv in env.items() if kv[1] is not None)
        return 'cd %s && %s ./bin/%s; echo S2_EXIT=$?' % (self.root, prefix, program)

    def start(self, side, program, overrides=None):
        return Run(self.boards[side], self.line(side, program, overrides), self.log)

    def go(self):
        for target in self.boards.values():
            sh(target, 'touch %s/go' % self.work)

    def begin(self, runs, watched, may_drop=False):
        """Start the socket watcher before any traffic, so a short run still has samples, then open the gate.
        may_drop says the run breaks or overloads the link on purpose, so the hub may drop frames, as a network would."""
        counters = self.hubs.stats() if self.hubs else None
        watch = SocketWatch(watched, self.hubs, counters, may_drop)
        if self.hubs:
            return watch
        wait_until(lambda: all(r.seen('FDDS_AUTOLOAD_READY') or r.p.poll() is not None for r in runs), 60)
        time.sleep(3)
        self.go()
        return watch


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
    if any('CLOSE_INCOMPLETE' in l for l in run.lines):
        return False
    return any(l.startswith('FDDS_AUTOLOAD_CLOSED ok=1') for l in run.lines) or not any(l.startswith('FDDS_AUTOLOAD') for l in run.lines)


def send_failure_with_peer_alive(log_path):
    """A send may fail once the peer has shut down; before that it is a Transport failure."""
    finished, early = set(), []
    for line in Path(log_path).read_text(encoding='utf-8').splitlines():
        board = line[1:7]
        if 'S2_SUMMARY' in line:
            finished.add(board)
        elif ('FDDS_AUTOLOAD_SEND_FAILURE' in line or 'FDDS_HUB_SEND_FAILURE' in line) and not finished - {board}:
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

    def __init__(self, boards_programs, hubs=None, counters=None, may_drop=False):
        self.boards_programs, self.samples, self.stopping = boards_programs, {s: [] for s in boards_programs}, threading.Event()
        self.hubs, self.counters, self.may_drop = hubs, counters, may_drop
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _loop(self):
        while not self.stopping.wait(1):
            for side, (target, program) in self.boards_programs.items():
                sample = udp_socket_count(target, program)
                if sample is not None:
                    self.samples[side].append(sample)

    def hub_counts(self):
        """What each hub's link carried during this run. A hub is shared, so only the difference counts."""
        delta = stats_delta(self.counters, self.hubs.stats())
        return {s: delta[s] for s in self.hubs.boards}

    def verdict(self, runs, minimum_records):
        """runs: {side: Run}. Each side's Transport must have carried at least minimum_records each way."""
        self.stopping.set(); self.thread.join()
        sockets = {s: {'samples': len(v), 'max_udp': max(v, default=None)} for s, v in self.samples.items()}
        transport = self.hub_counts() if self.hubs else {s: transport_counts(run) for s, run in runs.items()}
        clean = self.may_drop or all(v.get('lost', 0) == 0 for v in transport.values())
        # A process can end before its first sample, so one sampled board is enough; the counters are required of both.
        ok = clean and (any(v['samples'] > 0 for v in sockets.values()) and all(v['max_udp'] in (0, None) for v in sockets.values()) and
              all(v['sdk_sends'] >= minimum_records and v['received_records'] >= minimum_records for v in transport.values()))
        return {'sockets': sockets, 'transport': transport, 'minimum_records': minimum_records, 'path_ok': ok}


def stats_delta(before, after):
    lost = ('dropped_out', 'dropped_in', 'dropped_client', 'refused', 'send_failures')
    return {s: {'sdk_sends': after[s]['sdk_sends'] - before[s]['sdk_sends'], 'received_records': after[s]['sdk_records'] - before[s]['sdk_records'],
                'lost': sum(after[s][k] - before[s][k] for k in lost)} for s in after}


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
    watch = pair.begin(runs, {s: (t, 'stage2_node') for s, t in pair.boards.items()})
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


def run_unmodified(pair, sender, receiver, sentinel='I heard', printer=1, minimum=5):
    """Two unmodified ROS binaries, one per board, stopped by SIGINT like an operator would."""
    runs = [pair.start('A', sender), pair.start('B', receiver)]
    watch = pair.begin(runs, {'A': (pair.boards['A'], sender), 'B': (pair.boards['B'], receiver)})
    count = lambda: sum(sentinel in l for l in runs[printer].lines)
    heard = wait_until(lambda: count() >= minimum, 60)
    time.sleep(8)  # long enough for the socket watcher to sample a short run
    for side, name in (('A', sender), ('B', receiver)):
        sh(pair.boards[side], 'p=$(pidof %s); [ -z "$p" ] || kill -INT $p' % name)
    wait_runs(runs, 30)
    result = {'heard': count(), 'exit': [exit_code(r) for r in runs], 'closed': [closed(r) for r in runs]}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, max(1, min(result['heard'], minimum))))  # 0 would pass with no traffic at all
    result['ok'] = heard and all(c == 0 for c in result['exit']) and all(result['closed']) and result['path_ok']
    return result


def mode_smoke(a, pair):
    """Upstream sample binaries that link rclcpp directly."""
    return run_unmodified(pair, 'minimal_publisher', 'minimal_subscriber')


def mode_components(a, pair):
    """demo_nodes_cpp talker/listener, which load their node through class_loader."""
    return run_unmodified(pair, 'talker', 'listener')


def mode_demos(a, pair_factory):
    chosen = DEMOS if a.groups == 'all' else [d for d in DEMOS if d[0] in a.groups.split(',')]
    rows = []
    for name, sender, receiver, sentinel, side, minimum in chosen:
        result = run_unmodified(pair_factory(), sender, receiver, sentinel, 0 if side == 'A' else 1, minimum)
        result['case'] = name
        rows.append(result)
        print('demo', name, 'PASS' if result['ok'] else 'FAIL', {k: result[k] for k in ('heard', 'exit', 'closed', 'path_ok')}, flush=True)
    return {'rows': rows, 'ok': bool(rows) and all(r['ok'] for r in rows)}


def mode_features(a, pair_factory):
    """Each group gets its own process pair, so one crash cannot hide another group's result."""
    groups = [g for g in FEATURE_CASES if a.hub or g not in DIRECT_ONLY_LIMIT] if a.groups == 'all' else a.groups.split(',')
    rows = []
    for group in groups:
        pair = pair_factory()
        limit = '' if a.hub else ' 25'  # the direct path's known limit for a second Participant: the 20 s lease
        runs = [pair.start('A', 'stage2_features 0 %s%s' % (group, limit)), pair.start('B', 'stage2_features 1 %s%s' % (group, limit))]
        # large writes two 16 MiB samples at once, more than the hub's 32 MiB link queue holds at link speed, so frames can
        # drop and RELIABLE repairs them. The cases still require every sample intact, and the count is reported.
        watch = pair.begin(runs, {s: (t, 'stage2_features') for s, t in pair.boards.items()}, may_drop=group == 'large')
        wait_runs(runs, a.timeout)
        result = check_pair(runs, ['A', 'B'])
        result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 10))
        result.update(group=group, expected_cases=2 * FEATURE_CASES[group])
        result['ok'] = result['ok'] and result['path_ok'] and len(result['cases']) == result['expected_cases']
        rows.append(result)
        for row in result['cases']:
            print('case', group, row['board'], row['name'], 'PASS' if row['pass'] == '1' else 'FAIL', row.get('detail', ''), flush=True)
        print('group', group, 'PASS' if result['ok'] else 'FAIL', {k: result[k] for k in ('exit', 'closed', 'path_ok')},
              'cases', len(result['cases']), 'of', result['expected_cases'], flush=True)
        for target in pair.boards.values():
            kill_ros(target)
    return {'rows': rows, 'ok': bool(rows) and all(r['ok'] for r in rows)}


def heard(run, text='I heard'):
    return sum(text in l for l in run.lines)


def mode_multiprocess(a, pair):
    """An ordinary ROS graph: several unmodified processes per board, on the same and on different boards.

    Also the two events such a graph must survive: SIGKILL of one process leaves the others running, and a process
    started late joins without anyone restarting."""
    victim_run = pair.start('B', 'listener --ros-args -r __node:=listener_b1')
    # Taken now, while it is the board's only listener: the board's grep stops at the NUL bytes in /proc/<pid>/cmdline.
    wait_until(lambda: sh(pair.boards['B'], 'pidof listener').strip(), 10)
    victim = sh(pair.boards['B'], 'pidof listener').split()[0]
    runs = {'A.talker': pair.start('A', 'talker'), 'A.listener': pair.start('A', 'listener --ros-args -r __node:=listener_a'),
            'B.listener1': victim_run, 'B.listener2': pair.start('B', 'listener --ros-args -r __node:=listener_b2'),
            'A.server': pair.start('A', 'add_two_ints_server'), 'B.publisher': pair.start('B', 'minimal_publisher'),
            'A.subscriber': pair.start('A', 'minimal_subscriber')}
    watch = pair.begin(list(runs.values()), {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'minimal_publisher')})
    hearing = ('A.listener', 'B.listener1', 'B.listener2', 'A.subscriber')
    all_heard = wait_until(lambda: all(heard(runs[n]) >= 5 for n in hearing), 90)
    client = pair.start('B', 'add_two_ints_client')
    wait_runs([client], 60)
    called = heard(client, 'Result of add_two_ints') == 1 and exit_code(client) == 0
    # SIGKILL one of the two listeners on B: nothing else may notice.
    sh(pair.boards['B'], 'kill -9 %s' % victim)
    # B.listener2 shares a hub with the killed process, so it is the closest witness.
    before = {n: heard(runs[n]) for n in ('A.listener', 'A.subscriber', 'B.listener2')}
    time.sleep(6)
    survived = all(heard(runs[n]) >= before[n] + 3 for n in before)
    late = pair.start('B', 'listener --ros-args -r __node:=listener_late')
    late_heard = wait_until(lambda: heard(late) >= 3, 45)
    for side in ('A', 'B'):
        sh(pair.boards[side], 'p=$(pidof talker listener add_two_ints_server minimal_publisher minimal_subscriber); [ -z "$p" ] || kill -INT $p')
    everything = list(runs.values()) + [late]
    wait_runs(everything, 40)
    exits = {n: exit_code(r) for n, r in list(runs.items()) + [('B.late', late)]}
    killed = [n for n, c in exits.items() if c != 0]  # must be exactly B.listener1
    result = {'all_heard': all_heard, 'heard': {n: heard(runs[n]) for n in hearing}, 'client': called, 'survived': survived, 'late_heard': late_heard, 'exits': exits}
    result.update(watch.verdict({n: r for n, r in runs.items()}, 20))
    # Exactly one process died, the one SIGKILL hit (exit status 137 from the shell).
    result['ok'] = all_heard and called and survived and late_heard and result['path_ok'] and killed == ['B.listener1'] and exits['B.listener1'] == 137
    return result


def worker_pid(target):
    """The hub worker, not its supervisor: the one whose parent is another hub process."""
    pids = sh(target, 'pidof fdds_dsb_hub').split()
    for pid in pids:
        parent = sh(target, 'cut -d" " -f4 /proc/%s/stat' % pid).strip()
        if parent in pids:
            return pid
    return None


def mode_hub_restart(a, pair):
    """SIGKILL a board's hub under live traffic. Its worker restarts and the ROS processes reattach by themselves."""
    seconds = 60
    runs = [pair.start('A', 'stage2_node stream 0 %d' % seconds), pair.start('B', 'stage2_node stream 1 %d' % seconds)]
    watch = pair.begin(runs, {s: (t, 'stage2_node') for s, t in pair.boards.items()}, may_drop=True)
    matched = wait_until(lambda: all(r.seen('S2_STREAM_MATCHED') for r in runs), 120)
    time.sleep(8)
    victim = worker_pid(pair.boards['B'])
    sh(pair.boards['B'], 'kill -9 %s' % victim)
    wait_runs(runs, seconds + 90)
    numbers = {'A': stream_numbers(runs[0]), 'B': stream_numbers(runs[1])}
    result = check_pair(runs, ['A', 'B'])
    delivered = all(numbers.values()) and (numbers['A']['received'] >= numbers['B']['sent'] - ALLOWED_TAIL_LOSS and
                                           numbers['B']['received'] >= numbers['A']['sent'] - ALLOWED_TAIL_LOSS)
    result.update(matched=matched, numbers=numbers, victim=victim, delivered_all=delivered,
                  reattached={s: r.seen('FDDS_HUB_CLIENT_RECONNECTED') for s, r in zip('AB', runs)})
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 20))
    result['ok'] = result['ok'] and matched and delivered and result['path_ok'] and result['reattached']['B']
    return result


def mode_kill_peer(a, pair):
    """SIGKILL a ROS process on B under load; a new process takes its place while A keeps running."""
    long_run = pair.start('A', 'stage2_node stream 0 70')
    first = pair.start('B', 'stage2_node stream 1 60')
    watch = pair.begin([long_run, first], {'A': (pair.boards['A'], 'stage2_node'), 'B': (pair.boards['B'], 'stage2_node')})
    matched = wait_until(lambda: long_run.seen('S2_STREAM_MATCHED') and first.seen('S2_STREAM_MATCHED'), 120)
    time.sleep(10)
    sh(pair.boards['B'], 'kill -9 $(pidof stage2_node)')
    wait_runs([first], 20)
    time.sleep(3)
    second = pair.start('B', 'stage2_node stream 1 25')
    wait_runs([second], 120)
    wait_runs([long_run], 150)
    result = check_pair([long_run, second], ['A', 'B2'])
    numbers = {'A': stream_numbers(long_run), 'B2': stream_numbers(second)}
    result.update(watch.verdict({'A': long_run, 'B2': second}, 50))
    resumed = all(numbers.values()) and numbers['B2']['received'] > 100 and numbers['A']['received'] > numbers['B2']['sent'] - ALLOWED_TAIL_LOSS
    result.update(matched=matched, numbers=numbers, first_exit=exit_code(first), resumed=resumed)
    result['ok'] = result['ok'] and matched and resumed and result['path_ok'] and exit_code(first) == 137
    return result


def mode_sigterm(a, pair):
    """SIGTERM, as a service manager stops a node."""
    runs = [pair.start('A', 'talker'), pair.start('B', 'listener')]
    watch = pair.begin(runs, {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'listener')})
    hearing = wait_until(lambda: heard(runs[1]) >= 5, 60)
    for side, name in (('B', 'listener'), ('A', 'talker')):
        sh(pair.boards[side], 'kill -TERM $(pidof %s)' % name)
    wait_runs(runs, 30)
    result = {'heard': heard(runs[1]), 'exit': [exit_code(r) for r in runs], 'closed': [closed(r) for r in runs]}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 5))
    result['ok'] = hearing and all(c == 0 for c in result['exit']) and all(result['closed']) and result['path_ok']
    return result


def mode_second_process(a, pair):
    """On the direct path a second process on one config used to look healthy and hear nothing. It must be refused."""
    runs = [pair.start('A', 'talker'), pair.start('B', 'listener')]
    watch = pair.begin(runs, {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'listener')})
    hearing = wait_until(lambda: heard(runs[1]) >= 3, 60)
    second = pair.start('B', 'listener')
    wait_runs([second], 20)
    before = heard(runs[1])
    time.sleep(4)
    undisturbed = heard(runs[1]) >= before + 2
    for side, name in (('A', 'talker'), ('B', 'listener')):
        sh(pair.boards[side], 'p=$(pidof %s); [ -z "$p" ] || kill -INT $p' % name)
    wait_runs(runs, 30)
    result = {'second_exit': exit_code(second), 'refused': second.seen('FDDS_AUTOLOAD_REFUSED'), 'undisturbed': undisturbed, 'exit': [exit_code(r) for r in runs]}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 3))
    result['ok'] = hearing and result['second_exit'] == REFUSED_EXIT and result['refused'] and undisturbed and result['exit'] == [0, 0] and result['path_ok']
    return result


def send_keystore(a, pair):
    """Ship a host-generated SROS2 keystore; keys are made off the board, as a deployment would."""
    source = Path(a.work) / 'keystore'
    if not (source / 'enclaves').exists():
        sys.exit('missing %s: create it with ros2 security create_keystore and create_enclave /s2/talker /s2/listener' % source)
    with tempfile.TemporaryDirectory() as folder:
        archive = Path(folder) / 'keystore.tar'
        with tarfile.open(archive, 'w') as tar:
            tar.add(source, arcname='keystore')
        for target in pair.boards.values():
            send(target, archive, pair.work + '/keystore.tar')
            sh(target, 'cd %s && tar xf keystore.tar' % pair.work, 60)
    return pair.work + '/keystore'


def mode_security(a, pair):
    """SROS2 Enforce between boards. Enforcement has to be seen refusing: nothing here counts until a process with no
    identity is shown to hear nothing, while one with an identity hears everything."""
    store = send_keystore(a, pair)
    secure = {'ROS_SECURITY_KEYSTORE': store, 'ROS_SECURITY_ENABLE': 'true', 'ROS_SECURITY_STRATEGY': 'Enforce'}
    talker = pair.start('A', 'talker --ros-args -e /s2/talker', secure)
    listener = pair.start('B', 'listener --ros-args -e /s2/listener', secure)
    outsider = pair.start('B', 'listener --ros-args -r __node:=outsider')
    refused = pair.start('B', 'listener --ros-args -r __node:=no_enclave -e /s2/not_in_keystore', secure)
    watch = pair.begin([talker, listener, outsider], {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'listener')})
    hearing = wait_until(lambda: heard(listener) >= 8, 90)
    wait_runs([refused], 15)
    time.sleep(6)
    for side, name in (('B', 'listener'), ('A', 'talker')):
        sh(pair.boards[side], 'p=$(pidof %s); [ -z "$p" ] || kill -INT $p' % name)
    wait_runs([talker, listener, outsider], 30)
    result = {'heard': heard(listener), 'outsider_heard': heard(outsider), 'outsider_exit': exit_code(outsider), 'no_enclave_exit': exit_code(refused), 'no_enclave_heard': heard(refused),
              'exit': [exit_code(talker), exit_code(listener)]}
    result.update(watch.verdict({'A': talker, 'B': listener}, 8))
    # An encrypted exchange carries the handshake too, so the secure listener must hear while the others stay silent.
    # The outsider must have run the whole time: one that crashed at startup would also hear nothing.
    result['ok'] = hearing and result['outsider_heard'] == 0 and result['outsider_exit'] == 0 and result['no_enclave_heard'] == 0 and result['no_enclave_exit'] not in (0, None) and \
        result['exit'] == [0, 0] and result['path_ok']
    return result


def rss_kb(target, program):
    """Resident memory of every process with that name, in kB, summed."""
    text = sh(target, 'for p in $(pidof %s); do grep VmRSS /proc/$p/status; done' % program)
    return sum(int(v) for v in re.findall(r'VmRSS:\s+(\d+)', text))


def mode_scale(a, pair):
    """Eight processes per board, four publishing and four subscribing, on topics that cross the boards."""
    runs, listeners = [], []
    for i in range(4):
        runs.append(pair.start('A', 'talker --ros-args -r chatter:=/s2a%d -r __node:=talker_a%d' % (i, i)))
        runs.append(pair.start('B', 'talker --ros-args -r chatter:=/s2b%d -r __node:=talker_b%d' % (i, i)))
        listeners.append(pair.start('B', 'listener --ros-args -r chatter:=/s2a%d -r __node:=listener_b%d' % (i, i)))
        listeners.append(pair.start('A', 'listener --ros-args -r chatter:=/s2b%d -r __node:=listener_a%d' % (i, i)))
    everything = runs + listeners
    watch = pair.begin(everything, {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'talker')})
    all_hearing = wait_until(lambda: all(heard(r) >= 5 for r in listeners), 120)
    first = [heard(r) for r in listeners]
    time.sleep(a.seconds or 60)
    rates = [heard(r) - b for r, b in zip(listeners, first)]
    for target in pair.boards.values():
        sh(target, 'p=$(pidof talker listener); [ -z "$p" ] || kill -INT $p')
    wait_runs(everything, 60)
    expected = int((a.seconds or 60) * 0.9)  # talker publishes at 1 Hz
    result = {'all_hearing': all_hearing, 'heard_in_window': rates, 'expected_at_least': expected, 'exits': [exit_code(r) for r in everything]}
    result.update(watch.verdict({str(i): r for i, r in enumerate(everything)}, 100))
    result['ok'] = all_hearing and min(rates) >= expected and all(c == 0 for c in result['exits']) and result['path_ok']
    return result


def mode_soak(a, pair):
    """A long reliable stream both ways plus a local process, watching for loss and for memory that keeps growing."""
    seconds = a.seconds or 1800
    runs = [pair.start('A', 'stage2_node stream 0 %d' % seconds), pair.start('B', 'stage2_node stream 1 %d' % seconds)]
    side = [pair.start('A', 'talker'), pair.start('A', 'listener'), pair.start('B', 'listener')]
    watch = pair.begin(runs, {s: (t, 'stage2_node') for s, t in pair.boards.items()})
    matched = wait_until(lambda: all(r.seen('S2_STREAM_MATCHED') for r in runs), 120)
    time.sleep(60)  # memory settles after discovery
    samples = []
    end = time.time() + seconds - 60
    while time.time() < end:
        samples.append({n: rss_kb(t, 'fdds_dsb_hub') for n, t in pair.boards.items()})
        time.sleep(min(60, max(1, end - time.time())))
    wait_runs(runs, 120)
    for target in pair.boards.values():
        sh(target, 'p=$(pidof talker listener); [ -z "$p" ] || kill -INT $p')
    wait_runs(side, 30)
    numbers = {'A': stream_numbers(runs[0]), 'B': stream_numbers(runs[1])}
    result = check_pair(runs, ['A', 'B'])
    delivered = all(numbers.values()) and (numbers['A']['received'] >= numbers['B']['sent'] - ALLOWED_TAIL_LOSS and
                                           numbers['B']['received'] >= numbers['A']['sent'] - ALLOWED_TAIL_LOSS)
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, seconds))
    growth = {n: (samples[-1][n] - samples[0][n]) if samples else None for n in pair.boards}
    stats = pair.hubs.stats()
    drops = {n: stats[n]['dropped_out'] + stats[n]['dropped_in'] + stats[n]['dropped_client'] for n in stats}
    result.update(matched=matched, numbers=numbers, delivered_all=delivered, hub_rss_first=samples[0] if samples else None, hub_rss_last=samples[-1] if samples else None,
                  hub_rss_growth_kb=growth, hub_drops=drops, side_exits=[exit_code(r) for r in side])
    # A bounded allowance: allocator state may move; a leak of one frame per sample would exceed it within the run.
    result['ok'] = result['ok'] and matched and delivered and result['path_ok'] and all(v == 0 for v in drops.values()) and \
        all(v is not None and v < 8192 for v in growth.values()) and all(c == 0 for c in result['side_exits'])
    return result


def outage_rules(pair, add):
    ips = ipv4(pair.boards['A'])
    action = '-I' if add else '-D'
    return '; '.join('iptables %s INPUT -s %s -j DROP; iptables %s OUTPUT -d %s -j DROP' % (action, p, action, p) for p in ips)


def mode_long_outage(a, pair):
    """An outage longer than the 20 s lease. Volatile samples sent during it are gone by design, and Participants expire,
    so the check is that traffic returns by itself once the route does, with no process restarted."""
    seconds = a.seconds or 45
    runs = [pair.start('A', 'talker'), pair.start('B', 'listener')]
    watch = pair.begin(runs, {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'listener')}, may_drop=True)
    hearing = wait_until(lambda: heard(runs[1]) >= 5, 60)
    sh(pair.boards['B'], outage_rules(pair, True))
    time.sleep(seconds)
    sh(pair.boards['B'], outage_rules(pair, False))
    restored = time.time()
    before = heard(runs[1])
    recovered = wait_until(lambda: heard(runs[1]) >= before + 3, a.timeout if a.timeout < 300 else 180)
    recovery = round(time.time() - restored, 1)
    nodes = sh(pair.boards['A'], 'hidumper -s 4700 -a "buscenter -l remote_device_info" 2>/dev/null | grep "remote device num"')
    for side, name in (('A', 'talker'), ('B', 'listener')):
        sh(pair.boards[side], 'p=$(pidof %s); [ -z "$p" ] || kill -INT $p' % name)
    wait_runs(runs, 30)
    result = {'outage_seconds': seconds, 'recovered': recovered, 'recovery_seconds': recovery, 'heard': heard(runs[1]), 'bus': nodes.strip(),
              'exit': [exit_code(r) for r in runs], 'hub_restarts': {s: sum('FDDS_HUB_WORKER_EXIT' in l for l in r.lines) for s, r in pair.hubs.runs.items()}}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 1))
    # The hubs' counters restart with their workers here, so the record counts do not apply.
    result['ok'] = hearing and recovered and result['exit'] == [0, 0] and all(v['max_udp'] in (0, None) for v in result['sockets'].values())
    return result


def mode_dispose(a, pair):
    """A clean exit must reach the peer as a removal, at once. A lease expiry would be DROPPED and take 20 s."""
    runs = [pair.start('A', 'stage2_dispose_probe stay %s' % a.domain), pair.start('B', 'stage2_dispose_probe leave %s' % a.domain)]
    watch = pair.begin(runs, {'A': (pair.boards['A'], 'stage2_dispose_probe'), 'B': (pair.boards['B'], 'stage2_dispose_probe')})
    wait_runs(runs, 90)
    line = next((l for l in runs[0].lines if l.startswith('S2_DISPOSE')), '')
    seen = {k: float(v) for k, v in re.findall(r'(\w+)=([0-9.]+)', line)}
    result = {'line': line, 'exit': [exit_code(r) for r in runs], 'closed': [closed(r) for r in runs]}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 1))
    result['ok'] = seen.get('removed') == 1 and seen.get('dropped') == 0 and seen.get('seconds', 99) < 8 and result['exit'] == [0, 0] and \
        all(result['closed']) and result['path_ok']
    return result


def mode_no_hub(a, pair):
    """With no hub a process must fail, never reach for UDP or shared memory."""
    for target in pair.boards.values():
        sh(target, 'p=$(pidof fdds_dsb_hub); [ -z "$p" ] || kill -TERM $p')
    wait_runs(list(pair.hubs.runs.values()), 15)
    time.sleep(1)
    run = pair.start('A', 'stage2_node suite 0 topic')
    wait_runs([run], 40)
    result = {'exit': exit_code(run), 'refused': run.seen('FDDS_HUB_CLIENT_REFUSED'), 'cases_run': len(cases(run, 'A'))}
    result['ok'] = result['exit'] not in (0, None) and result['refused'] and result['cases_run'] == 0
    pair.hubs.start()
    return result


# Below the board root: everything Python runs from there, built by build_ros2_ohos.py --python-target and
# build_ros2_python.py. Nothing outside it is on any path, so a missing file fails rather than finding an older copy.
PYTHON_LAYER = 'python-layer.tar'
# (case, command, what its output must contain). Every command runs on B; the nodes it talks about run on A.
CLI_CASES = [
    ('pkg_list', 'pkg list', 'rclcpp'),
    ('interface_show', 'interface show std_msgs/msg/String', 'string data'),
    ('daemon_start', 'daemon start', 'daemon'),
    ('topic_list', 'topic list', '/chatter'),
    ('topic_list_no_daemon', 'topic list --no-daemon --spin-time 5', '/chatter'),
    ('topic_info', 'topic info /chatter --verbose', 'Node name: talker'),
    ('topic_echo', 'topic echo /chatter --once', "data: 'Hello World"),
    ('topic_hz', 'topic hz /chatter --window 3', 'average rate'),
    ('topic_bw', 'topic bw /chatter --window 3', 'B/s'),
    ('topic_pub', 'topic pub --once --wait-matching-subscriptions 1 /s2cli std_msgs/msg/String "{data: from_cli}"', 'publishing'),
    ('node_list', 'node list', '/talker'),
    ('node_info', 'node info /talker', 'Publishers:'),
    ('service_list', 'service list', '/add_two_ints'),
    ('service_type', 'service type /add_two_ints', 'example_interfaces/srv/AddTwoInts'),
    ('service_call', 'service call /add_two_ints example_interfaces/srv/AddTwoInts "{a: 2, b: 3}"', 'sum=5'),
    ('param_list', 'param list /parameter_blackboard', 'use_sim_time'),
    ('param_set', 'param set /parameter_blackboard s2cli 41', 'Set parameter successful'),
    ('param_get', 'param get /parameter_blackboard s2cli', 'Integer value is: 41'),
    ('param_dump', 'param dump /parameter_blackboard', 's2cli: 41'),
    ('action_list', 'action list', '/fibonacci'),
    ('action_send_goal', 'action send_goal /fibonacci example_interfaces/action/Fibonacci "{order: 5}" --feedback', 'Goal finished with status: SUCCEEDED'),
    ('lifecycle_nodes', 'lifecycle nodes', '/lc_talker'),
    ('lifecycle_configure', 'lifecycle set /lc_talker configure', 'Transitioning successful'),
    ('lifecycle_get', 'lifecycle get /lc_talker', 'inactive'),
    ('component_types', 'component types', 'composition::Talker'),
    ('component_load', 'component load /ComponentManager composition composition::Listener', 'Loaded component'),
    ('component_list', 'component list /ComponentManager', '/listener'),
    ('doctor', 'doctor --report', 'middleware name'),
    ('rclpy_client', 'run demo_nodes_py add_two_ints_client', 'Result of add_two_ints'),
    ('daemon_stop', 'daemon stop', 'daemon'),
]


# Commands that run until stopped: (case, command, seconds, text in its own output, text heard on A's /s2cli listener).
# {work} is the run's directory on B.
LONG_CASES = [
    ('rclpy_publisher', 'run demo_nodes_py talker --ros-args -r chatter:=/s2cli', 10, 'Publishing', 'Hello World'),
    ('rclpy_subscriber', 'run demo_nodes_py listener', 10, 'I heard', None),
    ('run_cpp', 'run demo_nodes_cpp listener', 10, 'I heard', None),
    ('launch', 'launch {work}/s2.launch.py', 14, 'I heard', None),
    ('bag_record_sqlite3', 'bag record -o {work}/bag_sql -s sqlite3 /chatter', 10, 'Recording', None),
    ('bag_record_mcap', 'bag record -o {work}/bag_mcap -s mcap /chatter', 10, 'Recording', None),
    ('bag_record_zstd', 'bag record -o {work}/bag_zstd -s sqlite3 --compression-mode file --compression-format zstd /chatter', 10, 'Recording', None),
]
BAG_CASES = [('bag_info_sqlite3', 'bag info {work}/bag_sql', 'Messages:'), ('bag_info_mcap', 'bag info {work}/bag_mcap', 'Messages:'),
             ('bag_info_zstd', 'bag info {work}/bag_zstd', 'zstd')]
BAG_PLAY = [('bag_play_sqlite3', 'bag play {work}/bag_sql --remap /chatter:=/s2bag'), ('bag_play_mcap', 'bag play {work}/bag_mcap --remap /chatter:=/s2bag')]
LAUNCH_FILE = ("from launch import LaunchDescription\\nfrom launch_ros.actions import Node\\n"
               "def generate_launch_description():\\n    return LaunchDescription(["
               "Node(package='demo_nodes_cpp', executable='listener', name='launched_listener', output='screen'),"
               "Node(package='demo_nodes_py', executable='listener', name='launched_py_listener', output='screen')])\\n")


ORIGIN_PROBE = ("import sys\\nimport rclpy, rclpy.node, yaml, numpy, rosbag2_py, launch, ros2cli.cli\\nrclpy.init()\\n"
                "node = rclpy.create_node('s2_origin')\\nfiles = {l.split()[-1] for l in open('/proc/self/maps') if '/' in l and '.so' in l}\\n"
                "files |= {getattr(m, '__file__', None) or '/system/builtin' for m in list(sys.modules.values())}\\n"
                "files |= {sys.prefix + '/', sys.exec_prefix + '/'} | {p + '/' for p in sys.path if p}\\n"
                "[print('S2_ORIGIN', p) for p in sorted(files)]\\nnode.destroy_node()\\nrclpy.shutdown()\\n")


class PythonOnBoard:
    """Launch lines for the Python layer, attached to the hub like any other ROS process."""

    def __init__(self, pair, side):
        self.pair, self.side = pair, side
        root, hub = pair.root, pair.hubs
        runtime = root + '/python/usr'
        self.env = ' '.join('%s=%s' % kv for kv in [
            ('PATH', '%s/bin:/system/bin:/bin' % runtime),  # rclpy entry scripts start through env python3.12
            ('LD_LIBRARY_PATH', '%s/lib:%s/lib' % (root, runtime)),
            ('PYTHONPATH', '%s/lib/python3.12/site-packages:%s/python/third_party' % (root, root)), ('PYTHONDONTWRITEBYTECODE', '1'), ('PYTHONUNBUFFERED', '1'),
            ('AMENT_PREFIX_PATH', root), ('HOME', pair.work), ('ROS_HOME', pair.work), ('ROS_LOG_DIR', pair.work + '/log'),
            ('ROS_DISTRO', 'jazzy'), ('ROS_VERSION', '2'), ('ROS_PYTHON_VERSION', '3'), ('RMW_IMPLEMENTATION', 'rmw_fastrtps_cpp'),
            ('ROS_DOMAIN_ID', pair.a.domain), ('ROS_AUTOMATIC_DISCOVERY_RANGE', 'SYSTEM_DEFAULT'),
            # python3.12 is a thin launcher, and NumPy's prebuilt modules find the interpreter through this global copy.
            ('FASTDDS_DSOFTBUS_HUB', hub.socket), ('LD_PRELOAD', '%s/lib/libpython3.12.so.1.0:%s/lib/%s' % (runtime, root, HUB_PRELOAD))])
        self.python = '%s/bin/python3.12' % runtime

    def line(self, arguments):
        return 'cd %s && %s %s/bin/ros2 %s; echo S2_EXIT=$?' % (self.pair.work, self.env, self.pair.root, arguments)

    def script(self, path, arguments=''):
        return 'cd %s && %s %s %s %s; echo S2_EXIT=$?' % (self.pair.work, self.env, self.python, path, arguments)

    def start(self, arguments):
        return Run(self.pair.boards[self.side], self.line(arguments), self.pair.log)

    def run(self, arguments, seconds):
        run = self.start(arguments)
        end = time.time() + seconds
        while run.p.poll() is None and time.time() < end:
            time.sleep(0.1)
        return run


def stop_python(target, grace=2):
    """Python processes share one name, so these are found by command line."""
    sh(target, 'p=$(pidof python3.12 listener talker); [ -z "$p" ] || kill -INT $p')
    time.sleep(grace)
    sh(target, 'p=$(pidof python3.12 listener talker); [ -z "$p" ] || kill -9 $p')


def mode_cli(a, pair):
    runs = {'talker': pair.start('A', 'talker'), 'server': pair.start('A', 'add_two_ints_server'), 'params': pair.start('A', 'parameter_blackboard'),
            'cli_listener': pair.start('A', 'listener --ros-args -r chatter:=/s2cli -r __node:=cli_listener'),
            'bag_listener': pair.start('A', 'listener --ros-args -r chatter:=/s2bag -r __node:=bag_listener'),
            'action': pair.start('A', 'minimal_action_server'), 'lifecycle': pair.start('A', 'lifecycle_talker --ros-args -r __node:=lc_talker'),
            'container': pair.start('A', 'component_container')}
    time.sleep(5)
    python = PythonOnBoard(pair, 'B')
    # Where the code really loads from: every mapped library and every imported Python file of a process with the ROS stack
    # loaded, and the interpreter's own prefix and path, so an older tree on the board cannot be in use unnoticed.
    sh(pair.boards['B'], 'printf "%s" > %s/s2_origin.py' % (ORIGIN_PROBE, pair.work))
    origin = Run(pair.boards['B'], python.script(pair.work + '/s2_origin.py'), pair.log)
    wait_runs([origin], 30)
    origins = [l.split()[1] for l in origin.lines if l.startswith('S2_ORIGIN')]
    foreign = [p for p in origins if not p.startswith((pair.root + '/', pair.work, '/system/', '/vendor/', '/chip_prod/', '/lib/ld-musl'))]
    rooted = len(origins) > 400 and not foreign and exit_code(origin) == 0  # the ROS stack is well over this many files
    print('origin files', len(origins), 'outside the deployment', foreign[:8], 'rooted', rooted, flush=True)
    chosen = [c for c in CLI_CASES if a.groups == 'all' or c[0] in a.groups.split(',')]
    rows = []
    for name, command, expected in chosen:
        started = time.time()
        run = python.run(command, 45)
        hung = run.p.poll() is None
        output = '\n'.join(run.lines)
        if hung:  # hz and bw never end by themselves, so they are read and then stopped
            stop_python(pair.boards['B'])
            wait_runs([run], 5)
        ok = expected in output and (hung and name in ('topic_hz', 'topic_bw') or exit_code(run) == 0)
        rows.append({'case': name, 'ok': ok, 'exit': exit_code(run), 'seconds': round(time.time() - started, 1), 'tail': run.lines[-3:] if not ok else []})
        print('cli', name, 'PASS' if ok else 'FAIL', rows[-1]['seconds'], rows[-1]['tail'], flush=True)
    delivered = heard(runs['cli_listener']) >= 1
    work = pair.work
    sh(pair.boards['B'], 'printf "%s" > %s/s2.launch.py' % (LAUNCH_FILE, work))
    for name, command, seconds, expected, on_a in LONG_CASES:
        if a.groups != 'all' and name not in a.groups.split(','):
            continue
        before = sum((on_a or '\0') in l for l in runs['cli_listener'].lines)
        run = python.start(command.format(work=work))
        time.sleep(seconds)
        stop_python(pair.boards['B'], 12 if name.startswith('bag') else 4)
        wait_runs([run], 10)
        output = '\n'.join(run.lines)
        ok = expected in output and (on_a is None or wait_until(lambda: sum(on_a in l for l in runs['cli_listener'].lines) > before + 2, 5))
        rows.append({'case': name, 'ok': ok, 'exit': exit_code(run), 'seconds': seconds, 'tail': run.lines[-3:] if not ok else []})
        print('cli', name, 'PASS' if ok else 'FAIL', rows[-1]['tail'], flush=True)
    if a.groups == 'all':
        for name, command, expected in BAG_CASES:
            run = python.run(command.format(work=work), 30)
            ok = expected in '\n'.join(run.lines) and exit_code(run) == 0
            rows.append({'case': name, 'ok': ok, 'exit': exit_code(run), 'output': [l for l in run.lines if 'Messages' in l or 'Storage' in l]})
            print('cli', name, 'PASS' if ok else 'FAIL', rows[-1]['output'] or run.lines[-3:], flush=True)
        for name, command in BAG_PLAY:
            before = heard(runs['bag_listener'])
            run = python.run(command.format(work=work), 30)
            played = wait_until(lambda: heard(runs['bag_listener']) >= before + 3, 10)
            ok = played and exit_code(run) == 0
            rows.append({'case': name, 'ok': ok, 'exit': exit_code(run), 'heard': heard(runs['bag_listener']) - before, 'tail': run.lines[-3:] if not ok else []})
            print('cli', name, 'PASS' if ok else 'FAIL', rows[-1]['heard'], rows[-1]['tail'], flush=True)
    stop_python(pair.boards['B'])
    for name in ('talker', 'add_two_ints_server', 'parameter_blackboard', 'listener', 'minimal_action_server', 'lifecycle_talker', 'component_container'):
        sh(pair.boards['A'], 'p=$(pidof %s); [ -z "$p" ] || kill -INT $p' % name)
    wait_runs(list(runs.values()), 30)
    return {'rows': rows, 'topic_pub_delivered': delivered, 'origin_files': len(origins), 'origin_foreign': foreign, 'exits': {n: exit_code(r) for n, r in runs.items()},
            'ok': bool(rows) and all(r['ok'] for r in rows) and delivered and rooted}


def mode_route(a, pair):
    seconds = 60
    runs = [pair.start('A', 'stage2_node stream 0 %d' % seconds), pair.start('B', 'stage2_node stream 1 %d' % seconds)]
    watch = pair.begin(runs, {s: (t, 'stage2_node') for s, t in pair.boards.items()}, may_drop=True)
    matched = wait_until(lambda: all(r.seen('S2_STREAM_MATCHED') for r in runs), 120)
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
    # Half, not all: repairs after the outage batch many samples into one record.
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, max(1, min(a_numbers['received'], b_numbers['received']) // 2) if a_numbers and b_numbers else 1))
    result['ok'] = result['ok'] and matched and delivered and stalled and result['path_ok']
    return result


def mode_restart(a, pair):
    """A runs throughout; B exits and a new B process rejoins, reusing the deployment."""
    long_run = pair.start('A', 'stage2_node stream 0 75')
    first = pair.start('B', 'stage2_node stream 1 15')
    watch = pair.begin([long_run, first], {s: (t, 'stage2_node') for s, t in pair.boards.items()})
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
                                ('qos_from_xml', {'RMW_FASTRTPS_USE_QOS_FROM_XML': '1'}),
                                ('environment_file', {'FASTDDS_ENVIRONMENT_FILE': '/data/none.json'}), ('discovery_server', {'ROS_DISCOVERY_SERVER': '192.168.77.202:11811'})]:
            run = pair.start(side, 'stage2_node suite %d topic' % index, overrides)
            wait_runs([run], 30)
            rows.append({'board': side, 'case': name, 'exit': exit_code(run), 'refused': run.seen('FDDS_AUTOLOAD_REFUSED'), 'cases_run': len(cases(run, side))})
    return {'rows': rows, 'ok': all(r['exit'] == REFUSED_EXIT and r['refused'] and r['cases_run'] == 0 for r in rows)}


def mode_second_hub(a, pair):
    """A second hub on one socket path would split the board. It must be refused, and the first must keep working."""
    target = pair.boards['A']
    hubs = pair.hubs
    second = Run(target, 'cd %s && LD_LIBRARY_PATH=%s ./bin/fdds_dsb_hub %s/s2.conf %s; echo S2_EXIT=$?' % (hubs.root, hubs.lib, hubs.work, hubs.socket), pair.log)
    wait_runs([second], 20)
    runs = [pair.start('A', 'talker'), pair.start('B', 'listener')]
    watch = pair.begin(runs, {'A': (pair.boards['A'], 'talker'), 'B': (pair.boards['B'], 'listener')})
    hearing = wait_until(lambda: heard(runs[1]) >= 3, 60)
    for side, name in (('A', 'talker'), ('B', 'listener')):
        sh(pair.boards[side], 'p=$(pidof %s); [ -z "$p" ] || kill -INT $p' % name)
    wait_runs(runs, 30)
    result = {'second_exit': exit_code(second), 'heard': heard(runs[1]), 'exit': [exit_code(r) for r in runs]}
    result.update(watch.verdict({'A': runs[0], 'B': runs[1]}, 3))
    result['ok'] = result['second_exit'] == REFUSED_EXIT and second.seen('FDDS_HUB_REFUSED') and hearing and result['path_ok']
    return result


MODES = {'suite': mode_suite, 'smoke': mode_smoke, 'components': mode_components, 'route': mode_route, 'restart': mode_restart,
         'negative': mode_negative, 'multiprocess': mode_multiprocess, 'hub_restart': mode_hub_restart, 'kill_peer': mode_kill_peer,
         'sigterm': mode_sigterm, 'no_hub': mode_no_hub, 'cli': mode_cli, 'second_process': mode_second_process, 'security': mode_security, 'second_hub': mode_second_hub, 'dispose': mode_dispose, 'scale': mode_scale, 'soak': mode_soak, 'long_outage': mode_long_outage}
HUB_ONLY = {'multiprocess', 'hub_restart', 'no_hub', 'cli', 'security', 'second_hub', 'scale', 'soak', 'long_outage'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True); ap.add_argument('--a', required=True); ap.add_argument('--b', required=True)
    ap.add_argument('--mode', required=True, choices=sorted(MODES) + ['features', 'demos'])
    ap.add_argument('--groups', default='all'); ap.add_argument('--domain', default='151'); ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--board-root', default='/data/local/tmp/fdds_dsb')
    ap.add_argument('--token-process', default='mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16')
    ap.add_argument('--diagnostic-preload', default='', help='board path of an extra library loaded before the autoload library')
    ap.add_argument('--no-deploy', action='store_true'); ap.add_argument('--out'); ap.add_argument('--hub', action='store_true')
    ap.add_argument('--seconds', type=int, default=0, help='length of scale, soak or long_outage')
    ap.add_argument('--start-apart', type=int, default=0, help='start the hubs this many seconds apart, with a deployed config: no go file, peer found by UDID')
    ap.add_argument('--recover-bus-after', type=int, default=0, help='passed to the hub: restart softbus_server after this long without the peer')
    a = ap.parse_args()
    known = {'features': FEATURE_CASES, 'demos': [d[0] for d in DEMOS], 'cli': [c[0] for c in CLI_CASES] + [c[0] for c in LONG_CASES]}.get(a.mode, GROUP_CASES)
    if a.groups != 'all' and not set(a.groups.split(',')) <= set(known):
        sys.exit('unknown group in %s (known: %s)' % (a.groups, ', '.join(known)))
    out = Path(a.out or Path(a.work) / 'runs' / ('%s-%s' % (a.mode, secrets.token_hex(4)))); out.mkdir(parents=True, exist_ok=True)
    log = open(out / 'run.log', 'w', encoding='utf-8')
    root = '%s/%s' % (a.board_root, VERSION)
    boards = {'A': a.a, 'B': a.b}
    libs, a.bins = artifacts(a.work)
    if not a.no_deploy:
        for side, target in boards.items():
            print('deploy', side, 'sent', deploy(target, root, libs, a.bins, Path(a.work) / 'ros/install'), 'of', len(libs) + len(a.bins), flush=True)
    for target in boards.values():
        kill_leftovers(target)
    env = 'cd %s && LD_LIBRARY_PATH=%s/lib' % (root, root)
    tokens = {s: token_of(t, a.token_process) for s, t in boards.items()}
    nodes = {s: node_info(t, env, tokens[s]) for s, t in boards.items()}
    if a.mode in HUB_ONLY and not a.hub:
        sys.exit('--mode %s needs --hub' % a.mode)
    hubs = Hubs(a, root, boards, nodes, tokens, log) if a.hub else None
    new_pair = lambda: Pair(a, root, boards, nodes, tokens, log, hubs)
    pair = new_pair()
    if a.mode in ('features', 'demos'):
        outcome = {'features': mode_features, 'demos': mode_demos}[a.mode](a, new_pair)
    else:
        outcome = MODES[a.mode](a, pair)
    if hubs:
        outcome['hub_stats'] = hubs.stats()
        hubs.stop()
    outcome.update(mode=a.mode, version=VERSION, hub=a.hub, run=pair.run_id, targets=boards, artifacts={'stage2_node': local_sha(a.bins['stage2_node']), 'autoload': local_sha(libs[AUTOLOAD])})
    for target in boards.values():
        kill_leftovers(target)
    (out / 'result.json').write_text(json.dumps(outcome, indent=1))
    print(json.dumps({k: v for k, v in outcome.items() if k != 'artifacts'}, indent=1))
    print('result:', 'PASS' if outcome['ok'] else 'FAIL', 'out=%s' % out)
    return 0 if outcome['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
