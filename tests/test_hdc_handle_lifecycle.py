"""Owned-handle exit transition: synthetic negatives and harmless native child.

No HDC process, board, grant, raw stream or identity change is used here.
"""
import contextlib
import copy
import ctypes
import importlib.util
from pathlib import Path
import subprocess
import sys
import time
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


leaf = load('_terminal_leaf_tests', ROOT/'scripts/hdc_goal/goal_handle_lifecycle.py')


class Ops:
    def __init__(self):
        self.state = 258
        self.image_error = None
        self.image_value = 'synthetic.exe'
        self.owner = (31, 123456789012345678)
        self.pin = (7, 8, 9)
        self.waited = []
        self.wait_result = 0
        self.exit = 0
        self.clock = 10.0
        self.after = None

    def pid_birth(self, handle): return self.owner
    def file_id(self, handle): return self.pin
    def wait(self, handle): return self.state
    def exit_code(self, handle): return self.exit
    def image(self, handle):
        if self.image_error is not None:
            raise leaf.ImageUnavailable(self.image_error)
        return self.image_value
    def wait_terminal(self, handle, ms):
        self.waited.append((handle, ms))
        self.state = self.wait_result
        if self.after is not None:
            self.clock = self.after
        return self.state


def root_fixture():
    binding = leaf.HandleBinding(21, 22, 31, 123456789012345678, 'synthetic.exe', (7, 8, 9))
    owner = {'pid': binding.pid, 'birth': binding.birth, 'image': binding.image}
    return types.SimpleNamespace(binding=binding, authority=True, closed=False,
        pi=types.SimpleNamespace(hProcess=21), image_pin=22, handles=[21, 22],
        owner=dict(owner), terminal_at=None,
        receipt={'created': True, 'bound': True, 'hProcess': 21, 'owner': dict(owner),
                 'FileID': [7, 8, 9], 'terminal': False, 'EOF': True, 'signal_authority': True})


class LifecycleTests(unittest.TestCase):
    def test_live_uses_full_image(self):
        root, ops = root_fixture(), Ops()
        self.assertEqual(leaf.observe_owned_binding(root, ops), 'LIVE')
        self.assertTrue(root.authority)
        self.assertEqual(ops.waited, [])

    def test_original_signaled_handle_proves_terminal_without_image(self):
        root, ops = root_fixture(), Ops()
        ops.state = 0
        ops.image_error = 5
        self.assertEqual(leaf.observe_owned_binding(root, ops), 'TERMINAL')
        self.assertFalse(root.authority)
        self.assertTrue(leaf.terminal_receipt_qualified(root.receipt))

    def test_transition_wait_is_once_readonly_and_bounded(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        result = leaf.observe_binding(root.binding, ops, deadline=11, clock=lambda: ops.clock)
        self.assertEqual(result['phase'], 'TERMINAL')
        self.assertEqual(ops.waited, [(21, 50)])

    def test_transition_wait_clamps_to_remaining_original_deadline(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        leaf.observe_binding(root.binding, ops, deadline=10.021, clock=lambda: ops.clock)
        self.assertEqual(len(ops.waited), 1)
        self.assertGreater(ops.waited[0][1], 0)
        self.assertLessEqual(ops.waited[0][1], 21)

    def test_no_deadline_no_extra_wait(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        with self.assertRaises(leaf.ImageUnavailable):
            leaf.observe_binding(root.binding, ops)
        self.assertEqual(ops.waited, [])

    def test_expired_deadline_no_extra_wait(self):
        for deadline in (9.0, 10.0, 10.0001):
            with self.subTest(deadline=deadline):
                root, ops = root_fixture(), Ops()
                ops.image_error = 5
                with self.assertRaises(leaf.ImageUnavailable):
                    leaf.observe_binding(root.binding, ops, deadline=deadline, clock=lambda: ops.clock)
                self.assertEqual(ops.waited, [])

    def test_persistent_live_error_never_proves_exit(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        ops.wait_result = 258
        with self.assertRaises(leaf.ImageUnavailable):
            leaf.observe_binding(root.binding, ops, deadline=11, clock=lambda: ops.clock)
        self.assertEqual(ops.waited, [(21, 50)])

    def test_failed_wait_never_proves_exit(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        ops.wait_result = 0xffffffff
        with self.assertRaisesRegex(leaf.LifecycleError, 'WAIT_UNKNOWN'):
            leaf.observe_binding(root.binding, ops, deadline=11, clock=lambda: ops.clock)

    def test_terminal_anchors_are_rechecked_after_wait(self):
        for changed in ('pid_birth', 'pin'):
            with self.subTest(changed=changed):
                root, ops = root_fixture(), Ops()
                ops.image_error = 5
                original_wait = ops.wait_terminal
                def wait(handle, ms):
                    result = original_wait(handle, ms)
                    if changed == 'pid_birth': ops.owner = (31, root.binding.birth+1)
                    else: ops.pin = (7, 8, 10)
                    return result
                ops.wait_terminal = wait
                with self.assertRaises(leaf.LifecycleError):
                    leaf.observe_binding(root.binding, ops, deadline=11, clock=lambda: ops.clock)

    def test_wait_return_after_deadline_is_rejected(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        ops.after = 11.0
        with self.assertRaises(leaf.ImageUnavailable):
            leaf.observe_binding(root.binding, ops, deadline=11, clock=lambda: ops.clock)

    def test_clock_regression_rejected(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        ops.after = 9.0
        with self.assertRaisesRegex(leaf.LifecycleError, 'IDENTITY_CLOCK_REGRESSION'):
            leaf.observe_binding(root.binding, ops, deadline=11, clock=lambda: ops.clock)

    def test_error_revokes_operations_but_later_terminal_can_be_observed(self):
        root, ops = root_fixture(), Ops()
        ops.image_error = 5
        with self.assertRaises(leaf.ImageUnavailable):
            leaf.observe_owned_binding(root, ops)
        self.assertFalse(root.authority)
        self.assertFalse(root.receipt['signal_authority'])
        with self.assertRaisesRegex(leaf.LifecycleError, 'TERMINAL_WAIT_NOT_SIGNALED'):
            leaf.observe_owned_binding(root, ops)
        ops.state = 0
        self.assertEqual(leaf.observe_owned_binding(root, ops), 'TERMINAL')
        self.assertFalse(root.authority)
        self.assertFalse(root.receipt['signal_authority'])
        self.assertTrue(leaf.terminal_receipt_qualified(root.receipt))

    def test_terminal_require_live_rejects_without_erasing_terminal_proof(self):
        root, ops = root_fixture(), Ops()
        ops.state = 0
        with self.assertRaisesRegex(leaf.LifecycleError, 'EXPECTED_LIVE_PROCESS_TERMINATED'):
            leaf.observe_owned_binding(root, ops, require_live=True)
        self.assertTrue(leaf.terminal_receipt_qualified(root.receipt))
        self.assertEqual(leaf.observe_owned_binding(root, ops), 'TERMINAL')

    def test_contradictions_latch_even_if_values_later_match(self):
        variants = (
            lambda r, o: setattr(o, 'owner', (32, r.binding.birth)),
            lambda r, o: setattr(o, 'owner', (31, r.binding.birth+1)),
            lambda r, o: setattr(o, 'pin', (7, 8, 10)),
            lambda r, o: setattr(o, 'image_value', 'other.exe'),
            lambda r, o: setattr(r, 'owner', {}),
            lambda r, o: setattr(r.pi, 'hProcess', 23),
            lambda r, o: r.handles.remove(22),
            lambda r, o: r.receipt.update(bound=False),
            lambda r, o: r.receipt.update(FileID=[7, 8, 10]),
            lambda r, o: r.receipt.update(owner={}),
        )
        for variant in variants:
            with self.subTest(variant=variants.index(variant)):
                root, ops = root_fixture(), Ops()
                variant(root, ops)
                with self.assertRaises(leaf.LifecycleError):
                    leaf.observe_owned_binding(root, ops)
                restored = root_fixture()
                for name in ('owner', 'pi', 'handles', 'receipt'):
                    setattr(root, name, getattr(restored, name))
                ops = Ops()
                ops.state = 0
                with self.assertRaisesRegex(leaf.LifecycleError, 'IDENTITY_PREVIOUSLY_INVALID'):
                    leaf.observe_owned_binding(root, ops)

    def test_foreign_revocation_cannot_be_recovered(self):
        root, ops = root_fixture(), Ops()
        root.authority = False
        ops.state = 0
        with self.assertRaisesRegex(leaf.LifecycleError, 'ROOT_AUTHORITY_REVOKED'):
            leaf.observe_owned_binding(root, ops)

    def test_closed_or_failed_terminal_queries_are_rejected(self):
        for variant in ('closed', 'wait', 'exit', 'times'):
            with self.subTest(variant=variant):
                root, ops = root_fixture(), Ops()
                ops.state = 0
                if variant == 'closed': root.closed = True
                if variant == 'wait': ops.state = 0xffffffff
                if variant == 'exit': ops.exit = None
                if variant == 'times': ops.pid_birth = lambda _: (_ for _ in ()).throw(leaf.LifecycleError('PROCESS_TIMES_FAILED'))
                with self.assertRaises(leaf.LifecycleError):
                    leaf.observe_owned_binding(root, ops)
                self.assertFalse(root.receipt['terminal_binding_verified'])


@contextlib.contextmanager
def generated_win32():
    builder = load('_identity_composition_builder', ROOT/'scripts/hdc_goal/prepare_directdds.py')
    buffers, _, _ = builder.source_buffers()
    names = ('goal_contract', 'goal_handle_lifecycle', 'goal_policy', 'goal_memory_trace', 'goal_win32_owned')
    saved = {name: sys.modules.get(name) for name in names}
    modules = {}
    try:
        for name in names:
            module = types.ModuleType(name)
            module.__file__ = str(builder.ACTIVE/(name+'.py'))
            sys.modules[name] = module
            exec(compile(buffers[name+'.py'], module.__file__, 'exec'), module.__dict__)
            modules[name] = module
        yield modules['goal_win32_owned'], modules['goal_handle_lifecycle']
    finally:
        for name, old in saved.items():
            if old is None: sys.modules.pop(name, None)
            else: sys.modules[name] = old


@unittest.skipUnless(sys.platform == 'win32', 'native Win32 handle contract')
class NativeHandleTests(unittest.TestCase):
    def test_generated_adapter_uses_original_handle_and_pin_after_natural_exit(self):
        with generated_win32() as (w, l):
            child = None
            pin = w.K.CreateFileW(sys.executable, 0x80000000, 1, None, 3, 0x00200000, None)
            self.assertTrue(pin and pin != w.INVALID)
            try:
                child = subprocess.Popen([sys.executable, '-I', '-B', '-c',
                    'import sys,os;sys.stdin.buffer.read(1);os._exit(0)'],
                    stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                ops = w.IdentityOps()
                handle = int(child._handle)
                pid, birth = ops.pid_birth(handle)
                image = ops.image(handle)
                self.assertEqual((pid, ops.wait(handle)), (child.pid, 258))
                binding = l.HandleBinding(handle, int(pin), pid, birth, image, tuple(ops.file_id(pin)))
                owner = {'pid': pid, 'birth': birth, 'image': image}
                root = w.OwnedRoot.__new__(w.OwnedRoot)
                root.pi = types.SimpleNamespace(hProcess=handle)
                root.image_pin = int(pin)
                root.handles = [handle, int(pin)]
                root.owner = dict(owner)
                root.binding = binding
                root.authority = True
                root.closed = False
                root.terminal_at = None
                root.receipt = {'created': True, 'bound': True, 'hProcess': handle,
                    'owner': dict(owner), 'FileID': list(binding.file_id), 'terminal': False, 'EOF': True}
                # Deterministically select the observed pre-signal error window;
                # all other APIs and the subsequent terminal proof remain real.
                class WindowOps(w.IdentityOps):
                    def image(self, h):
                        raise l.ImageUnavailable(5)
                with patch.object(w, 'IdentityOps', WindowOps):
                    with self.assertRaises(l.ImageUnavailable): root.identity()
                self.assertFalse(root.authority)
                with patch.object(w.K, 'TerminateProcess', side_effect=AssertionError('NO_REGRANTED_SIGNAL')):
                    with self.assertRaises(l.LifecycleError): root.stop(time.monotonic()+1)
                with patch.object(w.K, 'ResumeThread', side_effect=AssertionError('NO_REGRANTED_RESUME')):
                    with self.assertRaises(l.LifecycleError): root.resume()
                child.stdin.write(b'x')
                child.stdin.flush()
                self.assertEqual(child.wait(timeout=5), 0)
                self.assertEqual(root.identity(), 'TERMINAL')
                self.assertFalse(root.authority)
                self.assertTrue(l.terminal_receipt_qualified(root.receipt))
                with self.assertRaises(l.LifecycleError): root.identity(require_live=True)
                self.assertTrue(l.terminal_receipt_qualified(root.receipt))
                self.assertEqual(ops.wait_terminal(handle, 1), 0)
                with self.assertRaises(w.Fault): ops.wait_terminal(handle, 51)
            finally:
                if child is not None:
                    if child.poll() is None:
                        child.stdin.close()
                        child.wait(timeout=5)
                    if not child.stdin.closed: child.stdin.close()
                    child._handle.Close()
                self.assertTrue(w.K.CloseHandle(pin))


class BackendWiringTests(unittest.TestCase):
    def runtime(self):
        tests = load('_terminal_composition_tests', ROOT/'tests/test_hdc_directdds_composition.py')
        return tests.runtime()

    def test_journal_identity_label_is_set_before_observation(self):
        with self.runtime() as m:
            backend = object.__new__(m.goal_windows_backend.GoalWindowsBackend)
            backend.samples = backend.epochs = 0
            backend.latest = {'client': {}}
            backend.journal = types.SimpleNamespace(case='previous')
            calls = []
            root = types.SimpleNamespace(case='client', identity=lambda **_: (calls.append(backend.journal.case) or 'TERMINAL'))
            self.assertIsNone(backend.observe(root, 'RUNNING'))
            self.assertEqual(calls, ['client'])

    def test_server_journal_label_is_scoped_and_restored_even_on_failure(self):
        with self.runtime() as m:
            backend = object.__new__(m.goal_windows_backend.GoalWindowsBackend)
            backend.journal = types.SimpleNamespace(case='client')
            labels = []
            server = types.SimpleNamespace(case='server', owner={'pid': 31},
                state=lambda: (labels.append(backend.journal.case) or 'LIVE'),
                identity=lambda **_: labels.append(backend.journal.case))
            backend.roots = [server]
            backend.w = types.SimpleNamespace(tcp_table=lambda: [{'port': 8710, 'pid': 31, 'address': '127.0.0.1'}])
            self.assertTrue(backend.server_ok())
            self.assertEqual(labels, ['server', 'server'])
            self.assertEqual(backend.journal.case, 'client')
            server.identity = lambda **_: (_ for _ in ()).throw(RuntimeError('SYNTHETIC'))
            with self.assertRaises(RuntimeError): backend.server_ok()
            self.assertEqual(backend.journal.case, 'client')

    def test_identity_wait_deadline_uses_existing_bind_step_and_work_caps(self):
        with self.runtime() as m:
            backend = object.__new__(m.goal_windows_backend.GoalWindowsBackend)
            backend.journal = types.SimpleNamespace(case='previous', failed=False)
            backend.server_ok = lambda: True
            backend.grant = types.SimpleNamespace(mark_started=lambda *a: None)
            backend.latest = {}
            backend.baseline = None
            backend.observe = lambda *a: {'system': {'commit_total_bytes': 1}}
            for work_end in (101., 200.):
                root = types.SimpleNamespace(receipt={})
                seen = []
                root.create = lambda: seen.append(root.identity_deadline)
                root.resume = lambda: seen.append(root.identity_deadline)
                step = types.SimpleNamespace(id='client', kind='client')
                with patch.object(m.goal_windows_backend.time, 'monotonic', return_value=100.), \
                     patch.object(m.goal_windows_backend, 'aggregate_reason', return_value=None):
                    backend.create_and_resume(root, step, work_end)
                self.assertEqual(seen, [min(work_end, 102.)]*2)
                self.assertEqual(root.identity_deadline, min(work_end, 115.))

    def test_cleanup_never_stops_a_live_root_with_revoked_signal_capability(self):
        tests = load('_terminal_cleanup_tests', ROOT/'tests/test_hdc_goal_cleanup.py')
        with tests.load_pair() as (_, module):
            backend = object.__new__(module.GoalWindowsBackend)
            clock = tests.Clock()
            root = tests.Root('client', clock)
            root.receipt['signal_authority'] = False
            root.stop = lambda _: (_ for _ in ()).throw(AssertionError('SIGNAL_FORBIDDEN'))
            backend.journal = tests.Journal()
            backend.capture_failed = False
            backend.captured = {'client': 0}
            backend.marker = tests.Marker()
            backend.w = types.SimpleNamespace(put=lambda *a: None)
            backend.observe_chunk = lambda *a: None
            result = backend.cleanup(root, time.monotonic()+1)
            self.assertTrue(root.closed)
            self.assertEqual(root.stop_calls, 0)
            self.assertEqual(root.receipt['stop_skipped'], 'NO_SIGNAL_AUTHORITY')
            self.assertEqual(result['state'], 'UNKNOWN')

    def acceptance_fixture(self, m):
        clock = types.SimpleNamespace(value=10.)
        root = root_fixture()
        root.case = 'client'
        root.dest = Path('SYNTHETIC_NO_DISK_WRITE')
        root.goal_started = 0.
        root.identity_deadline = 11.
        root.output = [bytearray(), bytearray()]
        root.receipt.update(case='client', native_exit=0, cleanup_errors=[], stop_attempts=0,
            terminal=True, signal_authority=False, terminal_binding_verified=True,
            terminal_proof={'method':'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN',
                'hProcess':21,'pid':31,'birth':root.binding.birth,'image':root.binding.image,
                'FileID':[7,8,9],'required_apis_ok':True,'image_pin_retained_at_check':True,
                'full_image_was_verified_while_live':True})
        root.state = lambda: 'TERMINAL'
        root.identity = lambda: 'TERMINAL'
        root.close = lambda: setattr(root, 'closed', True)
        backend = object.__new__(m.goal_windows_backend.GoalWindowsBackend)
        backend.journal = types.SimpleNamespace(case='previous',failed=False)
        backend.marker = types.SimpleNamespace(eof=lambda *a: None,advance=lambda: None)
        backend.notice_gate = lambda: None
        backend.server_ok = lambda: True
        backend.decoded = 0
        published, accepted = [], []
        backend.w = types.SimpleNamespace(put=lambda p,r: published.append(copy.deepcopy(r)))
        def accept(identifier, receipt):
            self.assertTrue(receipt['accepted_evidence'])
            self.assertEqual(receipt['step_acceptance'], 'PENDING')
            accepted.append(identifier)
        backend.grant = types.SimpleNamespace(mark_terminal=lambda *a: None,accept=accept)
        step = types.SimpleNamespace(id='client',kind='client')
        return backend, root, step, clock, published, accepted

    def test_success_keeps_disk_pending_and_commits_existing_acceptance_ledger(self):
        with self.runtime() as m:
            backend,root,step,clock,published,accepted = self.acceptance_fixture(m)
            with patch.object(m.goal_windows_backend.time,'monotonic',side_effect=lambda: clock.value), \
                 patch.object(m.goal_evidence,'evaluate_step',return_value={}):
                result = backend.outcome(root,step)
            self.assertTrue(result['accepted'])
            self.assertEqual(accepted,['client'])
            self.assertFalse(published[0]['accepted_evidence'])
            self.assertTrue(published[0]['content_evidence_qualified'])
            self.assertEqual(published[0]['step_acceptance'],'PENDING')
            self.assertTrue(root.receipt['accepted_evidence'])
            self.assertEqual(root.receipt['step_acceptance'],'ACCEPTED')

    def test_late_identity_publication_or_final_server_never_records_acceptance(self):
        for boundary in ('identity','publication','final_server'):
            with self.subTest(boundary=boundary),self.runtime() as m:
                backend,root,step,clock,published,accepted = self.acceptance_fixture(m)
                if boundary == 'identity':
                    root.identity = lambda: setattr(clock,'value',11.)
                elif boundary == 'publication':
                    backend.w.put = lambda p,r: (published.append(copy.deepcopy(r)),setattr(clock,'value',11.))
                else:
                    calls=[]
                    def server_ok():
                        calls.append(True)
                        if len(calls)==2: clock.value=11.
                        return True
                    backend.server_ok=server_ok
                with patch.object(m.goal_windows_backend.time,'monotonic',side_effect=lambda: clock.value), \
                     patch.object(m.goal_evidence,'evaluate_step',return_value={}):
                    with self.assertRaisesRegex(m.goal_contract.ContractError,'STEP_ACCEPT_DEADLINE'):
                        backend.outcome(root,step)
                self.assertEqual(accepted,[])
                self.assertTrue(root.closed)
                self.assertTrue(root.receipt['terminal_binding_verified'])
                self.assertFalse(root.receipt['accepted_evidence'])
                self.assertEqual(root.receipt['step_acceptance'],'REJECTED')
                self.assertFalse(published[0]['accepted_evidence'])
                self.assertEqual(published[0]['step_acceptance'],'PENDING')

    def test_acceptance_exception_resets_memory_without_overwriting_pending_receipt(self):
        with self.runtime() as m:
            backend,root,step,clock,published,accepted = self.acceptance_fixture(m)
            backend.grant.accept = lambda *a: (_ for _ in ()).throw(RuntimeError('SYNTHETIC_GRANT_FAILURE'))
            with patch.object(m.goal_windows_backend.time,'monotonic',side_effect=lambda: clock.value), \
                 patch.object(m.goal_evidence,'evaluate_step',return_value={}):
                with self.assertRaises(RuntimeError): backend.outcome(root,step)
            self.assertEqual(accepted,[])
            self.assertFalse(root.receipt['accepted_evidence'])
            self.assertEqual(root.receipt['step_acceptance'],'REJECTED')
            self.assertFalse(published[0]['accepted_evidence'])

    def test_dds_server_ready_rechecks_its_original_deadline(self):
        for deadline,now in ((10.5,10.5),(11.,11.0)):
            with self.subTest(deadline=deadline,now=now),self.runtime() as m:
                backend,root,step,clock,published,accepted = self.acceptance_fixture(m)
                step.kind='server'
                root.state=lambda:'LIVE'
                root.identity_deadline=deadline
                backend.grant.mark_server_ready=lambda *a: accepted.append('server')
                backend.server_ok=lambda:(setattr(clock,'value',now) or True)
                with patch.object(m.goal_windows_backend.time,'monotonic',side_effect=lambda: clock.value):
                    with self.assertRaisesRegex(m.goal_contract.ContractError,'SERVER_ACCEPT_DEADLINE'):
                        backend.outcome(root,step)
                self.assertEqual(accepted,[])

    def test_maintained_probe_backend_keeps_its_original_listener_deadline(self):
        tests=load('_terminal_probe_cleanup_tests',ROOT/'tests/test_hdc_goal_cleanup.py')
        with tests.load_pair() as (_,module):
            backend=object.__new__(module.GoalWindowsBackend)
            backend.journal=types.SimpleNamespace(case='previous')
            backend.server_ok=lambda:True
            accepted=[]
            backend.grant=types.SimpleNamespace(mark_server_ready=lambda *a:accepted.append('server'))
            root=types.SimpleNamespace(case='server',goal_started=0.,identity_deadline=11.,
                                       state=lambda:'LIVE',receipt={})
            step=types.SimpleNamespace(id='server',kind='server')
            with patch.object(module.time,'monotonic',return_value=10.):
                with self.assertRaisesRegex(tests.ContractError,'SERVER_ACCEPT_DEADLINE'):
                    backend.outcome(root,step)
            self.assertEqual(accepted,[])

    def test_server_ready_in_deadline_still_accepted(self):
        with self.runtime() as m:
            backend,root,step,clock,published,accepted = self.acceptance_fixture(m)
            step.kind='server'
            root.goal_started=5.
            root.state=lambda:'LIVE'
            backend.grant.mark_server_ready=lambda *a: accepted.append('server')
            with patch.object(m.goal_windows_backend.time,'monotonic',return_value=10.):
                self.assertTrue(backend.outcome(root,step)['accepted'])
            self.assertEqual(accepted,['server'])


if __name__ == '__main__':
    unittest.main()
