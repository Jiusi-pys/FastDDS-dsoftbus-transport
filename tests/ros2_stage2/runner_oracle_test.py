"""Host tests for the acceptance criteria in scripts/release/run_ros2_cross_board.py.

Nothing here touches hdc or a board. The board is an in-memory model of the files under its lib/ and
bin/, driven by the very commands deploy() sends, and the socket sampler is fed canned shell output.

What is pinned down here:

  * a process whose transport path was never observed cannot satisfy the DSoftBus-only requirement,
    and an observed UDP socket rejects the run (the negative control);
  * every recorded rosbag2 message has to replay, and a long-running CLI case has to stop cleanly;
  * a deployment carries a manifest, matches it, and leaves nothing behind that this build did not
    produce, except the files the Python layer owns.
"""
import hashlib
import importlib.util
import re
import subprocess
import tarfile
import sys
import tempfile
import threading
import time
import types
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RUNNER = REPO / 'scripts/release/run_ros2_cross_board.py'


def load_runner():
    """The runner, with its hdc-facing import stubbed, so it loads on a host with no device tooling."""
    stub = types.ModuleType('run_cross_board')
    stub.CXX_SHARED = 'libc++_shared.so'
    stub.Run = object
    stub.ipv4 = lambda *a, **k: None
    stub.send = lambda *a, **k: None
    stub.sh = lambda *a, **k: ''
    stub.soname = lambda *a, **k: None
    stub.token_of = lambda *a, **k: None
    stub.wait_until = lambda pred, seconds: bool(pred())
    sys.modules['run_cross_board'] = stub
    spec = importlib.util.spec_from_file_location('run_ros2_cross_board', RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m = load_runner()
# Captured before anything in this file can rebind it. Nothing does any more — the deploy tests drive the
# real builders — but holding the original is what makes that a fact a reader can check rather than assume.
REAL_BUILD_LAYER = m.build_python_layer


class FakeRun:
    """Just enough of Run for the verdict: the lines the Transport printed as it closed."""

    def __init__(self, sends=20, records=20):
        self.lines = ['FDDS_AUTOLOAD_CLOSED ok=1 sdk_sends=%d received_records=%d' % (sends, records)]


def verdict_for(samples, missed, minimum=5, may_drop=False):
    """A verdict built from given socket evidence, without running the sampler thread."""
    watch = m.SocketWatch.__new__(m.SocketWatch)
    watch.samples, watch.missed, watch.crash = samples, missed, None
    watch.hubs, watch.counters, watch.may_drop = None, None, may_drop
    watch.stopping = threading.Event()
    watch.thread = threading.Thread(target=lambda: None)
    watch.thread.start()
    return m.SocketWatch.verdict(watch, {s: FakeRun() for s in samples}, minimum)


def no_misses(sides):
    return {s: {'nopid': 0, 'unreadable': 0} for s in sides}


def test_sampler_states():
    """A missing observation and a zero reading must never be the same value."""
    outputs = {'SOCKETS udp=0\n': ('live', 0), 'SOCKETS udp=4\n': ('live', 4),
               'S2_NOPID\n': ('nopid', None), 'S2_FDERR\n': ('unreadable', None), '': ('unreadable', None)}
    for text, expected in outputs.items():
        m.sh = lambda target, cmd, timeout=60, _t=text: _t
        assert m.udp_socket_count('board', 'prog') == expected, 'sampler mapped %r wrongly' % text

    for failure in (subprocess.TimeoutExpired(cmd='hdc', timeout=60), OSError('hdc is not on PATH')):
        def explode(target, cmd, timeout=60, _f=failure):
            raise _f

        m.sh = explode
        assert m.udp_socket_count('board', 'prog') == ('unreadable', None), 'a broken hdc call is not a zero'


def test_sampler_fault_is_not_an_unreadable_board():
    """A fault in the sampler must name itself rather than look like a board that could not be read."""
    def bug(target, cmd, timeout=60):
        raise ValueError('a fault in the sampler, not an hdc failure')

    m.sh = bug
    result = m.SocketWatch({'A': ('ta', 'prog')}).verdict({'A': FakeRun()}, 5)
    assert result['sockets_ok'] is False and result['path_ok'] is False
    assert any('socket sampler failed' in r for r in result['socket_reasons']), result['socket_reasons']


def test_unobserved_process_fails():
    """The audited scenario: one board sampled with no UDP socket, the other never sampled at all."""
    result = verdict_for({'A': [0], 'B': []}, {'A': {'nopid': 0, 'unreadable': 0}, 'B': {'nopid': 6, 'unreadable': 0}})
    assert result['path_ok'] is False, 'a process with no transport-path evidence must not pass'
    assert result['sockets_ok'] is False
    assert any('no socket observation on B' in r for r in result['path_reasons']), result['path_reasons']
    assert result['sockets']['B']['max_udp'] is None and result['sockets']['B']['nopid'] == 6


def test_udp_fallback_rejected():
    """Negative control: a board that really does hold a UDP socket fails, however healthy the counters."""
    result = verdict_for({'A': [0, 0], 'B': [0, 2]}, no_misses('AB'))
    assert result['path_ok'] is False, 'deliberate UDP fallback must be rejected'
    assert any('udp sockets held on B' in r for r in result['path_reasons']), result['path_reasons']


def test_clean_run_passes():
    result = verdict_for({'A': [0, 0], 'B': [0, 0]}, no_misses('AB'))
    assert result['path_ok'] is True, result['path_reasons']
    assert result['sockets_ok'] is True and result['path_reasons'] == []


def test_record_counts_separate_from_socket_evidence():
    """mode_long_outage excuses the record counts; it must not thereby excuse the socket evidence."""
    watch = m.SocketWatch.__new__(m.SocketWatch)
    watch.samples, watch.missed, watch.crash = {'A': [0], 'B': [0]}, no_misses('AB'), None
    watch.hubs, watch.counters, watch.may_drop = None, None, True
    watch.stopping = threading.Event()
    watch.thread = threading.Thread(target=lambda: None)
    watch.thread.start()
    thin = m.SocketWatch.verdict(watch, {'A': FakeRun(1, 1), 'B': FakeRun(20, 20)}, 5)
    assert thin['path_ok'] is False and thin['sockets_ok'] is True, 'thin counters, sound socket evidence'

    blind = verdict_for({'A': [0], 'B': []}, no_misses('AB'), may_drop=True)
    assert blind['sockets_ok'] is False, 'may_drop must not excuse a missing observation'


def test_sampler_takes_a_sample_before_waiting():
    """A run that ends in well under the sampling interval still has to leave evidence behind."""
    m.sh = lambda target, cmd, timeout=60: 'SOCKETS udp=0\n'
    watch = m.SocketWatch({'A': ('ta', 'prog'), 'B': ('tb', 'prog')})
    result = watch.verdict({'A': FakeRun(), 'B': FakeRun()}, 5)
    assert all(v['samples'] > 0 for v in result['sockets'].values()), result['sockets']
    assert result['path_ok'] is True, result['path_reasons']


def test_bag_message_count():
    sqlite = ('Files:             bag_sql_0.db3\nStorage id:        sqlite3\n'
              'Messages:          20\nTopic information: Topic: /chatter | Count: 20\n')
    zstd = 'Compression Format: zstd\nCompression Mode:  FILE\nMessages:          18\n'
    assert m.bag_message_count(sqlite) == 20
    assert m.bag_message_count(zstd) == 18
    assert m.bag_message_count('Messages:          0\n') == 0, 'an empty bag reports zero, not nothing'
    assert m.bag_message_count('Storage id: sqlite3\n') is None, 'an absent line is not a count'
    assert m.bag_message_count('Topic information: Count: 99\n') is None, 'the topic line is not the count'


def test_every_replayed_bag_has_a_recorded_count():
    """bag play compares against the count bag info read, so each played bag needs its own info case."""
    recorded = {bag for *_, bag in m.BAG_CASES}
    played = {bag for *_, bag in m.BAG_PLAY}
    assert played <= recorded, 'no recorded count for %s' % sorted(played - recorded)
    assert len(recorded) == len(m.BAG_CASES), 'two info cases share one bag'


def test_empty_bag_does_not_replay():
    """The audited weakness: a bag that recorded nothing must not pass its playback case."""
    assert m.playback_ok(0, 20, 20) is True
    assert m.playback_ok(0, 20, 21) is True, 'a late message from an earlier case is not a shortfall'
    assert m.playback_ok(0, 8, 7) is True, 'the first message goes before the player matches: STAGE2 limit 2'
    assert m.playback_ok(0, 20, 18) is False, 'two messages missing is a shortfall, not the documented head loss'
    assert m.playback_ok(0, 0, 0) is False, 'an empty bag replayed nothing: that is not a pass'
    assert m.playback_ok(0, None, 0) is False, 'an unreadable count is not a pass'
    assert m.playback_ok(1, 20, 20) is False, 'ros2 bag play failed'
    assert m.ALLOWED_PLAYBACK_HEAD_LOSS == 1, 'every recorded run lost at most the first message'
    assert m.playback_ok(0, 1, 0) is False, 'a playback that delivered nothing is not a playback'
    assert m.playback_ok(0, 2, 1) is True, 'one of two is the head loss, and one message did arrive'
    # Still far stronger than the criterion this replaced, which accepted any three messages.
    assert m.playback_ok(0, 20, 3) is False, 'three messages out of twenty is not a replayed recording'


def test_long_case_must_have_stopped_because_it_was_stopped():
    """A startup marker is not a pass. The clean-exit code of each entry point is not known here, so
    the check is that the process did not die on a signal and did report an exit."""
    for code in (0, 2, 130):  # handled, ros2cli's KeyboardInterrupt, the default action
        assert m.stopped_cleanly(code) is True, 'a SIGINT-stopped case reporting %d was failed' % code
    for code in m.CRASH_EXITS:
        assert m.stopped_cleanly(code) is False, 'a process killed by a signal (%d) passed' % code
    assert m.stopped_cleanly(None) is False, 'a run that never reported an exit is not a pass'
    assert 137 in m.CRASH_EXITS, 'a process still running when the SIGINT grace ran out must fail'


def test_bag_sequence():
    """The talker's number is the message identity, so a gap in it is a gap in the recording."""
    assert m.bag_sequence('Hello World: 7') == 7
    assert m.bag_sequence('Hello World: 0') == 0
    assert m.bag_sequence('from_cli') is None, 'a payload with no number is compared by text alone'
    assert m.bag_sequence('Hello World: 7 ') == 7, 'trailing space'
    assert m.bag_sequence('Hello 7: World') is None, 'the number must end the payload'


def test_parse_bag_probe():
    """A probe that skipped or reordered a message must not read as a bag that did."""
    good = ['S2_BAG 0 aa Hello World: 1', 'S2_BAG 1 bb Hello World: 2', 'S2_BAG_END 2']
    assert m.parse_bag_probe(good) == [('aa', 'Hello World: 1'), ('bb', 'Hello World: 2')]
    assert m.parse_bag_probe(['noise'] + good) == [('aa', 'Hello World: 1'), ('bb', 'Hello World: 2')]
    gap = ['S2_BAG 0 aa Hello World: 1', 'S2_BAG 2 cc Hello World: 3']
    assert m.parse_bag_probe(gap) == [('aa', 'Hello World: 1')], 'a skipped index truncates rather than lying'
    assert m.parse_bag_probe(['S2_BAG 0 aa']) == [], 'a line with no payload is not a message'
    assert m.parse_bag_probe([]) == []


def test_heard_payloads():
    """Counting lines cannot say which message arrived; strict completeness needs the payloads."""
    class Fake:
        lines = ['[INFO] [12.3] [bag_listener]: I heard: [Hello World: 1]',
                 '[INFO] [13.3] [bag_listener]: I heard: [Hello World: 2]',
                 '[INFO] [14.3] [bag_listener]: something else']
    assert m.heard_payloads(Fake()) == ['Hello World: 1', 'Hello World: 2']

    class Bracketed:
        lines = ['x: I heard: [a [nested] b]']
    assert m.heard_payloads(Bracketed()) == ['a [nested] b'], 'the payload runs to the last bracket'


def test_subscription_count():
    """Readiness, not a sleep. An output with no count at all is never ready."""
    verbose = ('Type: std_msgs/msg/String\nPublisher count: 0\n\nSubscription count: 1\n\n'
               'Node name: strict_listener1\nNode namespace: /\n')
    assert m.subscription_count(verbose) == 1
    assert m.subscription_count('Subscription count: 0\n') == 0
    assert m.subscription_count('Publisher count: 3\n') is None, 'the publisher line is not the count'
    assert m.subscription_count('') is None


class FakePython:
    """Just enough of PythonOnBoard for the readiness gate: canned probe output, calls recorded."""

    def __init__(self, *texts):
        self.texts, self.asked = list(texts), []

    def probe(self, arguments, seconds=20):
        self.asked.append(arguments)
        return self.texts[min(len(self.asked), len(self.texts)) - 1]


def test_wait_for_subscription():
    """Readiness is established or it is not; no sleep stands in for it, and no output means not ready."""
    ready = FakePython('Subscription count: 1\n')
    assert m.wait_for_subscription(ready, '/s2strict1') is True
    assert len(ready.asked) == 1, 'a subscription already visible costs one invocation'
    assert '/s2strict1' in ready.asked[0] and '--verbose' in ready.asked[0]

    nobody = FakePython('Subscription count: 0\n')
    assert m.wait_for_subscription(nobody, '/t', attempts=3) is False, 'nobody listening is not ready'
    assert len(nobody.asked) == 3, 'the attempts are bounded, not a poll loop'

    blind = FakePython('daemon not running\n')
    assert m.wait_for_subscription(blind, '/t', attempts=2) is False, 'an unreadable graph is not ready'

    late = FakePython('Subscription count: 0\n', 'Subscription count: 1\n')
    assert m.wait_for_subscription(late, '/t', attempts=4) is True, 'a subscriber that appears late is ready'
    assert len(late.asked) == 2, 'it stops asking once ready'


def test_finished_reaps_an_overrunning_invocation():
    """An abandoned ros2 invocation keeps its hub attachment, and a bag player keeps publishing."""
    stopped, waited = [], []

    class Hung:
        p = type('P', (), {'poll': staticmethod(lambda: None)})()  # never exits
        lines = ['Subscription count: 1']

    class Board:
        boards, side = {'B': 'target-b'}, 'B'

    fake = m.PythonOnBoard.__new__(m.PythonOnBoard)
    fake.pair, fake.side = Board(), 'B'
    fake.run = lambda arguments, seconds: Hung()
    real_stop, real_wait = m.stop_python, m.wait_runs
    try:
        m.stop_python = lambda target, grace=2: stopped.append(target)
        m.wait_runs = lambda runs, seconds: waited.append(len(runs))
        assert fake.finished('topic info /t --verbose', 20).lines == ['Subscription count: 1']
        assert stopped == ['target-b'], 'an overrunning invocation was not reaped'
        assert waited == [1], 'the reaped invocation was not waited for'
        assert fake.probe('topic info /t --verbose') == 'Subscription count: 1', 'probe reads finished()'

        stopped[:] = []

        class Exited:
            p = type('P', (), {'poll': staticmethod(lambda: 0)})()
            lines = ['Subscription count: 2']

        fake.run = lambda arguments, seconds: Exited()
        assert fake.probe('topic info /t --verbose') == 'Subscription count: 2'
        assert stopped == [], "a probe that finished on its own must not kill the board's python"
    finally:
        m.stop_python, m.wait_runs = real_stop, real_wait


def test_playback_completeness():
    """Strict fidelity: the replay must BE the recording. Nothing is subtracted to reach that."""
    recorded = [('d1', 'Hello World: 1'), ('d2', 'Hello World: 2'), ('d3', 'Hello World: 3')]
    whole = m.playback_completeness(recorded, ['Hello World: 1', 'Hello World: 2', 'Hello World: 3'])
    assert whole['complete'] is True, whole
    assert whole['recorded'] == 3 and whole['replayed'] == 3
    assert whole['missing_payloads'] == [] and whole['duplicated'] == [] and whole['unknown'] == []
    assert whole['out_of_order'] is False and whole['recorded_gaps'] == []

    assert whole['recording_sound'] is True
    assert 'missing' not in whole, "the smoke row's 'missing' is a count; this key must not collide"

    # The head loss the smoke row tolerates is NOT completeness.
    head = m.playback_completeness(recorded, ['Hello World: 2', 'Hello World: 3'])
    assert head['complete'] is False, 'one missing message is not a complete replay'
    assert head['missing_payloads'] == ['Hello World: 1']

    dup = m.playback_completeness(recorded, ['Hello World: 1', 'Hello World: 1', 'Hello World: 2', 'Hello World: 3'])
    assert dup['complete'] is False and dup['duplicated'] == ['Hello World: 1'], dup

    corrupt = m.playback_completeness(recorded, ['Hello World: 1', 'Hello Vorld: 2', 'Hello World: 3'])
    assert corrupt['complete'] is False, 'an altered payload is not the recorded message'
    assert corrupt['unknown'] == ['Hello Vorld: 2'] and corrupt['missing_payloads'] == ['Hello World: 2']

    swapped = m.playback_completeness(recorded, ['Hello World: 2', 'Hello World: 1', 'Hello World: 3'])
    assert swapped['complete'] is False and swapped['out_of_order'] is True, swapped
    assert swapped['missing_payloads'] == [] and swapped['duplicated'] == [], 'order alone, same messages'

    # A damaged recording is reported against the recording, not against the replay that was faithful.
    gapped = m.playback_completeness([('d1', 'Hello World: 1'), ('d3', 'Hello World: 3')],
                                     ['Hello World: 1', 'Hello World: 3'])
    assert gapped['recorded_gaps'] == [2] and gapped['recording_sound'] is False
    assert gapped['complete'] is True, 'the replay WAS the recording; the recording is the fault'

    collided = m.playback_completeness([('d1', 'same'), ('d2', 'same')], ['same', 'same'])
    assert collided['recorded_collisions'] == ['same'], 'one payload under two digests is bag corruption'
    assert collided['recording_sound'] is False and collided['complete'] is True

    empty = m.playback_completeness([], [])
    assert empty['complete'] is False and empty['recording_sound'] is False, 'an empty bag proves nothing'
    assert m.playback_completeness(recorded, [])['complete'] is False


class Timed:
    """A stream whose payloads become visible at fixed offsets in time, as a real one would.

    Growth has to be driven by the clock, not by how often the lines are read: a fake that advances
    on every access would reveal the late duplicate during the final read as well, and could not
    tell a settle detector apart from a bare count wait.
    """

    def __init__(self, schedule):
        self.schedule, self.start = schedule, time.time()

    @property
    def lines(self):
        elapsed = time.time() - self.start
        return ['I heard: [%s]' % p for p, at in self.schedule if elapsed >= at]


def test_settled_payloads_sees_a_late_duplicate():
    """Waiting for a count cannot see a duplicate: it arrives after the count is satisfied."""
    late = Timed([('a', 0), ('b', 0.1), ('c', 0.2), ('c', 1.0)])
    arrived = m.settled_payloads(late, 0, 3, seconds=6, quiet=5)
    assert arrived == ['a', 'b', 'c', 'c'], 'a duplicate arriving after the count was satisfied was missed'

    short = m.settled_payloads(Timed([('a', 0), ('b', 0)]), 0, 3, seconds=1, quiet=2)
    assert short == ['a', 'b'], 'a shortfall is returned as a shortfall, never padded to the target'

    assert m.settled_payloads(Timed([('x', 0), ('y', 0)]), 1, 1, seconds=2, quiet=2) == ['y'], \
        'measured from `before` onward, so an earlier case cannot be counted twice'


def test_smoke_and_strict_are_separate():
    """The allowance-bearing row must not be mistaken for the completeness row."""
    smoke = [c[0] for c in m.BAG_PLAY]
    assert all('smoke' in name for name in smoke), smoke
    strict = {name for name, *_ in m.BAG_STRICT}
    assert strict and not (strict & set(smoke)), 'the strict rows are cases of their own'
    recorded_bags = {bag for *_, bag in m.BAG_CASES}
    assert {bag for _, bag, *_ in m.BAG_STRICT} <= recorded_bags, 'a strict case reads a bag nothing records'
    topics = [topic for *_, topic, _ in m.BAG_STRICT]
    listeners = [listener for *_, listener in m.BAG_STRICT]
    assert len(set(topics)) == len(topics), 'two strict cases sharing a topic would count each other'
    assert len(set(listeners)) == len(listeners), 'two strict cases sharing a listener would count each other'
    assert 'sys.argv[2]' in m.BAG_READER, 'the reader takes the storage id rather than detecting it'
    assert all(storage in ('sqlite3', 'mcap') for _, _, storage, _, _ in m.BAG_STRICT)


def test_bag_reader_is_in_step_with_what_is_recorded():
    """The reader deserializes as std_msgs/msg/String, so it must be given the recorded topic."""
    assert 'sys.argv[3]' in m.BAG_READER and 'S2_BAG_OTHER' in m.BAG_READER, 'the topic is not filtered'
    recording = [command for name, command, *_ in m.LONG_CASES if name.startswith('bag_record')]
    assert recording, 'no bag_record case to keep in step with'
    for command in recording:
        assert command.rstrip().endswith(m.BAG_RECORD_TOPIC), \
            'bag_record captures a topic the reader is not told about: %r' % command


def digest_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# Nothing here supplies a number the production code is supposed to derive. The archives are built
# by the real build_python_layer and build_index, and the fake extracts those archives, so the host
# count and the board count are compared rather than copied from one another.


class StagedBoard:
    """A board's filesystem under the deployment root, driven by the commands deploy() really sends.

    Artifact files are modelled by digest, as sha256sum reports them; state files and the manifest by
    their text. Symlinks are a map of their own, because activation IS a symlink swap: a fake without
    them could not tell a prepared release from a running one, which is the property under test.

    The shell semantics here are deliberately the real ones, not the convenient ones. In particular
    `mv a b` with b an existing directory moves a INSIDE b, as POSIX and toybox do; modelling that as
    a replace is what hid a wedged-deploy bug through a whole review cycle.

    The knobs are the failures the task asks for evidence about:
      corrupt      paths that arrive with content other than what was sent
      truncate     paths whose upload never completes, i.e. an interrupted upload
      refuse_ln    hard links unavailable, so staging has to fall back to uploading
      stuck_links  a symlink swap that does not take
      immovable    a path mv refuses to move
      die_after    a command substring after which every later command is ignored, to model a kill
    """

    def __init__(self, root='/data/local/tmp/fdds_dsb/2.14.5-ros'):
        self.root = root
        self.files, self.links, self.dirs = {}, {}, set()
        self.commands, self.sent = [], []
        self.corrupt, self.truncate, self.immovable = set(), set(), set()
        self.archives = {}  # remote archive path -> the local file it came from, for tar xf
        self.refuse_ln = self.stuck_links = False
        self.stuck_names = set()  # link names that silently refuse to be written, one at a time
        self.die_after, self.dead = None, False
        self.cwd = root

    # --- helpers -------------------------------------------------------------
    def make(self, path):
        parts = path.strip('/').split('/')
        for depth in range(1, len(parts) + 1):
            self.dirs.add('/' + '/'.join(parts[:depth]))

    def absolute(self, path):
        return path if path.startswith('/') else '%s/%s' % (self.cwd, path)

    def flat(self, folder):
        """Paths directly inside folder/lib and folder/bin, as 'lib/x' relative names."""
        out = {}
        for path, value in self.files.items():
            if path.startswith(folder + '/'):
                rest = path[len(folder) + 1:]
                if rest.count('/') == 1 and rest.split('/')[0] in ('lib', 'bin'):
                    out[rest] = value
        return out

    def resolve(self, path):
        """Follow the release links the way the board would, so a test can ask what root/lib serves.

        Without this, a fake that merely stores symlinks cannot tell a reachable deployment from a
        dangling one, which is how a deploy that deleted the release `current` pointed at went unseen.
        """
        for _ in range(8):
            parts = path.strip('/').split('/')
            for depth in range(len(parts), 0, -1):
                head = '/' + '/'.join(parts[:depth])
                if head in self.links:
                    where = self.links[head]
                    base = head.rsplit('/', 1)[0] if not where.startswith('/') else ''
                    path = '/'.join(filter(None, [base, where] + parts[depth:]))
                    path = path if path.startswith('/') else '/' + path
                    break
            else:
                return path
        return path

    def serves(self, name):
        """True when root/<name> resolves to something the board actually has."""
        where = self.resolve('%s/%s' % (self.root, name))
        return where in self.dirs or where in self.files or any(p.startswith(where + '/') for p in self.files)

    def drop(self, path):
        path = self.absolute(path)
        for store in (self.files, self.links):
            for key in [k for k in store if k == path or k.startswith(path + '/')]:
                del store[key]
        self.dirs = {d for d in self.dirs if d != path and not d.startswith(path + '/')}

    def move(self, source, destination):
        if source in self.immovable:
            return
        if destination in self.dirs or destination in self.files:
            destination = '%s/%s' % (destination, source.rsplit('/', 1)[1])  # POSIX: into, not onto
        for store in (self.files, self.links):
            for key in [k for k in store if k == source or k.startswith(source + '/')]:
                store[destination + key[len(source):]] = store.pop(key)
        for folder in [d for d in list(self.dirs) if d == source or d.startswith(source + '/')]:
            self.dirs.discard(folder)
            self.dirs.add(destination + folder[len(source):])

    # --- the shell -----------------------------------------------------------
    def sh(self, target, cmd, timeout=60):
        self.commands.append(cmd)
        if self.dead:
            return ''
        self.cwd = self.root
        out = ''
        for step in cmd.split(' && '):
            got = self.step(step.strip())
            if got:
                out += got
        if self.die_after and self.die_after in cmd:
            self.dead = True
        return out

    def step(self, cmd):
        quoted = re.findall(r"'([^']+)'", cmd)
        if cmd.startswith('cd '):
            self.cwd = cmd[3:].strip()
            return ''
        if cmd.startswith('mkdir -p '):
            for path in cmd[len('mkdir -p '):].split():
                self.make(self.absolute(path))
            return ''
        if cmd.startswith('rm -rf '):
            for path in cmd[len('rm -rf '):].split():
                self.drop(path)
            return ''
        if cmd.startswith('rm -f '):
            for path in cmd[len('rm -f '):].split():
                # A real rm -f unlinks a symlink as readily as a file, and removing a stale release
                # link is the only place production relies on that.
                self.files.pop(self.absolute(path), None)
                self.links.pop(self.absolute(path), None)
            return ''
        if cmd.startswith('ls -ld '):
            path = self.absolute(cmd[len('ls -ld '):].split()[0])
            if path in self.links:
                return 'lrwxrwxrwx 1 root root 10 Jan 1 00:00 %s -> %s\n' % (path, self.links[path])
            if path in self.dirs:
                return 'drwxr-xr-x 2 root root 4096 Jan 1 00:00 %s\n' % path
            if path in self.files:
                return '-rw-r--r-- 1 root root 10 Jan 1 00:00 %s\n' % path
            return ''
        if cmd.startswith('ls '):
            folder = self.absolute(cmd[3:].split()[0])
            names = {p[len(folder) + 1:].split('/')[0] for p in list(self.files) + list(self.dirs) + list(self.links)
                     if p.startswith(folder + '/')}
            return '\n'.join(sorted(names)) + '\n' if names else ''
        if cmd.startswith('readlink '):
            return (self.links.get(self.absolute(cmd[len('readlink '):].split()[0])) or '') + '\n'
        if cmd.startswith('ln -sfn '):
            where, link = cmd[len('ln -sfn '):].split()
            if not self.stuck_links and link.rsplit('/', 1)[-1] not in self.stuck_names:
                self.links[self.absolute(link)] = where
            return ''
        if cmd.startswith('cat '):
            return self.files.get(self.absolute(cmd[4:].split()[0]), '')
        if cmd.startswith('echo '):
            body = cmd[5:]
            if ' > ' not in body:
                return body.strip() + '\n'  # echo LAYER_OK and friends
            value, path = body.split(' > ', 1)
            self.files[self.absolute(path.strip())] = value.strip() + '\n'
            return ''
        if cmd.startswith('mv '):
            source, destination = [self.absolute(p) for p in cmd[3:].split()]
            self.move(source, destination)
            return ''
        if cmd.startswith('sha256sum'):
            assert quoted or 'sha256sum lib/* bin/*' in cmd, \
                'sha256sum with no operand would read stdin and hang: %r' % cmd
            present = self.flat(self.cwd)
            if quoted:  # named paths: sha256sum prints nothing for one that is not there
                return ''.join('%s  %s\n' % (present[p], p) for p in quoted if p in present)
            return ''.join('%s  %s\n' % (d, p) for p, d in sorted(present.items()))
        if cmd.startswith('find '):
            prefixes = tuple('%s/%s/' % (self.cwd, p) for p in cmd.split()[1:] if not p.startswith('-'))
            return '%d\n' % len([p for p in self.files if p.startswith(prefixes)])
        if cmd.startswith('tar xf '):
            # Extract the archive that was actually sent, regular files only, as tar and find agree.
            local = self.archives.get('%s/%s' % (self.cwd, cmd[len('tar xf '):].split()[0]))
            assert local is not None, 'tar xf of an archive that was never sent: %r' % cmd
            with tarfile.open(local) as tar:
                for member in tar.getmembers():
                    if not member.isfile():
                        continue
                    self.files['%s/%s' % (self.cwd, member.name)] = hashlib.sha256(
                        tar.extractfile(member).read()).hexdigest()
                    self.make('%s/%s' % (self.cwd, member.name.rsplit('/', 1)[0]))
            return ''
        if cmd.startswith('for p in '):
            return self.link_batch(cmd, quoted)
        if cmd.startswith('chmod '):
            return ''
        return ''

    def link_batch(self, cmd, quoted):
        """`for p in '...' '...'; do ln ../<active>/$p $p ...; done`, as one command."""
        if not quoted or self.refuse_ln:
            return ''
        source = re.search(r'ln \.\./([^/]+)/\$p', cmd)
        if not source:
            return ''
        active = '%s/releases/%s' % (self.root, source.group(1))
        for path in quoted:
            have = self.files.get('%s/%s' % (active, path))
            if have is not None:
                self.files['%s/%s' % (self.cwd, path)] = have
        return ''

    def send(self, target, local, remote):
        if self.dead:
            return
        path = remote if remote.startswith('/') else '%s/%s' % (self.root, remote)
        self.sent.append(path[len(self.root) + 1:] if path.startswith(self.root + '/') else path)
        # Matched by suffix: a staged path is relative to the release directory, not to the root, so a
        # test naming 'lib/libb.so' means that file wherever this deploy happens to be putting it.
        hit = lambda names: any(path.endswith('/' + n) or path == n for n in names)
        if hit(self.truncate):
            return  # the upload never completed: nothing lands
        if Path(local).suffix == '.tar':
            self.archives[path] = Path(local)
        text = Path(local).read_text(encoding='utf-8') if Path(local).name in ('manifest.sha256', 'release.marker') else None
        self.files[path] = 'corrupted' if hit(self.corrupt) else (text if text is not None else digest_of(local))
        self.make(path.rsplit('/', 1)[0])


def artifact_tree(folder, python=True):
    """Three build outputs and a ros_install the real layer and index builders can work from.

    It carries the member shapes that have broken a counting rule: lib/<package>/<file>, which a host
    rule of "not directly in lib/ or bin/" counts and a board rule of "find python share lib/python3.12"
    does not, and the directory entries build_index writes, which find -type f never sees. With
    python=False there is no rclpy, so the C++ only path and build_index run instead.
    """
    files = {}
    for name, body in (('lib/liba.so', b'A' * 32), ('lib/libb.so', b'B' * 48), ('bin/prog', b'P' * 16)):
        path = Path(folder) / Path(name).name
        path.write_bytes(body)
        files[name] = path
    install = Path(folder) / 'ros/install'
    made = ['bin/ros2', 'lib/demo_nodes_cpp/talker', 'lib/demo_nodes_cpp/listener',
            'lib/demo_nodes_cpp/libtalker_component.so', 'lib/composition/manual_composition',
            'share/ament_index/resource_index/packages/demo_nodes_cpp',
            'share/ament_index/resource_index/rclcpp_components/composition']
    if python:
        made += ['lib/python3.12/site-packages/rclpy/__init__.py', 'lib/python3.12/site-packages/yaml/__init__.py']
    for name in made:
        where = install / name
        where.parent.mkdir(parents=True, exist_ok=True)
        where.write_bytes(name.encode())
    if python:
        for name in ('usr/bin/python3.12', 'usr/lib/libpython3.12.so.1.0', 'third_party/numpy/__init__.py'):
            where = Path(folder) / 'python-target' / name
            where.parent.mkdir(parents=True, exist_ok=True)
            where.write_bytes(name.encode())
    libs = {'liba.so': files['lib/liba.so'], 'libb.so': files['lib/libb.so']}
    return libs, {'prog': files['bin/prog']}, install, {k: digest_of(v) for k, v in files.items()}


def run_deploy(board, libs, bins, install, preload='', rollback=False):
    """deploy() against the fake board, with NOTHING on the host side stubbed.

    build_python_layer, build_index, extract_python_layer and extract_index all run for real; the fake
    models `tar xf` by extracting the archive that was sent, and `find -type f` by counting the files
    that landed. So the count the host writes into release.marker is compared with a count the board
    derived, rather than with itself. Three rounds of review found a categorical bug in exactly the
    gap a hand-written member set leaves.
    """
    m.sh, m.send = board.sh, board.send
    return m.deploy('target', board.root, libs, bins, install, preload, rollback)


def active_release(board):
    return board.files.get('%s/state/active' % board.root, '').strip()


def current_target(board):
    return board.links.get('%s/%s' % (board.root, m.CURRENT), '')


def test_deploy_activates_only_a_complete_release():
    """The happy path, and the shape everything else is measured against."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, want = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        assert release, 'nothing was recorded as active'
        assert current_target(board) == 'releases/%s' % release, current_target(board)
        for name in m.RELEASE_LINKS:
            assert board.links['%s/%s' % (board.root, name)] == '%s/%s' % (m.CURRENT, name), name
        for path, digest in want.items():
            assert board.files['%s/releases/%s/%s' % (board.root, release, path)] == digest, path
        assert board.files.get('%s/releases/%s/manifest.sha256' % (board.root, release)), 'no manifest'
        assert board.files.get('%s/releases/%s/release.marker' % (board.root, release)), 'no marker'
        assert not board.files.get('%s/state/activating' % board.root), 'the activation marker was left behind'
        assert m.validate_release('target', board.root, release) == []
        before = len(board.sent)
        run_deploy(board, libs, bins, install)
        assert active_release(board) == release and len(board.sent) == before, board.sent[before:]


def test_activation_moves_one_link_so_a_release_cannot_be_half_switched():
    """Five separate swaps could leave lib/ on one release and share/ on another."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        first = active_release(board)
        (Path(folder) / 'libb.so').write_bytes(b'B' * 64)
        libs['libb.so'] = Path(folder) / 'libb.so'
        run_deploy(board, libs, bins, install)
        second = active_release(board)
        assert second != first
        # Every release name still points through the one indirection, which now names the new release.
        assert current_target(board) == 'releases/%s' % second
        for name in m.RELEASE_LINKS:
            assert board.links['%s/%s' % (board.root, name)] == '%s/%s' % (m.CURRENT, name), name
        swaps = [c for c in board.commands if c.startswith('ln -sfn releases/')]
        assert len(swaps) == 2, 'one swap per activation, not one per release name: %s' % swaps


def test_activation_reads_every_link_back_not_only_the_one_that_moved():
    """One name failing to link is the half-switched board the single indirection exists to prevent.

    sh() ignores exit codes, so an ln that failed leaves a name pointing at the old release while
    current reads back correctly. Reading only current would report that as a finished activation.
    """
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        board.stuck_names = {'share'}
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'activation did not take' in str(stop) and 'share' in str(stop), stop
        else:
            raise AssertionError('an activation with one unwritten link was accepted')
        assert active_release(board) == '', 'a half-switched release was recorded as active'
        assert board.files.get('%s/state/activating' % board.root), 'no evidence of the interruption'


def test_no_deploy_refuses_a_board_whose_activation_was_interrupted():
    """state/activating is evidence, and it has to be acted on rather than merely written."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        assert m.pending_activation('target', board.root) == '', 'a clean board has nothing pending'
        board.stuck_links = True
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit:
            pass
        assert board.files.get('%s/state/activating' % board.root), 'no evidence of the interrupted activation'
        # This is what main() consults before it allows --no-deploy, so the guard has a named check.
        assert m.pending_activation('target', board.root), 'the guard would let --no-deploy run anyway'
        run_deploy(board, libs, bins, install) if not setattr(board, 'stuck_links', False) else None
        assert m.pending_activation('target', board.root) == '', 'a completed activation left the marker behind'


def test_failed_preparation_leaves_the_running_deployment_alone():
    """An interrupted upload must not touch what is running."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        first = active_release(board)
        before = dict(board.files)
        (Path(folder) / 'libb.so').write_bytes(b'B' * 64)
        libs['libb.so'] = Path(folder) / 'libb.so'
        board.truncate = {'lib/libb.so'}
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'did not match its manifest' in str(stop), stop
        else:
            raise AssertionError('a release with an interrupted upload was accepted')
        assert active_release(board) == first and current_target(board) == 'releases/%s' % first
        for path in before:
            if path.startswith('%s/releases/%s/' % (board.root, first)):
                assert board.files.get(path) == before[path], 'the running release was modified at %s' % path
        assert not [p for p in board.files if '.partial' in p], 'a failed partial was left on the board'


def test_a_restage_replaces_the_release_rather_than_nesting_inside_it():
    """mv onto an existing directory moves into it, which would wedge every later run."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        # Damage the release so the next deploy of the same build has to stage it again.
        del board.files['%s/releases/%s/lib/liba.so' % (board.root, release)]
        assert m.validate_release('target', board.root, release), 'the damage was not detected'
        run_deploy(board, libs, bins, install)
        assert active_release(board) == release
        assert m.validate_release('target', board.root, release) == [], 'the restage did not repair the release'
        nested = [p for p in list(board.files) + list(board.dirs) if '.partial' in p]
        assert not nested, 'the staged release was moved inside the old one: %s' % nested[:4]


def test_hash_mismatch_is_refused():
    """A file that arrives with the wrong content fails validation and is never activated."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        board.corrupt = {'lib/libb.so'}
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'did not match its manifest' in str(stop), stop
        else:
            raise AssertionError('a release whose content does not match its manifest was accepted')
        assert active_release(board) == '' and current_target(board) == ''


def test_a_stale_library_invalidates_a_release():
    """A file the manifest does not name could satisfy a dependency this build is missing."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        board.files['%s/releases/%s/lib/libstale.so' % (board.root, release)] = 'f' * 64
        broken = m.validate_release('target', board.root, release)
        # Containment, not equality: lib/ is a counted tree, so an added file moves the count as well.
        assert 'extra:lib/libstale.so' in broken, broken


def test_a_missing_dependency_invalidates_a_release():
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        del board.files['%s/releases/%s/lib/liba.so' % (board.root, release)]
        broken = m.validate_release('target', board.root, release)
        assert 'wrong:lib/liba.so' in broken, broken


def test_a_cut_short_layer_tree_invalidates_a_release():
    """lib/ and bin/ are hashed; the layer is most of a release and is checked by its file count."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        assert m.validate_release('target', board.root, release) == []
        before = m.tree_file_count('target', m.release_dir(board.root, release))
        del board.files['%s/releases/%s/python/usr/bin/python3.12' % (board.root, release)]
        broken = m.validate_release('target', board.root, release)
        assert broken and 'release holds' in broken[0], broken
        assert m.tree_file_count('target', m.release_dir(board.root, release)) == before - 1


def test_a_cut_short_index_tree_invalidates_a_release():
    """A C++ only release carries the ament index, and it is counted on the same terms as the layer.

    A hole in share/ament_index is what breaks class_loader and generic pub/sub, so a release with one
    must not validate merely because its libraries do.
    """
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder, python=False)  # no rclpy: build_index runs
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        marker = board.files['%s/releases/%s/release.marker' % (board.root, release)]
        assert marker.startswith('index '), 'this release carries a layer marker, not an index one'
        # The real build_index writes directory entries that find -type f never sees, so this is also
        # where a host count that included them would make every C++ only release unstageable.
        assert m.validate_release('target', board.root, release) == [], \
            m.validate_release('target', board.root, release)
        gone = next(p for p in board.files if '/share/ament_index/' in p and release in p)
        del board.files[gone]
        broken = m.validate_release('target', board.root, release)
        assert broken and 'release holds' in broken[0], broken


def test_a_release_whose_marker_was_replaced_is_refused():
    """The directory name is the content address of the manifest and the marker together."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        counted = m.tree_file_count('target', m.release_dir(board.root, release))
        board.files['%s/releases/%s/release.marker' % (board.root, release)] = 'layer %s %d\n' % ('0' * 64, counted)
        broken = m.validate_release('target', board.root, release)
        assert any('id does not match' in b for b in broken), broken


def test_operator_artifacts_and_runtime_data_survive_a_deploy():
    """Nothing outside a release is created, validated or removed."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        board.files['%s/operator/libdiag.so' % board.root] = 'd' * 64
        board.files['%s/work/abc/s2.conf' % board.root] = 'config'
        preload = '%s/operator/libdiag.so' % board.root
        run_deploy(board, libs, bins, install, preload=preload)
        (Path(folder) / 'libb.so').write_bytes(b'B' * 96)
        libs['libb.so'] = Path(folder) / 'libb.so'
        run_deploy(board, libs, bins, install, preload=preload)
        assert board.files['%s/operator/libdiag.so' % board.root] == 'd' * 64, 'the operator library was touched'
        assert board.files['%s/work/abc/s2.conf' % board.root] == 'config', 'runtime data was touched'


def test_an_operator_library_inside_a_release_is_refused_loudly():
    """A deploy replaces root/lib with a symlink, so a library kept there would vanish."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        for inside in ('%s/lib/libdiag.so', '%s/current/lib/libdiag.so', '%s/releases/x/lib/libdiag.so'):
            board = StagedBoard()
            try:
                run_deploy(board, libs, bins, install, preload=inside % board.root)
            except SystemExit as stop:
                assert 'inside a release directory' in str(stop) and 'operator' in str(stop), stop
            else:
                raise AssertionError('a preload at %s was accepted silently' % (inside % board.root))


def test_rollback_returns_to_the_previous_release_after_checking_it():
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        first = active_release(board)
        (Path(folder) / 'libb.so').write_bytes(b'B' * 72)
        libs['libb.so'] = Path(folder) / 'libb.so'
        run_deploy(board, libs, bins, install)
        second = active_release(board)
        assert second != first and board.files['%s/state/previous' % board.root].strip() == first
        run_deploy(board, libs, bins, install, rollback=True)
        assert active_release(board) == first and current_target(board) == 'releases/%s' % first
        assert board.files['%s/state/previous' % board.root].strip() == second, 'rollback forward is not possible'


def test_rollback_refuses_a_damaged_or_absent_previous_release():
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        try:
            run_deploy(board, libs, bins, install, rollback=True)
        except SystemExit as stop:
            assert 'nothing to roll back to' in str(stop), stop
        else:
            raise AssertionError('a rollback with no previous release was accepted')
        first = active_release(board)
        (Path(folder) / 'libb.so').write_bytes(b'B' * 80)
        libs['libb.so'] = Path(folder) / 'libb.so'
        run_deploy(board, libs, bins, install)
        second = active_release(board)
        del board.files['%s/releases/%s/lib/liba.so' % (board.root, first)]
        try:
            run_deploy(board, libs, bins, install, rollback=True)
        except SystemExit as stop:
            assert 'does not match its manifest' in str(stop), stop
        else:
            raise AssertionError('a rollback to a damaged release was accepted')
        assert active_release(board) == second, 'the running release changed on a refused rollback'


def test_a_refused_rollback_does_not_retire_the_running_layout():
    """The worst case: a rollback that cannot be served must not strip a pre-staging board."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        for name in ('lib', 'bin', 'python', 'share'):
            board.make('%s/%s' % (board.root, name))
        board.files['%s/lib/libold.so' % board.root] = 'o' * 64
        try:
            run_deploy(board, libs, bins, install, rollback=True)
        except SystemExit as stop:
            assert 'nothing to roll back to' in str(stop), stop
        else:
            raise AssertionError('a rollback with no target was accepted')
        assert board.files.get('%s/lib/libold.so' % board.root) == 'o' * 64, \
            'the running deployment was retired by a rollback that deployed nothing'
        assert not [p for p in board.files if '/legacy/' in p], 'the old layout was moved for nothing'


def test_a_failed_deploy_does_not_retire_the_running_layout():
    """Retiring belongs after a validated release is ready, not before the first upload."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        for name in ('lib', 'bin', 'python', 'share'):
            board.make('%s/%s' % (board.root, name))
        board.files['%s/lib/libold.so' % board.root] = 'o' * 64
        board.truncate = {'lib/libb.so'}
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit:
            pass
        else:
            raise AssertionError('a release with an interrupted upload was accepted')
        assert board.files.get('%s/lib/libold.so' % board.root) == 'o' * 64, \
            'a failed deploy left the board with no running deployment'
        assert not [p for p in board.files if '/legacy/' in p], 'the old layout was retired by a failed deploy'


def test_previous_is_not_recorded_until_activation_succeeded():
    """A deploy that died during the swap must not leave previous naming the running release.

    If it did, a later rollback would validate and re-activate the release already running, write
    previous back to it, and leave the genuine earlier release unreferenced for the next prune.
    """
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        first = active_release(board)
        assert board.files.get('%s/state/previous' % board.root, '').strip() == ''
        (Path(folder) / 'libb.so').write_bytes(b'B' * 64)
        libs['libb.so'] = Path(folder) / 'libb.so'
        board.stuck_links = True
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'activation did not take' in str(stop), stop
        else:
            raise AssertionError('an activation that did not take was accepted')
        recorded = board.files.get('%s/state/previous' % board.root, '').strip()
        assert recorded != first, 'previous names the running release, so a rollback would be a no-op'


def test_a_release_without_a_name_leaves_no_link_pointing_into_it():
    """Going from a layer release to a C++ only one must not leave root/python dangling."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        assert board.links.get('%s/python' % board.root), 'the layer release should serve python'
        assert board.serves('python')
        aside = Path(folder) / 'cxx'
        aside.mkdir()
        cxx_libs, cxx_bins, cxx_install, _ = artifact_tree(str(aside), python=False)
        run_deploy(board, cxx_libs, cxx_bins, cxx_install)
        assert active_release(board), 'the C++ only release did not activate'
        assert '%s/python' % board.root not in board.links, \
            'root/python still points into a release that does not serve it'
        for name in ('lib', 'bin', 'share', 'manifest.sha256'):
            assert board.serves(name), '%s is not served' % name


def test_a_kill_between_the_two_renames_is_detected_rather_than_run_against():
    """No marker is written in that window, so the recorded release and what current serves disagree.

    A deploy repairs it. What must not happen is a run proceeding against a board whose root/lib
    resolves to nothing, so the --no-deploy guard checks the active release as well as the marker.
    """
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        del board.files['%s/releases/%s/lib/liba.so' % (board.root, release)]  # force a restage
        board.die_after = '.stale-'  # killed immediately after the release was moved aside
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit:
            pass
        # The kill is the deploy process dying, not the board: it answers commands again afterwards.
        board.dead, board.die_after = False, None
        assert not board.serves('lib'), 'this test no longer reproduces the window it exists for'
        assert m.pending_activation('target', board.root) == '', 'no marker is written in this window'
        # This is exactly what main() consults before it allows --no-deploy to run.
        assert m.half_switched('target', board.root), 'the guard would let --no-deploy run anyway'
        # And a fresh deploy repairs it, which is what the guard tells the operator to do.
        run_deploy(board, libs, bins, install)
        assert board.serves('lib') and board.serves('bin')
        assert m.validate_release('target', board.root, active_release(board)) == []
        assert m.half_switched('target', board.root) == '', 'the repair left the board disagreeing'


def test_a_pre_staging_layout_is_moved_aside_not_deleted():
    """Removing the directories a running deployment uses would be destructive cleanup."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        for name in ('lib', 'bin', 'python', 'share'):
            board.make('%s/%s' % (board.root, name))
        board.files['%s/lib/libold.so' % board.root] = 'o' * 64
        run_deploy(board, libs, bins, install)
        kept = [p for p in board.files if '/legacy/' in p and p.endswith('libold.so')]
        assert kept, 'the pre-staging deployment was deleted rather than moved aside'
        assert active_release(board), 'the deploy did not finish after retiring the old layout'
        for name in m.RELEASE_LINKS:
            assert board.links.get('%s/%s' % (board.root, name)), '%s was not linked' % name


def test_the_previous_release_is_kept_and_older_ones_are_dropped():
    """Rollback needs a target, and nothing older needs to stay."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        ids = []
        for size in (32, 40, 48, 56):
            (Path(folder) / 'libb.so').write_bytes(b'B' * size)
            libs['libb.so'] = Path(folder) / 'libb.so'
            run_deploy(board, libs, bins, install)
            ids.append(active_release(board))
        present = {p[len(board.root) + len('/releases/'):].split('/')[0]
                   for p in board.files if p.startswith('%s/releases/' % board.root)}
        assert present == {ids[-1], ids[-2]}, present


def test_staging_reuses_the_running_release_and_falls_back_to_uploading():
    """Unchanged files are hard-linked from the running release; without ln they are uploaded."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        (Path(folder) / 'libb.so').write_bytes(b'B' * 64)
        libs['libb.so'] = Path(folder) / 'libb.so'
        board.sent = []
        run_deploy(board, libs, bins, install)
        assert not [p for p in board.sent if p.endswith('liba.so')], 'an unchanged library was uploaded again'
        assert [p for p in board.sent if p.endswith('libb.so')], 'the changed library was not uploaded'

        board.refuse_ln = True
        (Path(folder) / 'libb.so').write_bytes(b'B' * 88)
        libs['libb.so'] = Path(folder) / 'libb.so'
        board.sent = []
        run_deploy(board, libs, bins, install)
        assert active_release(board), 'a board without hard links could not be deployed to'
        assert [p for p in board.sent if p.endswith('liba.so')], 'without ln, an unchanged library must be uploaded'


def test_an_immovable_release_directory_is_reported():
    """The guard after the rename exists; this is the path that reaches it."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        board.immovable = set()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        del board.files['%s/releases/%s/lib/liba.so' % (board.root, release)]
        board.immovable = {'%s/releases/%s.partial' % (board.root, release)}
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'did not survive being put in place' in str(stop), stop
            # The message has to say the undo worked, not merely that the rename failed.
            assert 'still serves the release it was running' in str(stop), stop
        else:
            raise AssertionError('a release that could not be renamed into place was accepted')
        # The point of the message is that the board is still usable. A deploy that removed the release
        # root/current serves would leave every link dangling and no rollback target recorded.
        assert board.serves('lib') and board.serves('bin'), \
            'the failed restage left the running deployment unreachable'
        assert board.files.get('%s/releases/%s/manifest.sha256' % (board.root, release)), \
            'the release root/current serves was removed'
        # It is still the damaged release this test created in order to force the restage, so it does
        # not validate. The guarantee is that the board can still run and be redeployed, not that
        # nothing was wrong with it.
        assert m.validate_release('target', board.root, release), 'the damage vanished'


def test_the_origin_check_names_the_active_release():
    """Inside the root stopped meaning inside the release under test when staging arrived."""
    root, work, active = '/data/local/tmp/fdds_dsb/2.14.5-ros', '/data/local/tmp/fdds_dsb/2.14.5-ros/work/run1', 'aaaa1111'
    allowed = m.loaded_from_release(root, active, work)
    ok = ['%s/releases/%s/lib/libfastcdr.so' % (root, active), '%s/lib/libfastcdr.so' % root,
          '%s/python/usr/lib/libpython3.12.so.1.0' % root, '%s/share/ament_index/x' % root,
          work + '/s2.conf', '/system/lib64/libc.so']
    for path in ok:
        assert path.startswith(allowed), 'a legitimate origin was refused: %s' % path
    refused = ['%s/releases/bbbb2222/lib/libfastcdr.so' % root, '%s/legacy/9f/lib/libfastcdr.so' % root,
               '/data/local/tmp/other/lib/libfastcdr.so']
    for path in refused:
        assert not path.startswith(allowed), 'an origin outside the release under test was accepted: %s' % path
    # With no recorded release, a board in the older in-place layout still reads as rooted.
    bare = m.loaded_from_release(root, '', work)
    assert ('%s/lib/libfastcdr.so' % root).startswith(bare)
    assert not ('%s/releases/%s/lib/libfastcdr.so' % (root, active)).startswith(bare)


def test_the_host_and_board_file_counts_agree():
    """The counts come from one rule, checked on a fixture that has a lib/<package>/<file> member.

    This is the shape that separated the two earlier rules: a host rule of "not directly in lib/ or
    bin/" counted it and a board rule of "find python share lib/python3.12" did not, so every release
    carrying a layer would have failed validation on a board while passing here.
    """
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = StagedBoard()
        run_deploy(board, libs, bins, install)
        release = active_release(board)
        marker = board.files['%s/releases/%s/release.marker' % (board.root, release)].split()
        counted = m.tree_file_count('target', m.release_dir(board.root, release))
        assert int(marker[2]) == counted, 'the marker says %s, the board counts %s' % (marker[2], counted)
        staged = '%s/releases/%s/' % (board.root, release)
        assert any(p.startswith(staged + 'lib/') and p[len(staged):].count('/') > 1 for p in board.files), \
            'the fixture lacks the lib/<package>/<file> shape this test exists for'
        # And the rule itself: a package file under lib/ is counted, a direct lib/ entry is too, bin/ is not.
        assert m.counted_members(['lib/demo_nodes_cpp/talker']) == 1
        assert m.counted_members(['lib/liba.so']) == 1
        assert m.counted_members(['bin/ros2']) == 0
        assert m.counted_members(['python/usr/bin/x', 'share/y']) == 2


def test_python_layer_accepts_its_own_members():
    """The drift guard must pass for the layer as it stands, and the count must match what it builds."""
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        install = work / 'ros/install'
        (install / 'lib/python3.12/site-packages/rclpy').mkdir(parents=True)
        (install / 'lib/python3.12/site-packages/rclpy/__init__.py').write_bytes(b'x')
        (install / 'bin').mkdir(parents=True)
        (install / 'bin/ros2').write_bytes(b'#!/usr/bin/env python3\n')
        (install / 'lib/demo_nodes_cpp').mkdir(parents=True)
        (install / 'lib/demo_nodes_cpp/talker').write_bytes(b'elf')
        (install / 'share/ament_index/resource_index').mkdir(parents=True)
        (install / 'share/ament_index/resource_index/packages').write_bytes(b'demo\r\n')
        for name in ('usr/bin', 'usr/lib', 'third_party'):
            (work / 'python-target' / name).mkdir(parents=True)
            (work / 'python-target' / name / 'file').write_bytes(b'y')
        layer, digest, count = REAL_BUILD_LAYER(install)
        assert layer is not None and len(digest) == 64 and layer.exists(), 'the layer was not built'
        # Counted against the paths themselves, not against the production helper, so the two can disagree.
        with tarfile.open(layer) as tar:
            names = tar.getnames()
        assert 'lib/demo_nodes_cpp/talker' in names, 'the fixture lost the package member shape'
        expected = len([n for n in names if n.startswith(('python/', 'share/', 'lib/'))])
        assert count == expected, 'the recorded count %s is not the %s files in a counted tree' % (count, expected)
        assert 'bin/ros2' in names and count < len(names), 'bin/ is outside the counted trees'
        assert m.release_marker('layer', digest, count) == 'layer %s %d' % (digest, count)


def main():
    """Every case runs, even after one fails.

    A single exception used to abort the run, which hid every later case: a change that broke two
    things reported one. Each failure is named on its own line so the whole picture arrives at once.
    """
    tests = [(k, v) for k, v in sorted(globals().items()) if k.startswith('test_') and callable(v)]
    failures = []
    for name, test in tests:
        try:
            test()
        except BaseException as error:  # noqa: BLE001 - a test may fail by assertion or by SystemExit
            failures.append(name)
            print('FAIL %s: %s: %s' % (name, type(error).__name__, error), flush=True)
    if failures:
        print('run_ros2_cross_board oracle tests FAILED (%d of %d): %s'
              % (len(failures), len(tests), ', '.join(failures)), flush=True)
        raise SystemExit(1)
    print('run_ros2_cross_board oracle tests passed (%d cases)' % len(tests))


if __name__ == '__main__':
    main()
