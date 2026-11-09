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
import traceback
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
ALLOWED_PLAYBACK_HEAD_LOSS = 1  # ros2 bag play publishes its first message before its own publisher has matched
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


# Files the Python layer puts straight into lib/ or bin/. They belong to the layer's own digest, not to
# the artifact manifest, so the prune below must leave them alone. deploy_python_layer asserts this stays true.
LAYER_OWNED_FILES = {'bin/ros2'}


BATCH_FILES = 40  # paths per shell command, so a few hundred of them never make one over-long line


def remote_digests(target, root, paths=None):
    """sha256 of files directly in the board's lib/ and bin/, as {'lib/x.so': digest}.

    paths=None hashes everything there. A list hashes only those, in batches short enough for one
    command, and leaves out the ones that do not exist, which is also how a removal is confirmed.
    An empty list runs nothing at all: sha256sum without an operand would read stdin and hang.
    """
    if paths is None:
        batches = ['lib/* bin/*']
    else:
        batches = [' '.join("'%s'" % p for p in paths[i:i + BATCH_FILES]) for i in range(0, len(paths), BATCH_FILES)]
    digests = {}
    for what in batches:
        text = sh(target, 'cd %s && sha256sum %s 2>/dev/null' % (root, what), 300)
        digests.update(dict(reversed(l.split()) for l in text.splitlines() if len(l.split()) == 2))
    return digests


def kept_paths(root, preload):
    """Manifest-relative paths the prune must leave alone.

    The Python layer's own files, and anything the operator staged under root itself: a
    --diagnostic-preload library lives on the board before the run and this build does not produce
    it, so pruning it would leave every ROS process failing on a missing LD_PRELOAD file.
    """
    kept, prefix = set(LAYER_OWNED_FILES), root.rstrip('/') + '/'
    for one in (p for p in preload.split(':') if p):
        if one.startswith(prefix):
            kept.add(one[len(prefix):])
    return kept


def deploy(target, root, libs, bins, ros_install, preload=''):
    sh(target, 'mkdir -p %s/lib %s/bin %s/work' % (root, root, root))
    remote = remote_digests(target, root)
    keep = kept_paths(root, preload)
    sent = deploy_python_layer(target, root, ros_install)
    manifest, changed = {}, []
    for folder, items in (('lib', libs), ('bin', bins)):
        for name, f in sorted(items.items()):
            path = '%s/%s' % (folder, name)
            manifest[path] = local_sha(f)
            if remote.get(path) != manifest[path]:
                send(target, f, '%s/%s' % (root, path)); sent += 1
                changed.append(path)
    # A file this build does not produce must not survive on the board. On a board deployed to before, a
    # left-over library can satisfy a dependency the current build is missing, so the same artifacts pass
    # here and fail on a clean board. lib/ and bin/ are the only directories that need this: the Python
    # layer and the ament index are each re-extracted into a directory that is emptied first.
    stale = sorted(set(remote) - set(manifest) - keep)
    for start in range(0, len(stale), BATCH_FILES):
        sh(target, 'cd %s && rm -f %s' % (root, ' '.join("'%s'" % p for p in stale[start:start + BATCH_FILES])), 300)
    # What actually landed, by content, for the files this deploy sent. The others were hashed on the
    # board a moment ago and already matched; between then and here, the only things that write into
    # lib/ or bin/ are the Python layer, whose files are in keep, and the prune just above.
    after = remote_digests(target, root, changed)
    wrong = sorted(p for p in changed if after.get(p) != manifest[p])
    if wrong:
        sys.exit('deploy did not match its manifest on %s: %s' % (target, wrong[:8]))
    # A digest comes back for a pruned path only if the file is still there. Nothing outside this list
    # can be left over, because remote above listed every file the board had.
    left = sorted(remote_digests(target, root, stale))
    if left:
        sys.exit('stale files remain on %s after the prune: %s' % (target, left[:8]))
    # Beside the artifacts, so the evidence names exactly the set that ran.
    with tempfile.TemporaryDirectory() as folder:
        listing = Path(folder) / 'manifest.sha256'
        listing.write_text(''.join('%s  %s\n' % (d, p) for p, d in sorted(manifest.items())), encoding='utf-8', newline='\n')
        send(target, listing, root + '/manifest.sha256')
    print('deploy', target[-6:], 'manifest', len(manifest), 'pruned', len(stale), stale[:6], flush=True)
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
    direct = {n for n, _ in members if n.count('/') == 1 and n.split('/')[0] in ('lib', 'bin')}
    if direct - LAYER_OWNED_FILES:
        sys.exit('the Python layer writes %s directly into lib/ or bin/: add it to LAYER_OWNED_FILES, '
                 'or deploy() will prune it as stale' % sorted(direct - LAYER_OWNED_FILES))
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
    """Transport-path evidence for one process, as (state, udp_sockets).

    ('live', n)           the process was found and holds n UDP sockets
    ('nopid', None)       no such process at this instant: not started yet, or already gone
    ('unreadable', None)  it exists but could not be sampled: /proc unreadable, or hdc gave nothing

    Only ('live', 0) is evidence of a DSoftBus-only path. A missing observation is not a zero, so the
    two are kept apart all the way into the verdict.
    """
    script = ('p=$(pidof %s | cut -d" " -f1); [ -n "$p" ] || { echo S2_NOPID; exit; }; '
              'f=$(ls -l /proc/$p/fd) || { echo S2_FDERR; exit; }; n=0; '
              'for i in $(echo "$f" | grep -o "socket:\\[[0-9]*\\]" | grep -o "[0-9]*"); do '
              'grep -q " $i " /proc/net/udp /proc/net/udp6 && n=$((n+1)); done; '
              'echo SOCKETS udp=$n' % program)
    try:
        text = sh(target, script)
    except (subprocess.SubprocessError, OSError):  # hdc hung, or could not be run: a missing observation, never a zero
        return ('unreadable', None)
    m = re.search(r'SOCKETS udp=(\d+)', text)
    if m:
        return ('live', int(m.group(1)))
    return ('nopid', None) if 'S2_NOPID' in text else ('unreadable', None)


class SocketWatch:
    """Samples both boards until stopped, so a socket opened late in a run is seen too."""

    def __init__(self, boards_programs, hubs=None, counters=None, may_drop=False):
        self.boards_programs, self.samples, self.stopping = boards_programs, {s: [] for s in boards_programs}, threading.Event()
        self.missed = {s: {'nopid': 0, 'unreadable': 0} for s in boards_programs}
        self.crash = None  # a fault in the sampler itself, kept apart from a board that cannot be read
        self.hubs, self.counters, self.may_drop = hubs, counters, may_drop
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _loop(self):
        # Sampled before the first wait, so a run that ends in under a second still leaves evidence.
        # That is what lets the verdict insist on an observation of every watched process.
        try:
            while True:
                for side, (target, program) in self.boards_programs.items():
                    state, count = udp_socket_count(target, program)
                    if state == 'live':
                        self.samples[side].append(count)
                    else:
                        self.missed[side][state] += 1
                if self.stopping.wait(1):
                    return
        except Exception:
            # Without this the thread would die on its own traceback and the run would report a board
            # that could not be read. The verdict names the sampler instead, and still fails.
            self.crash = traceback.format_exc()
            print('socket sampler failed:', self.crash, flush=True)

    def hub_counts(self):
        """What each hub's link carried during this run. A hub is shared, so only the difference counts."""
        delta = stats_delta(self.counters, self.hubs.stats())
        return {s: delta[s] for s in self.hubs.boards}

    def verdict(self, runs, minimum_records):
        """runs: {side: Run}. Each side's Transport must have carried at least minimum_records each way."""
        self.stopping.set(); self.thread.join()
        sockets = {s: dict({'samples': len(v), 'max_udp': max(v, default=None)}, **self.missed[s]) for s, v in self.samples.items()}
        transport = self.hub_counts() if self.hubs else {s: transport_counts(run) for s, run in runs.items()}
        clean = self.may_drop or all(v.get('lost', 0) == 0 for v in transport.values())
        # Every watched process must have been observed at least once. An unobserved process is missing
        # evidence, not a process free of UDP sockets, so it can never satisfy the DSoftBus-only requirement.
        unobserved = sorted(s for s, v in sockets.items() if v['samples'] == 0)
        with_udp = sorted(s for s, v in sockets.items() if v['max_udp'] not in (None, 0))
        thin = sorted(s for s, v in transport.items()
                      if v['sdk_sends'] < minimum_records or v['received_records'] < minimum_records)
        socket_reasons = []
        if self.crash:
            socket_reasons.append('socket sampler failed: ' + self.crash.strip().splitlines()[-1])
        if unobserved:
            socket_reasons.append('no socket observation on ' + ','.join(unobserved))
        if with_udp:
            socket_reasons.append('udp sockets held on ' + ','.join(with_udp))
        reasons = list(socket_reasons)
        if thin:
            reasons.append('transport carried under %d records on %s' % (minimum_records, ','.join(thin)))
        if not clean:
            reasons.append('hub lost frames')
        # sockets_ok is the socket evidence alone, for the one caller whose record counts cannot apply.
        return {'sockets': sockets, 'transport': transport, 'minimum_records': minimum_records,
                'path_ok': not reasons, 'path_reasons': reasons,
                'sockets_ok': not socket_reasons, 'socket_reasons': socket_reasons}


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


def bag_message_count(text):
    """The count 'ros2 bag info' prints, or None when the line is absent."""
    m = re.search(r'^\s*Messages:\s*(\d+)\s*$', text, re.MULTILINE)
    return int(m.group(1)) if m else None


def stopped_cleanly(code):
    """True when a long-running case ended because it was stopped, not because it died.

    No exit line at all (None) means the run never reported one, which is a failure of its own.
    """
    return code is not None and code not in CRASH_EXITS


def bag_sequence(payload):
    """The talker's sequence number inside a payload, or None when it carries none.

    demo_nodes_cpp talker publishes 'Hello World: N', so the number is the message identity and a
    gap in it is a gap in the recording. Payloads without one are compared by text alone.
    """
    m = re.search(r':\s*(\d+)\s*$', payload)
    return int(m.group(1)) if m else None


def parse_bag_probe(lines):
    """[(digest, payload)] in storage order, from the board-side reader's S2_BAG lines.

    Each line is 'S2_BAG <index> <sha256 of the serialized bytes> <payload>'. The index is the
    reader's own ordering and is checked here, so a probe that skipped or reordered a message
    cannot be mistaken for a bag that did.
    """
    messages = []
    for line in lines:
        parts = line.split(' ', 3)
        if len(parts) == 4 and parts[0] == 'S2_BAG' and parts[1].isdigit() and int(parts[1]) == len(messages):
            messages.append((parts[2], parts[3].rstrip('\n')))
    return messages


def heard_payloads(run, marker='I heard: ['):
    """The payloads a demo_nodes_cpp listener logged, in arrival order.

    Counting lines cannot tell which message arrived, so strict completeness needs the payloads
    themselves: the listener logs 'I heard: [Hello World: 7]'.
    """
    out = []
    for line in run.lines:
        at = line.find(marker)
        if at >= 0:
            rest = line[at + len(marker):]
            end = rest.rfind(']')
            out.append(rest[:end] if end >= 0 else rest.rstrip('\n'))
    return out


def subscription_count(text):
    """The subscription count 'ros2 topic info --verbose' printed, or None when the line is absent.

    None means readiness could not be established, which is never treated as ready.
    """
    m = re.search(r'^\s*Subscription count:\s*(\d+)\s*$', text, re.MULTILINE)
    return int(m.group(1)) if m else None


def settled_payloads(run, before, expected, seconds=30, quiet=5):
    """The payloads that arrived after `before`, read once the stream has stopped growing.

    Waiting for a count cannot see an unexpected duplicate, because the duplicate arrives after the
    count is already satisfied and the measurement has been taken. So this waits for the expected
    count AND for the arrivals to stop changing across `quiet` consecutive polls.

    It is a settle detector, not a sleep standing in for synchronisation: a stream still growing is
    waited on to the deadline and what is returned is whatever really arrived, never a target
    number. A replay that is still delivering at the deadline reports what it had delivered, which
    is a shortfall or a surplus, and either fails.
    """
    end, stable, last = time.time() + seconds, 0, None
    while time.time() < end:
        now = len(heard_payloads(run))
        stable = stable + 1 if now == last else 0
        last = now
        if now - before >= expected and stable >= quiet:
            break
        time.sleep(0.2)
    return heard_payloads(run)[before:]


def wait_for_subscription(python, topic, attempts=6):
    """The readiness gate for a measured replay: the graph must show a subscriber on the topic.

    The player must not publish before the subscriber is matched, and a sleep only guesses at when
    that is. Subscriptions are visible in the ROS graph without any publisher, so this can be asked
    before playback starts, and asking it from the other board makes it a cross-board discovery
    check as well. An output carrying no count at all is never treated as ready.

    Bounded attempts rather than a poll loop, because each attempt is a whole ros2 invocation: it is
    seconds of work, and probe() has to reap it before the next one starts.
    """
    for _ in range(attempts):
        count = subscription_count(python.probe('topic info %s --verbose' % topic))
        if count is not None and count >= 1:
            return True
    return False


def playback_completeness(recorded, replayed):
    """Strict bag-to-replay fidelity, as a verdict naming every discrepancy.

    recorded: [(digest, payload)] the bag itself holds, in storage order.
    replayed: [payload] the subscriber logged, in arrival order.

    complete is True only when the replayed sequence IS the recorded one: same payloads, same
    number of each, same order, nothing missing, nothing duplicated and nothing that was never
    recorded. Nothing is subtracted to reach that: a shortfall is reported as a shortfall.

    recording_sound is a separate verdict on the bag itself, so a fault that happened while
    recording is not reported as a failure of the replay:
      recorded_gaps       missing sequence numbers inside the recording itself
      recorded_collisions one payload stored under two different digests, i.e. corruption in the bag
    A case needs both: a faithful replay of a damaged recording is not end-to-end completeness, but
    the two are named apart so an investigator is pointed at the right half.
    """
    want = [payload for _, payload in recorded]
    counts_want, counts_got = {}, {}
    for payload in want:
        counts_want[payload] = counts_want.get(payload, 0) + 1
    for payload in replayed:
        counts_got[payload] = counts_got.get(payload, 0) + 1
    missing = sorted(p for p, n in counts_want.items() if counts_got.get(p, 0) < n)
    duplicated = sorted(p for p, n in counts_got.items() if p in counts_want and n > counts_want[p])
    unknown = sorted(p for p in counts_got if p not in counts_want)  # never recorded: corrupted or foreign
    same_multiset = counts_want == counts_got
    numbers = [n for n in (bag_sequence(p) for p in want) if n is not None]
    gaps = sorted(set(range(min(numbers), max(numbers) + 1)) - set(numbers)) if len(numbers) > 1 else []
    by_payload = {}
    for digest, payload in recorded:
        by_payload.setdefault(payload, set()).add(digest)
    collisions = sorted(p for p, digests in by_payload.items() if len(digests) > 1)
    # missing_payloads, not missing: the smoke row's 'missing' is a count, and the two must not
    # share a key in the rows this mode returns as evidence.
    return {'recorded': len(want), 'replayed': len(replayed), 'missing_payloads': missing[:8],
            'duplicated': duplicated[:8], 'unknown': unknown[:8],
            'out_of_order': bool(same_multiset and want != replayed),
            'recorded_gaps': gaps[:8], 'recorded_collisions': collisions[:8],
            'recording_sound': bool(want) and not gaps and not collisions,
            'complete': bool(want) and want == replayed}


def playback_ok(code, recorded, replayed):
    """Every message a bag recorded has to arrive on the other board, bar the documented first one.

    A bag that recorded nothing, or whose count could not be read, replays nothing: the info case has
    already failed it, and comparing 0 against 0 here would report an empty playback as a pass.

    ALLOWED_PLAYBACK_HEAD_LOSS covers the message ros2 bag play publishes before its own publisher has
    matched the subscriber, which a volatile reader cannot receive however sound the transport. Every
    recorded run has shown that loss and never more than one message of it; see limit 2 of
    docs/STAGE2_ROS2_DEVICE_20261105.md. Requiring the whole recording would fail a healthy board.

    Something has to arrive, whatever the allowance: in a recording small enough for the head loss to
    cover all of it, a playback that delivered nothing would otherwise be indistinguishable from one
    that worked, and bag info only asks for a non-zero count.
    """
    return (code == 0 and bool(recorded) and replayed > 0
            and replayed >= recorded - ALLOWED_PLAYBACK_HEAD_LOSS)


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
    result['ok'] = hearing and recovered and result['exit'] == [0, 0] and result['sockets_ok']
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
# (case, command, text its own output must carry, the bag it reads). The bag name keys the recorded count.
BAG_CASES = [('bag_info_sqlite3', 'bag info {work}/bag_sql', 'Messages:', 'bag_sql'),
             ('bag_info_mcap', 'bag info {work}/bag_mcap', 'Messages:', 'bag_mcap'),
             ('bag_info_zstd', 'bag info {work}/bag_zstd', 'zstd', 'bag_zstd')]
# The smoke rows. They tolerate ALLOWED_PLAYBACK_HEAD_LOSS and are therefore not evidence of complete
# playback; BAG_STRICT below is. Keeping them is deliberate: they fail on a playback that broke outright,
# on any board, without needing the Python layer or the reader probe.
BAG_PLAY = [('bag_play_smoke_sqlite3', 'bag play {work}/bag_sql --remap /chatter:=/s2bag', 'bag_sql'),
            ('bag_play_smoke_mcap', 'bag play {work}/bag_mcap --remap /chatter:=/s2bag', 'bag_mcap')]
# (case, bag, the topic the measured replay is remapped onto, the listener whose lines are the evidence).
# A topic of its own per storage, so one storage's replay can never be counted as another's.
BAG_STRICT = [('bag_complete_sqlite3', 'bag_sql', 'sqlite3', '/s2strict1', 'strict_listener1'),
              ('bag_complete_mcap', 'bag_mcap', 'mcap', '/s2strict2', 'strict_listener2')]
# A long-running case is stopped by SIGINT, and each entry point reports that in its own way: 0 when it
# handled the signal, 130 when the default action killed it, 2 where ros2cli turns KeyboardInterrupt into
# signal.SIGINT. Which of those each case really reports is not measured: cli mode needs two boards and no
# run record of it exists, so a whitelist of clean exits would fail cases that worked. What is checkable is
# that the process did not die on a fatal signal, and was not still running when stop_python's grace ran
# out and SIGKILL followed.
CRASH_EXITS = (132, 134, 136, 137, 139)  # SIGILL, SIGABRT, SIGFPE, SIGKILL, SIGSEGV
LAUNCH_FILE = ("from launch import LaunchDescription\\nfrom launch_ros.actions import Node\\n"
               "def generate_launch_description():\\n    return LaunchDescription(["
               "Node(package='demo_nodes_cpp', executable='listener', name='launched_listener', output='screen'),"
               "Node(package='demo_nodes_py', executable='listener', name='launched_py_listener', output='screen')])\\n")


# The topic the bag_record cases capture. The reader is given it explicitly, and a test keeps it in step
# with the record commands, because the reader deserializes as std_msgs/msg/String and must not be handed
# a message of another type.
BAG_RECORD_TOPIC = '/chatter'
# Reads a bag back through rosbag2_py and prints one line per stored message of that topic, in storage
# order, with the sha256 of the serialized bytes. The storage id is passed in rather than detected, so a
# bag whose metadata is unreadable fails here instead of silently reading as empty. S2_BAG_END carries the
# count the reader itself saw, so a truncated capture cannot pass for a short bag, and any message of
# another topic is reported rather than deserialized as a String.
BAG_READER = ("import hashlib, sys\\nimport rosbag2_py\\n"
              "from rclpy.serialization import deserialize_message\\nfrom std_msgs.msg import String\\n"
              "reader = rosbag2_py.SequentialReader()\\n"
              "reader.open(rosbag2_py.StorageOptions(uri=sys.argv[1], storage_id=sys.argv[2]), "
              "rosbag2_py.ConverterOptions('', ''))\\n"
              "want = sys.argv[3]\\ni = 0\\nwhile reader.has_next():\\n"
              "    topic, data, stamp = reader.read_next()\\n"
              "    if topic != want:\\n        print('S2_BAG_OTHER', topic)\\n        continue\\n"
              "    print('S2_BAG', i, hashlib.sha256(data).hexdigest(), deserialize_message(data, String).data)\\n"
              "    i += 1\\nprint('S2_BAG_END', i)\\n")

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

    def finished(self, arguments, seconds):
        """Run a ros2 command and reap it if it overruns, instead of leaving it on the board.

        run() leaves an overrunning process alive, which is right for the CLI cases that read a
        stream and stop it themselves. Anything else needs this: an abandoned ros2 invocation keeps
        its hub attachment, and a bag player keeps publishing, for the rest of the mode.
        """
        run = self.run(arguments, seconds)
        if run.p.poll() is None:
            # stop_python is board-wide: it stops every python3.12, listener and talker on this board by
            # name. That is only safe because every caller here has already finished measuring whatever
            # else it started on this side. Do not call this while another run on the same board is
            # still being measured.
            stop_python(self.pair.boards[self.side], 2)
            wait_runs([run], 5)
        return run

    def probe(self, arguments, seconds=20):
        """finished(), as text, for a command that is read rather than judged by its exit code."""
        return '\n'.join(self.finished(arguments, seconds).lines)


def stop_python(target, grace=2):
    """Python processes share one name, so these are found by command line."""
    sh(target, 'p=$(pidof python3.12 listener talker); [ -z "$p" ] || kill -INT $p')
    time.sleep(grace)
    sh(target, 'p=$(pidof python3.12 listener talker); [ -z "$p" ] || kill -9 $p')


def mode_cli(a, pair):
    runs = {'talker': pair.start('A', 'talker'), 'server': pair.start('A', 'add_two_ints_server'), 'params': pair.start('A', 'parameter_blackboard'),
            'cli_listener': pair.start('A', 'listener --ros-args -r chatter:=/s2cli -r __node:=cli_listener'),
            'bag_listener': pair.start('A', 'listener --ros-args -r chatter:=/s2bag -r __node:=bag_listener'),
            # One per storage, up from the start of the mode so each is matched long before its replay.
            'strict_listener1': pair.start('A', 'listener --ros-args -r chatter:=/s2strict1 -r __node:=strict_listener1'),
            'strict_listener2': pair.start('A', 'listener --ros-args -r chatter:=/s2strict2 -r __node:=strict_listener2'),
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
        # Printing a startup marker is not passing: the process also has to have stopped because it was
        # stopped. No exit line at all means the run never reported one, which is a failure of its own.
        code = exit_code(run)
        clean = stopped_cleanly(code)
        ok = (expected in output and clean and
              (on_a is None or wait_until(lambda: sum(on_a in l for l in runs['cli_listener'].lines) > before + 2, 5)))
        rows.append({'case': name, 'ok': ok, 'exit': code, 'stopped_cleanly': clean, 'seconds': seconds,
                     'tail': run.lines[-3:] if not ok else []})
        print('cli', name, 'PASS' if ok else 'FAIL', 'exit=%s' % code, rows[-1]['tail'], flush=True)
    if a.groups == 'all':
        recorded = {}
        for name, command, expected, bag in BAG_CASES:
            run = python.run(command.format(work=work), 30)
            output = '\n'.join(run.lines)
            count = bag_message_count(output)
            recorded[bag] = count
            # The number is the check, not the presence of the word: a bag that recorded nothing is a failure.
            ok = expected in output and exit_code(run) == 0 and count is not None and count > 0
            rows.append({'case': name, 'ok': ok, 'exit': exit_code(run), 'messages': count,
                         'output': [l for l in run.lines if 'Messages' in l or 'Storage' in l]})
            print('cli', name, 'PASS' if ok else 'FAIL', 'messages=%s' % count, rows[-1]['output'] or run.lines[-3:], flush=True)
        for name, command, bag in BAG_PLAY:
            before = heard(runs['bag_listener'])
            want = recorded.get(bag)
            run = python.run(command.format(work=work), 30)
            # Waits for the whole recording and lets playback_ok judge what arrived. Waiting only for the
            # head-loss allowance would stop a message early and report a shortfall that was merely late.
            # The subscriber has been up since the start of the mode, so it is matched well before
            # playback opens its own publisher.
            wait_until(lambda: want is not None and heard(runs['bag_listener']) - before >= want, 20)
            got = heard(runs['bag_listener']) - before
            missing = None if want is None else want - got
            ok = playback_ok(exit_code(run), want, got)
            rows.append({'case': name, 'ok': ok, 'exit': exit_code(run), 'recorded': want, 'replayed': got,
                         'missing': missing, 'tail': run.lines[-3:] if not ok else []})
            print('cli', name, 'PASS' if ok else 'FAIL', 'smoke',
                  'recorded=%s replayed=%s missing=%s' % (want, got, missing), rows[-1]['tail'], flush=True)
        # Strict completeness. The smoke rows above tolerate the documented head loss and so cannot show
        # that a replay carried everything; these read the bag back and compare message by message.
        # A row that cannot establish completeness reports UNVERIFIED and does not pass: the criterion is
        # not weakened to accommodate a probe that did not run.
        sh(pair.boards['B'], 'printf "%s" > %s/s2_bagread.py' % (BAG_READER, work))
        for name, bag, storage, topic, listener in BAG_STRICT:
            row = {'case': name, 'ok': False, 'status': 'UNVERIFIED', 'reason': '', 'bag': bag, 'strict': True}
            if not wait_for_subscription(python, topic):
                row['reason'] = 'no subscription visible on %s, so a replay could not be measured' % topic
            else:
                probe = Run(pair.boards['B'], python.script(work + '/s2_bagread.py',
                            '%s/%s %s %s' % (work, bag, storage, BAG_RECORD_TOPIC)), pair.log)
                wait_runs([probe], 180)
                # stored, not recorded: the BAG_CASES loop above binds `recorded` to a dict of counts,
                # and shadowing it here would break that the moment the two loops were reordered.
                stored = parse_bag_probe(probe.lines)
                ends = [l.split() for l in probe.lines if l.startswith('S2_BAG_END')]
                counted = int(ends[-1][1]) if ends and len(ends[-1]) == 2 and ends[-1][1].isdigit() else None
                others = sorted({l.split(' ', 1)[1] for l in probe.lines if l.startswith('S2_BAG_OTHER ')})
                if exit_code(probe) != 0 or counted is None or counted != len(stored) or not stored or others:
                    row['reason'] = ('the bag could not be read back: exit=%s parsed=%d reader_count=%s other_topics=%s'
                                     % (exit_code(probe), len(stored), counted, others[:4]))
                    row['tail'] = probe.lines[-3:]
                else:
                    before = len(heard_payloads(runs[listener]))
                    play = python.finished('bag play %s/%s --remap %s:=%s'
                                           % (work, bag, BAG_RECORD_TOPIC, topic), 90)
                    # Settled, not counted: a duplicate arrives after the count is already satisfied.
                    # finished() above returned only once the player exited, so this is not racing the
                    # replay: the window has to cover delivery already in flight, not the gap between
                    # two replayed messages, which at a 1 Hz recording would be about the same length.
                    row.update(playback_completeness(stored, settled_payloads(runs[listener], before, len(stored))))
                    row['exit'] = exit_code(play)
                    row['ok'] = bool(row['complete']) and bool(row['recording_sound']) and exit_code(play) == 0
                    row['status'] = 'PASS' if row['ok'] else 'FAIL'
                    row['reason'] = ('' if row['ok'] else
                                     'the recording itself is damaged' if not row['recording_sound'] else
                                     'replay was not the recording')
            rows.append(row)
            print('cli', name, row['status'], row['reason'],
                  {k: row.get(k) for k in ('recorded', 'replayed', 'missing_payloads', 'duplicated', 'unknown',
                                           'out_of_order', 'recorded_gaps', 'recorded_collisions')}, flush=True)
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
            print('deploy', side, 'sent', deploy(target, root, libs, a.bins, Path(a.work) / 'ros/install', a.diagnostic_preload),
                  'of', len(libs) + len(a.bins), flush=True)
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
