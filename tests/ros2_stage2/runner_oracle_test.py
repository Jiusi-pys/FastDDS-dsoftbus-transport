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
REAL_DEPLOY_LAYER = m.deploy_python_layer  # the deploy tests stub it out; the layer test needs the real one


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


class Board:
    """The plain files under the board's lib/ and bin/, as {'lib/x.so': digest}.

    corrupt: paths that arrive with content other than what was sent.
    stubborn: paths that rm -f does not actually remove, for the prune's own verification.
    """

    def __init__(self, files, root='/data/local/tmp/fdds_dsb/2.14.5-ros', corrupt=(), stubborn=()):
        self.files, self.root, self.corrupt, self.stubborn = dict(files), root, set(corrupt), set(stubborn)
        self.commands, self.sent = [], []

    def sh(self, target, cmd, timeout=60):
        self.commands.append(cmd)
        asked = re.findall(r"'([^']+)'", cmd)
        if 'sha256sum' in cmd:
            assert asked or 'sha256sum lib/* bin/*' in cmd, 'sha256sum with no operand would read stdin: %r' % cmd
            if asked:  # named paths: sha256sum prints nothing for one that is not there
                return ''.join('%s  %s\n' % (self.files[p], p) for p in asked if p in self.files)
            return ''.join('%s  %s\n' % (d, p) for p, d in sorted(self.files.items())  # as the glob gives
                           if p.startswith(('lib/', 'bin/')) and p.count('/') == 1)
        if 'rm -f' in cmd:
            for path in asked:
                if path not in self.stubborn:
                    self.files.pop(path, None)
        return ''

    def send(self, target, local, remote):
        assert remote.startswith(self.root + '/'), remote
        path = remote[len(self.root) + 1:]
        self.sent.append(path)
        self.files[path] = 'corrupted' if path in self.corrupt else digest_of(local)


def artifact_tree(folder):
    """Three local build outputs, and a ros_install whose rclpy makes deploy skip the index step."""
    files = {}
    for name, body in (('lib/liba.so', b'A' * 32), ('lib/libb.so', b'B' * 48), ('bin/prog', b'P' * 16)):
        path = Path(folder) / Path(name).name
        path.write_bytes(body)
        files[name] = path
    install = Path(folder) / 'ros/install'
    (install / 'lib/python3.12/site-packages/rclpy').mkdir(parents=True)
    libs = {'liba.so': files['lib/liba.so'], 'libb.so': files['lib/libb.so']}
    return libs, {'prog': files['bin/prog']}, install, {k: digest_of(v) for k, v in files.items()}


def run_deploy(board, libs, bins, install, preload=''):
    m.sh, m.send = board.sh, board.send
    m.deploy_python_layer = lambda target, root, ros_install: 0
    return m.deploy('target', board.root, libs, bins, install, preload)


def test_deploy_prunes_stale_and_keeps_layer_files():
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, want = artifact_tree(folder)
        board = Board({'lib/liba.so': want['lib/liba.so'],   # already current: must not be re-sent
                       'lib/libold.so': 'f' * 64,            # no longer produced: must go
                       'bin/oldprog': 'e' * 64,
                       'bin/ros2': 'd' * 64})                # the Python layer's: must stay
        run_deploy(board, libs, bins, install)
        assert 'lib/libold.so' not in board.files, 'a library this build does not produce survived'
        assert 'bin/oldprog' not in board.files, 'a stale executable survived'
        assert board.files.get('bin/ros2') == 'd' * 64, 'the prune removed a file the Python layer owns'
        for path, digest in want.items():
            assert board.files[path] == digest, 'deployed content differs at %s' % path
        assert 'lib/liba.so' not in board.sent, 'an unchanged library was sent again'
        assert 'lib/libb.so' in board.sent and 'bin/prog' in board.sent
        assert 'manifest.sha256' in board.sent, 'the deployment carries no manifest'


def test_deploy_rejects_content_that_does_not_match_the_manifest():
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = Board({}, corrupt={'lib/libb.so'})
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'did not match its manifest' in str(stop), stop
        else:
            raise AssertionError('a deployment that does not match its manifest was accepted')


def test_deploy_prunes_more_files_than_fit_in_one_command():
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        stale = {'lib/old%03d.so' % i: '%064d' % i for i in range(90)}
        board = Board(stale)
        run_deploy(board, libs, bins, install)
        assert not [p for p in board.files if p.startswith('lib/old')], 'a long stale list was not fully removed'
        assert len([c for c in board.commands if 'rm -f' in c]) == 3, 'expected the removals to be batched'


def test_deploy_spares_a_library_the_operator_staged():
    """--diagnostic-preload names a library that is already on the board and that this build does not
    produce. Pruning it leaves every ROS process failing on a missing LD_PRELOAD file instead."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = Board({'lib/libdiag.so': 'a' * 64, 'lib/libold.so': 'b' * 64})
        run_deploy(board, libs, bins, install, preload=board.root + '/lib/libdiag.so')
        assert board.files.get('lib/libdiag.so') == 'a' * 64, 'the prune removed the diagnostic preload library'
        assert 'lib/libold.so' not in board.files, 'sparing the preload library stopped the prune working'
    assert m.kept_paths('/r', '/elsewhere/libx.so') == m.LAYER_OWNED_FILES, 'a path outside root needs no sparing'
    assert m.kept_paths('/r', '/r/lib/a.so:/r/lib/b.so') == m.LAYER_OWNED_FILES | {'lib/a.so', 'lib/b.so'}


def test_deploy_reports_a_prune_that_did_not_work():
    """The prune is verified, not assumed: rm -f can fail on a read-only mount or a busy file."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, _ = artifact_tree(folder)
        board = Board({'lib/libstuck.so': 'c' * 64}, stubborn={'lib/libstuck.so'})
        try:
            run_deploy(board, libs, bins, install)
        except SystemExit as stop:
            assert 'stale files remain' in str(stop), stop
        else:
            raise AssertionError('a prune that removed nothing was accepted')


def test_deploy_rehashes_only_what_it_sent():
    """Hashing the whole board twice doubles the slowest part of a deploy. A file left alone was
    already hashed and matched, so only what this deploy sent has to be read back."""
    with tempfile.TemporaryDirectory() as folder:
        libs, bins, install, want = artifact_tree(folder)
        board = Board({p: d for p, d in want.items()})  # everything already current
        run_deploy(board, libs, bins, install)
        assert board.sent == ['manifest.sha256'], board.sent
        hashing = [c for c in board.commands if 'sha256sum' in c]
        assert len(hashing) == 1, 'an unchanged deployment hashed the board more than once: %s' % hashing

        board = Board({'lib/liba.so': want['lib/liba.so']})
        run_deploy(board, libs, bins, install)
        read_back = [c for c in board.commands if 'sha256sum' in c][1:]
        assert read_back and all("'lib/liba.so'" not in c for c in read_back), 'an unsent file was hashed again'
        for path in ('lib/libb.so', 'bin/prog'):
            assert any("'%s'" % path in c for c in read_back), '%s was sent but never read back' % path


def test_python_layer_accepts_its_own_members():
    """The drift guard must pass for the layer as it stands, so LAYER_OWNED_FILES is in step with it."""
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
        board = Board({})
        m.send = board.send
        m.sh = lambda target, cmd, timeout=60: 'LAYER_OK\n' if 'LAYER_OK' in cmd else board.sh(target, cmd, timeout)
        assert REAL_DEPLOY_LAYER('target', board.root, install) == 1, 'the layer did not deploy'
        assert any(p.endswith('.tar') for p in board.sent), board.sent


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith('test_') and callable(v)]
    for test in tests:
        test()
    print('run_ros2_cross_board oracle tests passed (%d cases)' % len(tests))


if __name__ == '__main__':
    main()
