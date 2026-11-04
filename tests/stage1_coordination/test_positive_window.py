"""Host controller oracles. No fixture claims native Listen/DDS/Shutdown proof."""
from __future__ import annotations
import dataclasses
import gc
import pathlib
import sys
import threading
import unittest
import weakref

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
from stage1_coordination.positive_window import (
    CoordinationError, DataDone, Endpoint, Envelope, FixtureReport, HookWaitAdapter,
    LifecycleRetainer, ListenerReady, Match, NativeAdapterUnavailable, NotReleased,
    ParticipantReady, PositiveWindowCoordinator, ReleaseStage, Sample, Side,
    Startup, TeardownReport, native_runtime_adapter,
)

TAG = "explicit-host-fixture"
SCOPE = "HOST_CONTROL_FIXTURE_ONLY"
PAYLOADS = {Side.A: (b"A\x00one", b"A\xfftwo", b"Athree"),
            Side.B: (b"B\xffone", b"B\x00two", b"Bthree")}


class Clock:
    def __init__(self): self.value = 100.0
    def __call__(self): return self.value


class Owner:
    """Only a Python-owned fixture bundle, never an actual SDK anchor."""
    pass


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.env = {s: Envelope("0123456789abcdef", s, "fixture-board-" + s.value,
                    1001 if s is Side.A else 1002, "a" * 64, "b" * 64,
                    151, "fixture.ns", 901 if s is Side.A else 902) for s in Side}
        self.c = PositiveWindowCoordinator(
            tuple(Endpoint(self.env[s], "fixture-listener-" + s.value) for s in Side),
            PAYLOADS, deadline=110.0, fixture_id=TAG, clock=self.clock)
        self.workers = []
        self.worker_errors = []

    def tearDown(self):
        self.c.cancel("test-finally")
        for thread in self.workers:
            thread.join(2)
            self.assertFalse(thread.is_alive(), "fixture worker escaped bounded cleanup")
        self.assertEqual(self.worker_errors, [], "fixture worker errors must propagate")

    def r(self, value): return FixtureReport(TAG, value)
    def start(self, side):
        token = self.env[side].self_token
        self.c.startup(self.env[side], self.r(Startup(True, token, token, True, True)))

    def listen(self, side):
        self.c.listener_ready(self.env[side], self.r(ListenerReady(
            0, True, 41 if side is Side.A else 42, "fixture-listener-" + side.value)))

    def connect(self):
        for side in Side: self.start(side)
        for side in Side: self.listen(side)
        for side in Side: self.c.release(self.env[side], ReleaseStage.CONNECT)

    def send_ready(self):
        self.connect()
        for side in Side:
            self.c.participant_ready(self.env[side], self.r(ParticipantReady(True, 1, True, True, True)))
            self.c.matched(self.env[side], self.r(Match(1, 1)))
        for side in Side: self.c.release(self.env[side], ReleaseStage.SEND)

    def sample(self, writer, ordinal):
        return Sample(writer, ordinal, ("1" if writer is Side.A else "2") * 32,
                      (100 if writer is Side.A else 200) + ordinal,
                      PAYLOADS[writer][ordinal - 1])

    def data(self):
        self.send_ready()
        # Cross-side arrival is intentionally interleaved: TAKE precedes peer WRITE.
        for ordinal in (2, 1, 3):
            self.c.take(self.env[Side.B], self.r(self.sample(Side.A, ordinal)))
            self.c.write(self.env[Side.A], self.r(self.sample(Side.A, ordinal)))
            self.c.write(self.env[Side.B], self.r(self.sample(Side.B, ordinal)))
            self.c.take(self.env[Side.A], self.r(self.sample(Side.B, ordinal)))

    def done(self):
        self.data()
        for side in Side: self.c.data_done(self.env[side], self.r(DataDone(True)))
        for side in Side: self.c.release(self.env[side], ReleaseStage.DELETE)

    def spawn(self, function):
        def checked_worker():
            try: function()
            except BaseException as error: self.worker_errors.append(repr(error))
        thread = threading.Thread(target=checked_worker)
        self.workers.append(thread)
        thread.start()
        return thread

    def test_both_end_order_and_cross_correlated_delete(self):
        for stage in ReleaseStage:
            with self.assertRaises(NotReleased): self.c.release(self.env[Side.A], stage)
        self.start(Side.A); self.listen(Side.A)
        with self.assertRaises(NotReleased): self.c.release(self.env[Side.A], ReleaseStage.CONNECT)
        self.start(Side.B); self.listen(Side.B)
        for side in Side:
            self.assertEqual(self.c.release(self.env[side], ReleaseStage.CONNECT).scope, SCOPE)
            self.c.participant_ready(self.env[side], self.r(ParticipantReady(True, 1, True, True, True)))
        self.c.matched(self.env[Side.B], self.r(Match(1, 1)))
        with self.assertRaises(NotReleased): self.c.release(self.env[Side.A], ReleaseStage.SEND)
        self.c.matched(self.env[Side.A], self.r(Match(1, 1)))
        for side in Side: self.c.release(self.env[side], ReleaseStage.SEND)
        for writer in Side:
            reader = Side.B if writer is Side.A else Side.A
            for n in (1, 2, 3):
                self.c.write(self.env[writer], self.r(self.sample(writer, n)))
                self.c.take(self.env[reader], self.r(self.sample(writer, n)))
        self.c.data_done(self.env[Side.B], self.r(DataDone(True)))
        with self.assertRaises(NotReleased): self.c.release(self.env[Side.A], ReleaseStage.DELETE)
        self.c.data_done(self.env[Side.A], self.r(DataDone(True)))
        self.assertEqual(self.c.release(self.env[Side.A], ReleaseStage.DELETE).scope, SCOPE)

    def test_every_envelope_axis_is_bound(self):
        bad = {"run": "fedcba9876543210", "side": Side.B, "board": "wrong-board",
               "pid": 77, "executable_sha256": "c" * 64, "deployment_sha256": "d" * 64,
               "domain": 152, "namespace": "other.ns", "self_token": 999}
        for name, value in bad.items():
            with self.subTest(name=name):
                c = PositiveWindowCoordinator(tuple(Endpoint(self.env[s], "fixture-listener-" + s.value)
                    for s in Side), PAYLOADS, deadline=110, fixture_id=TAG, clock=self.clock)
                try:
                    with self.assertRaises(CoordinationError):
                        c.startup(dataclasses.replace(self.env[Side.A], **{name: value}),
                                  self.r(Startup(True, 901, 901, True, True)))
                    self.assertFalse(c.snapshot().connect_released)
                finally: c.cancel("subcase-finally")

    def test_wrong_types_and_unlabelled_evidence_rejected(self):
        with self.assertRaises(CoordinationError):
            self.c.startup(self.env[Side.A], FixtureReport("untrusted", Startup(True, 901, 901, True, True)))
        with self.assertRaises(ValueError):
            PositiveWindowCoordinator(tuple(Endpoint(dataclasses.replace(self.env[s], pid=True), "x")
                for s in Side), PAYLOADS, deadline=110, fixture_id=TAG, clock=self.clock)

    def test_own_startup_grant_and_unchanged_identity_required(self):
        with self.assertRaises(CoordinationError):
            self.c.startup(self.env[Side.A], self.r(Startup(False, 901, 901, True, True)))
        self.assertFalse(self.c.snapshot().connect_released)

    def test_listener_cannot_precede_startup(self):
        with self.assertRaises(CoordinationError): self.listen(Side.A)

    def test_observed_listen_return_and_publication_required(self):
        self.start(Side.A)
        with self.assertRaises(CoordinationError):
            self.c.listener_ready(self.env[Side.A], self.r(ListenerReady(-1, False, 41, "fixture-listener-A")))

    def test_duplicate_event_is_terminal_first_cause_preserved(self):
        self.start(Side.A)
        with self.assertRaises(CoordinationError): self.start(Side.A)
        first = self.c.snapshot().failure
        self.c.cancel("later")
        self.assertEqual(self.c.snapshot().failure, first)

    def test_participant_match_and_write_order(self):
        self.connect()
        with self.assertRaises(CoordinationError): self.c.matched(self.env[Side.A], self.r(Match(1, 1)))

    def test_effective_transport_facts_cannot_admit_fallback(self):
        self.connect()
        with self.assertRaises(CoordinationError):
            self.c.participant_ready(self.env[Side.A], self.r(ParticipantReady(False, 2, True, True, True)))

    def test_missing_samples_cannot_complete_or_delete(self):
        self.send_ready()
        self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))
        with self.assertRaises(NotReleased): self.c.release(self.env[Side.A], ReleaseStage.DELETE)
        with self.assertRaises(CoordinationError): self.c.data_done(self.env[Side.A], self.r(DataDone(True)))

    def test_duplicate_ordinal_rejected(self):
        self.send_ready(); self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))
        with self.assertRaises(CoordinationError): self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))

    def test_payload_must_match_independent_manifest_bytes(self):
        self.send_ready()
        with self.assertRaises(CoordinationError):
            self.c.write(self.env[Side.A], self.r(dataclasses.replace(self.sample(Side.A, 1), payload=b"wrong")))

    def test_peer_guid_mismatch_rejected_when_write_arrives(self):
        self.send_ready()
        self.c.take(self.env[Side.B], self.r(dataclasses.replace(self.sample(Side.A, 1), writer_guid="f" * 32)))
        with self.assertRaises(CoordinationError): self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))

    def test_peer_sequence_mismatch_rejected(self):
        self.send_ready(); self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))
        with self.assertRaises(CoordinationError):
            self.c.take(self.env[Side.B], self.r(dataclasses.replace(self.sample(Side.A, 1), sequence=999)))

    def test_writer_guid_and_sequence_unique(self):
        self.send_ready(); self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))
        with self.assertRaises(CoordinationError):
            self.c.write(self.env[Side.A], self.r(dataclasses.replace(self.sample(Side.A, 2), sequence=101)))

    def test_writer_guid_cannot_change_between_ordinals(self):
        self.send_ready(); self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))
        with self.assertRaises(CoordinationError):
            self.c.write(self.env[Side.A], self.r(dataclasses.replace(self.sample(Side.A, 2), writer_guid="f" * 32)))

    def test_ordinal_and_sequence_order_agree_despite_log_interleave(self):
        self.send_ready(); self.c.write(self.env[Side.A], self.r(self.sample(Side.A, 1)))
        with self.assertRaises(CoordinationError):
            self.c.write(self.env[Side.A], self.r(dataclasses.replace(self.sample(Side.A, 2), sequence=100)))

    def test_global_deadline_does_not_reset_on_progress(self):
        self.clock.value = 109; self.start(Side.A)
        self.assertEqual(self.c.deadline, 110)
        self.clock.value = 110; self.c.notify_time_advanced()
        with self.assertRaises(CoordinationError): self.start(Side.B)
        self.assertEqual(self.c.snapshot().failure, "TIMEOUT")

    def test_peer_loss_and_eof_are_terminal(self):
        self.c.peer_lost(Side.B, "EOF")
        with self.assertRaises(CoordinationError): self.start(Side.A)
        self.assertEqual(self.c.snapshot().failure, "PEER_LOST")

    def test_blocked_hook_independently_cancelled_without_close(self):
        self.start(Side.A); slot = threading.Lock(); returned = threading.Event(); allow_unwind = threading.Event()
        answer = []
        outer = self
        class CloseSpy:
            close_calls = 0
            def close(self):
                outer.assertEqual(outer.c.snapshot().active_hooks, 0)
                outer.assertTrue(allow_unwind.is_set())
                self.close_calls += 1
        owner = CloseSpy(); hold = LifecycleRetainer(self.c, self.env[Side.A], owner)
        hook = HookWaitAdapter(self.c, self.env[Side.A], lambda: self.r(ListenerReady(0, True, 41, "fixture-listener-A")))
        def worker():
            try:
                with slot:
                    answer.append(hook()); returned.set()
                    if not allow_unwind.wait(1): raise AssertionError("fixture unwind release missing")
            finally: self.c.creation_unwound(self.env[Side.A])
        thread = self.spawn(worker)
        try:
            self.assertTrue(self.c.wait_until_blocked(Side.A, 1))
            self.assertFalse(slot.acquire(blocking=False), "fixture did not hold slot")
            self.c.cancel("external cancellation; no Close callback exists")
            self.assertTrue(returned.wait(1)); self.assertEqual(answer, [False]); self.assertEqual(owner.close_calls, 0)
            with self.assertRaises(NotReleased): hold.teardown_permit(aborted=True)
        finally:
            allow_unwind.set(); self.c.cancel("finally"); thread.join(2)
        self.assertFalse(thread.is_alive())
        permit = hold.teardown_permit(aborted=True)
        self.assertEqual(permit.scope, SCOPE)
        owner.close()  # controlled fixture cleanup only after hook/creation unwind.
        self.assertTrue(hold.complete(permit, TeardownReport(True, True, True, True, True)))
        self.assertEqual(owner.close_calls, 1)

    def test_both_hook_orders_use_same_finite_wait_contract(self):
        self.start(Side.A); self.start(Side.B); results = []
        hooks = {s: HookWaitAdapter(self.c, self.env[s], lambda s=s: self.r(ListenerReady(
            0, True, 41 if s is Side.A else 42, "fixture-listener-" + s.value))) for s in Side}
        first = self.spawn(lambda: results.append((Side.B, hooks[Side.B]())))
        self.assertTrue(self.c.wait_until_blocked(Side.B, 1))
        second = self.spawn(lambda: results.append((Side.A, hooks[Side.A]())))
        first.join(2); second.join(2)
        self.assertEqual(set(results), {(Side.A, True), (Side.B, True)})

    def test_virtual_timeout_wakes_parked_hook(self):
        self.start(Side.A); result = []
        hook = HookWaitAdapter(self.c, self.env[Side.A], lambda: self.r(ListenerReady(0, True, 41, "fixture-listener-A")))
        t = self.spawn(lambda: result.append(hook()))
        self.assertTrue(self.c.wait_until_blocked(Side.A, 1))
        self.clock.value = 110; self.c.notify_time_advanced(); t.join(2)
        self.assertEqual(result, [False]); self.assertEqual(self.c.snapshot().failure, "TIMEOUT")

    def test_source_error_and_weak_owner_expiry_fail_closed(self):
        self.start(Side.A)
        def bad_source(): raise OSError("fixture source EOF")
        self.assertFalse(HookWaitAdapter(self.c, self.env[Side.A], bad_source)())
        other = PositiveWindowCoordinator(tuple(Endpoint(self.env[s], "x") for s in Side),
            PAYLOADS, deadline=110, fixture_id=TAG, clock=self.clock)
        hook = HookWaitAdapter(other, self.env[Side.A], bad_source)
        del other; gc.collect(); self.assertFalse(hook())

    def test_lifetime_retained_until_valid_teardown_not_early_delete(self):
        owner = Owner(); reference = weakref.ref(owner)
        hold = LifecycleRetainer(self.c, self.env[Side.A], owner); del owner; gc.collect()
        self.assertIsNotNone(reference())
        with self.assertRaises(NotReleased): hold.teardown_permit()
        self.done(); permit = hold.teardown_permit()
        self.assertIsNotNone(reference())
        self.assertTrue(hold.complete(permit, TeardownReport(True, True, True, True, True)))
        gc.collect(); self.assertIsNone(reference())
        with self.assertRaises(CoordinationError): hold.complete(permit, TeardownReport(True, True, True, True, True))

    def test_failed_teardown_retains_and_transfers_explicitly(self):
        owner = Owner(); reference = weakref.ref(owner)
        hold = LifecycleRetainer(self.c, self.env[Side.A], owner); del owner
        self.c.cancel("fixture process failure"); self.c.creation_unwound(self.env[Side.A])
        permit = hold.teardown_permit(aborted=True)
        self.assertFalse(hold.complete(permit, TeardownReport(True, False, True, True, False)))
        gc.collect(); self.assertIsNotNone(reference())
        transferred = hold.transfer_failed(); gc.collect(); self.assertIs(reference(), transferred)
        del transferred; gc.collect(); self.assertIsNone(reference())

    def test_foreign_and_forged_teardown_permit_rejected(self):
        hold = LifecycleRetainer(self.c, self.env[Side.A], Owner()); self.done()
        permit = hold.teardown_permit()
        forged = dataclasses.replace(permit)
        with self.assertRaises(CoordinationError): hold.complete(forged, TeardownReport(True, True, True, True, True))

    def test_deadline_cannot_turn_late_teardown_into_normal_success(self):
        hold = LifecycleRetainer(self.c, self.env[Side.A], Owner()); self.done()
        normal = hold.teardown_permit()
        self.clock.value = 110
        with self.assertRaises(NotReleased): hold.complete(normal, TeardownReport(True, True, True, True, True))
        self.assertEqual(self.c.snapshot().failure, "TIMEOUT")
        abort = hold.teardown_permit(aborted=True)
        self.assertTrue(hold.complete(abort, TeardownReport(True, True, True, True, True)))
        self.assertEqual(self.c.snapshot().failure, "TIMEOUT")

    def test_resource_and_native_boundary_are_explicit(self):
        with self.assertRaises(ValueError):
            PositiveWindowCoordinator(tuple(Endpoint(self.env[s], "x") for s in Side),
                {s: (b"x" * 4097,) for s in Side}, deadline=110, fixture_id=TAG, clock=self.clock)
        with self.assertRaises(NativeAdapterUnavailable): native_runtime_adapter()
        self.assertEqual(self.c.snapshot().scope, SCOPE)

    def test_deadline_is_read_only_while_hook_is_parked(self):
        self.start(Side.A); results = []
        hook = HookWaitAdapter(self.c, self.env[Side.A], lambda: self.r(ListenerReady(0, True, 41, "fixture-listener-A")))
        thread = self.spawn(lambda: results.append(hook()))
        try:
            self.assertTrue(self.c.wait_until_blocked(Side.A, 1))
            with self.assertRaises(AttributeError): self.c.deadline = 1000
            self.clock.value = 110; self.c.notify_time_advanced(); thread.join(2)
            self.assertEqual(results, [False]); self.assertEqual(self.c.snapshot().failure, "TIMEOUT")
        finally: self.c.cancel("counterexample-finally"); thread.join(2)

    def test_connect_issuance_is_required_on_each_side(self):
        for side in Side: self.start(side); self.listen(side)
        self.assertFalse(self.c.snapshot().connect_released)
        self.c.release(self.env[Side.A], ReleaseStage.CONNECT)
        self.assertFalse(self.c.snapshot().connect_released)
        self.c.participant_ready(self.env[Side.A], self.r(ParticipantReady(True, 1, True, True, True)))
        with self.assertRaises(CoordinationError):
            self.c.participant_ready(self.env[Side.B], self.r(ParticipantReady(True, 1, True, True, True)))

    def test_send_and_delete_require_issued_side_release(self):
        for stage in (ReleaseStage.SEND, ReleaseStage.DELETE):
            with self.subTest(stage=stage):
                c = PositiveWindowCoordinator(tuple(Endpoint(self.env[s], "fixture-listener-" + s.value)
                    for s in Side), PAYLOADS, deadline=110, fixture_id=TAG, clock=self.clock)
                old = self.c; self.c = c
                try:
                    hold = LifecycleRetainer(c, self.env[Side.B], Owner())
                    self.connect()
                    for side in Side:
                        c.participant_ready(self.env[side], self.r(ParticipantReady(True, 1, True, True, True)))
                        c.matched(self.env[side], self.r(Match(1, 1)))
                    self.assertFalse(c.snapshot().send_released)
                    c.release(self.env[Side.A], ReleaseStage.SEND)
                    self.assertFalse(c.snapshot().send_released)
                    if stage is ReleaseStage.SEND:
                        with self.assertRaises(CoordinationError):
                            c.write(self.env[Side.B], self.r(self.sample(Side.B, 1)))
                    else:
                        c.release(self.env[Side.B], ReleaseStage.SEND)
                        for writer in Side:
                            for n in (1, 2, 3):
                                c.write(self.env[writer], self.r(self.sample(writer, n)))
                                c.take(self.env[Side.B if writer is Side.A else Side.A], self.r(self.sample(writer, n)))
                        for side in Side: c.data_done(self.env[side], self.r(DataDone(True)))
                        self.assertFalse(c.snapshot().delete_released)
                        c.release(self.env[Side.A], ReleaseStage.DELETE)
                        self.assertFalse(c.snapshot().delete_released)
                        with self.assertRaises(NotReleased): hold.teardown_permit()
                        c.release(self.env[Side.B], ReleaseStage.DELETE)
                        self.assertTrue(c.snapshot().delete_released)
                        self.assertTrue(hold.complete(hold.teardown_permit(), TeardownReport(True, True, True, True, True)))
                finally:
                    c.cancel("subcase-finally"); self.c = old

    def test_hook_never_reobserves_after_unwind_cleanup_or_transfer(self):
        self.start(Side.A); self.start(Side.B); calls = []; results = []
        hook = HookWaitAdapter(self.c, self.env[Side.A], lambda: (
            calls.append("source"), self.r(ListenerReady(0, True, 41, "fixture-listener-A")))[1])
        thread = self.spawn(lambda: results.append(hook()))
        self.assertTrue(self.c.wait_until_blocked(Side.A, 1)); self.listen(Side.B); thread.join(2)
        self.assertEqual(results, [True]); self.assertEqual(calls, ["source"])
        self.assertFalse(hook()); self.assertEqual(calls, ["source"])
        # Direct fixture events do not run a native hook. Lifecycle authority must
        # still forbid even a first producer during an externally held teardown.
        for failed in (False, True):
            with self.subTest(failed=failed):
                c = PositiveWindowCoordinator(tuple(Endpoint(self.env[s], "fixture-listener-" + s.value)
                    for s in Side), PAYLOADS, deadline=110, fixture_id=TAG, clock=self.clock)
                old = self.c; self.c = c
                try:
                    hold = LifecycleRetainer(c, self.env[Side.A], Owner()); observations = []
                    late = HookWaitAdapter(c, self.env[Side.A], lambda: (
                        observations.append("unsafe"), self.r(ListenerReady(0, True, 41, "fixture-listener-A")))[1])
                    if failed: c.cancel("fixture failure"); c.creation_unwound(self.env[Side.A])
                    else: self.done()
                    permit = hold.teardown_permit(aborted=failed)
                    cleanup_held = threading.Event(); try_hook = threading.Event(); answers = []
                    def at_cleanup_barrier():
                        cleanup_held.set()
                        if not try_hook.wait(1): raise AssertionError("missing controlled cleanup release")
                        answers.append(late())
                    worker = self.spawn(at_cleanup_barrier)
                    try:
                        self.assertTrue(cleanup_held.wait(1)); try_hook.set(); worker.join(2)
                        self.assertEqual(answers, [False]); self.assertEqual(observations, [])
                        self.assertEqual(hold.complete(permit, TeardownReport(True, not failed, True, True, not failed)), not failed)
                        if failed: transferred = hold.transfer_failed(); self.assertIsInstance(transferred, Owner)
                        self.assertFalse(late()); self.assertEqual(observations, [])
                    finally: try_hook.set(); worker.join(2)
                finally: c.cancel("subcase-finally"); self.c = old


if __name__ == "__main__": unittest.main(verbosity=2)
