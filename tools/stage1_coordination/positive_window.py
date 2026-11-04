"""Bounded two-endpoint control decisions, not native readiness/permission proof.

All evidence is explicitly labelled Host fixture evidence. No Runtime, SDK,
process launcher, transport generation, network socket or native anchor exists
here. Native producers and cross-process bounded IO require separate work.
"""
from __future__ import annotations

import dataclasses
import enum
import math
import re
import threading
import time
import weakref
from typing import Callable

HOST_SCOPE = "HOST_CONTROL_FIXTURE_ONLY"


class Side(str, enum.Enum):
    A = "A"
    B = "B"


class ReleaseStage(str, enum.Enum):
    CONNECT = "CONNECT_RELEASE"
    SEND = "SEND_RELEASE"
    DELETE = "DELETE_RELEASE"


class CoordinationError(RuntimeError):
    """Terminal protocol/adapter failure. First session cause is retained."""


class NotReleased(RuntimeError):
    """A control/lifecycle action is premature; this does not advance state."""


class NativeAdapterUnavailable(NotImplementedError):
    pass


def native_runtime_adapter():
    """Fail closed: no constant READY, speculative SDK producer or native IO."""
    raise NativeAdapterUnavailable("Native C++/SDK/IO and authorization adapters are NOT_IMPLEMENTED")


@dataclasses.dataclass(frozen=True)
class Envelope:
    run: str
    side: Side
    board: str
    pid: int
    executable_sha256: str
    deployment_sha256: str
    domain: int
    namespace: str
    self_token: int


@dataclasses.dataclass(frozen=True)
class Endpoint:
    envelope: Envelope
    listener_name: str


@dataclasses.dataclass(frozen=True)
class FixtureReport:
    fixture_id: str
    value: object


@dataclasses.dataclass(frozen=True)
class Startup:
    granted: bool
    before: int
    after: int
    modules_ok: bool
    api_owners_ok: bool


@dataclasses.dataclass(frozen=True)
class ListenerReady:
    listen_return: int
    published: bool
    socket_id: int
    listener_name: str


@dataclasses.dataclass(frozen=True)
class ParticipantReady:
    builtins_disabled: bool
    user_transports: int
    unicast_only: bool
    data_sharing_off: bool
    intraprocess_off: bool


@dataclasses.dataclass(frozen=True)
class Match:
    writers: int
    readers: int


@dataclasses.dataclass(frozen=True)
class Sample:
    writer: Side
    ordinal: int
    writer_guid: str
    sequence: int
    payload: bytes


@dataclasses.dataclass(frozen=True)
class DataDone:
    dds_wait_condition_ok: bool


@dataclasses.dataclass(frozen=True)
class TeardownReport:
    entities_deleted: bool
    participant_deleted: bool
    pump_joined: bool
    descriptor_released: bool
    runtime_closed: bool


@dataclasses.dataclass(frozen=True)
class ControlPermit:
    """Identity-bound local decision; NOT an SDK lease/generation/authority."""
    envelope: Envelope
    stage: str
    scope: str = HOST_SCOPE


@dataclasses.dataclass(frozen=True)
class Snapshot:
    connect_released: bool
    send_released: bool
    delete_released: bool
    failure: str | None
    active_hooks: int
    events: int
    scope: str = HOST_SCOPE


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _text(value, limit):
    if type(value) is not str: return False
    try: return 0 < len(value.encode("utf-8")) <= limit and not any(c in value for c in "\x00\r\n")
    except UnicodeError: return False


def _envelope_valid(e):
    return (type(e) is Envelope and type(e.side) is Side and
            type(e.run) is str and re.fullmatch(r"[0-9a-f]{16}", e.run) is not None and
            _text(e.board, 128) and _integer(e.pid, 1, 0x7fffffff) and
            all(type(x) is str and re.fullmatch(r"[0-9a-f]{64}", x) is not None
                for x in (e.executable_sha256, e.deployment_sha256)) and
            _integer(e.domain, 0, 0xffffffff) and _text(e.namespace, 116) and
            _integer(e.self_token, 1, 0xffffffffffffffff))


class PositiveWindowCoordinator:
    """One-shot finite Host control session with a single absolute deadline.

    The caller owns real lifecycle objects and must retain this controller (or
    its cancellation handle) while using hooks/retainers. Reports are claims by
    an explicitly labelled fixture, not authenticated native observations.
    """
    def __init__(self, endpoints, payloads, *, deadline, fixture_id, clock=time.monotonic):
        if type(endpoints) not in (tuple, list) or len(endpoints) != 2:
            raise ValueError("bounded two-endpoint collection required")
        endpoints = tuple(endpoints)
        if (len(endpoints) != 2 or any(type(x) is not Endpoint or not _envelope_valid(x.envelope)
                or not _text(x.listener_name, 255) for x in endpoints) or
                {x.envelope.side for x in endpoints} != set(Side)):
            raise ValueError("exactly two valid endpoint envelopes required")
        a, b = (next(x.envelope for x in endpoints if x.envelope.side is s) for s in Side)
        if ((a.run, a.domain, a.namespace) != (b.run, b.domain, b.namespace) or
                (a.board, a.pid) == (b.board, b.pid)):
            raise ValueError("positive-window identity/domain/namespace configuration mismatch")
        if not _text(fixture_id, 128) or not callable(clock):
            raise ValueError("explicit Host fixture source and finite clock required")
        now = clock()
        if (type(now) not in (int, float) or not math.isfinite(now) or
                type(deadline) not in (int, float) or not math.isfinite(deadline) or
                not 0 < deadline - now <= 300):
            raise ValueError("finite absolute deadline within300 seconds required")
        expected = {}
        if type(payloads) is not dict or set(payloads) != set(Side):
            raise ValueError("independent payload expectations for both writers required")
        for side in Side:
            if type(payloads[side]) not in (tuple, list) or not 1 <= len(payloads[side]) <= 16:
                raise ValueError("bounded payload collection required")
            values = tuple(payloads[side])
            if not 1 <= len(values) <= 16 or any(type(x) is not bytes or not 0 < len(x) <= 4096 for x in values):
                raise ValueError("bounded immutable expected payloads required")
            expected[side] = values
        if len(expected[Side.A]) != len(expected[Side.B]):
            raise ValueError("equal finite sample counts required")
        self._deadline = float(deadline)
        self._last_time = float(now)
        self._clock = clock
        self._fixture = fixture_id
        self._expected = {x.envelope.side: x for x in endpoints}
        self._payloads = expected
        self._count = len(expected[Side.A])
        self._cv = threading.Condition(threading.RLock())
        self._failure = None
        self._events = 0
        self._state = {s: {"startup": False, "listener": False, "participant": False,
            "match": False, "done": False, "writes": {}, "takes": {}, "unwound": False,
            "hooks": 0, "waiting": 0, "resource": None, "resource_key": None,
            "hook_entered": False, "lifecycle_started": False,
            "teardown_attempted": False, "teardown_failed": False} for s in Side}
        self._releases = {}
        self._teardown = {}
        self._consumed = set()

    @property
    def deadline(self):
        """Validated initial absolute bound; public extension is prohibited."""
        return self._deadline

    def _abort(self, cause):
        if self._failure is None:
            self._failure = cause
        self._cv.notify_all()

    def _fault(self, cause):
        self._abort(cause)
        raise CoordinationError(self._failure)

    def _expire(self):
        now = self._clock()
        if type(now) not in (int, float) or not math.isfinite(now) or now < self._last_time:
            self._abort("CLOCK_ERROR")
        else:
            self._last_time = float(now)
            if now >= self.deadline:
                self._abort("TIMEOUT")

    def _identity(self, envelope):
        if not _envelope_valid(envelope) or envelope != self._expected[envelope.side].envelope:
            self._fault("ENVELOPE_MISMATCH")
        return self._state[envelope.side]

    def _begin(self, envelope):
        state = self._identity(envelope)
        self._expire()
        if self._failure is not None:
            raise CoordinationError(self._failure)
        if self._events >= 256:
            self._fault("EVENT_CAPACITY")
        self._events += 1
        return state

    def _report(self, report, kind):
        if (type(report) is not FixtureReport or type(report.fixture_id) is not str or
                report.fixture_id != self._fixture or type(report.value) is not kind):
            self._fault("EVIDENCE_SOURCE")
        return report.value

    def _once(self, state, key):
        if state[key]: self._fault("DUPLICATE_EVENT")

    def _ready(self, stage):
        if self._failure is not None: return False
        if stage is ReleaseStage.CONNECT:
            return all(x["startup"] and x["listener"] for x in self._state.values())
        if stage is ReleaseStage.SEND:
            return self._ready(ReleaseStage.CONNECT) and all(x["participant"] and x["match"] for x in self._state.values())
        if stage is ReleaseStage.DELETE:
            return self._ready(ReleaseStage.SEND) and all(x["done"] for x in self._state.values()) and self._all_samples_match()
        self._fault("UNKNOWN_RELEASE")

    def _all_samples_match(self):
        for writer in Side:
            reader = Side.B if writer is Side.A else Side.A
            writes = self._state[writer]["writes"]
            takes = self._state[reader]["takes"]
            if len(writes) != self._count or len(takes) != self._count: return False
            if any(writes[n] != takes[n] for n in range(1, self._count + 1)): return False
        return True

    def startup(self, envelope, report):
        with self._cv:
            state = self._begin(envelope); value = self._report(report, Startup); self._once(state, "startup")
            if (any(type(x) is not bool for x in (value.granted, value.modules_ok, value.api_owners_ok)) or
                    not (value.granted and value.modules_ok and value.api_owners_ok) or
                    not _integer(value.before, 1, 0xffffffffffffffff) or
                    not _integer(value.after, 1, 0xffffffffffffffff) or
                    value.before != value.after or value.before != envelope.self_token):
                self._fault("STARTUP_NOT_QUALIFIED")
            state["startup"] = True; self._cv.notify_all()

    def listener_ready(self, envelope, report):
        with self._cv:
            state = self._begin(envelope); value = self._report(report, ListenerReady)
            if not state["startup"]: self._fault("EVENT_ORDER")
            self._once(state, "listener")
            if (not _integer(value.listen_return, -0x80000000, 0x7fffffff) or value.listen_return != 0 or
                    type(value.published) is not bool or not value.published or
                    not _integer(value.socket_id, 0, 0x7fffffff) or
                    not _text(value.listener_name, 255) or
                    value.listener_name != self._expected[envelope.side].listener_name):
                self._fault("LISTENER_NOT_QUALIFIED")
            state["listener"] = True; self._cv.notify_all()

    def participant_ready(self, envelope, report):
        with self._cv:
            state = self._begin(envelope); value = self._report(report, ParticipantReady)
            if (not self._ready(ReleaseStage.CONNECT) or
                    (envelope.side, ReleaseStage.CONNECT) not in self._releases): self._fault("EVENT_ORDER")
            self._once(state, "participant")
            facts = (value.builtins_disabled, value.unicast_only, value.data_sharing_off, value.intraprocess_off)
            if any(type(x) is not bool or not x for x in facts) or not _integer(value.user_transports, 1, 1):
                self._fault("TRANSPORT_FACTS")
            state["participant"] = True; state["unwound"] = True; self._cv.notify_all()

    def matched(self, envelope, report):
        with self._cv:
            state = self._begin(envelope); value = self._report(report, Match)
            if not state["participant"]: self._fault("EVENT_ORDER")
            self._once(state, "match")
            if not _integer(value.writers, 1, 1) or not _integer(value.readers, 1, 1): self._fault("PEER_MATCH")
            state["match"] = True; self._cv.notify_all()

    def _sample(self, envelope, report, take):
        with self._cv:
            state = self._begin(envelope); value = self._report(report, Sample)
            if (not self._ready(ReleaseStage.SEND) or state["done"] or
                    (envelope.side, ReleaseStage.SEND) not in self._releases): self._fault("EVENT_ORDER")
            expected_writer = (Side.B if envelope.side is Side.A else Side.A) if take else envelope.side
            if (type(value.writer) is not Side or value.writer is not expected_writer or
                    not _integer(value.ordinal, 1, self._count) or
                    type(value.writer_guid) is not str or re.fullmatch(r"[0-9a-f]{32}", value.writer_guid) is None or
                    not _integer(value.sequence, 1, 0x7fffffffffffffff) or type(value.payload) is not bytes or
                    value.payload != self._payloads[expected_writer][value.ordinal - 1]):
                self._fault("SAMPLE_INVALID")
            ledger = state["takes" if take else "writes"]
            if value.ordinal in ledger: self._fault("DUPLICATE_SAMPLE")
            if any(x.writer_guid != value.writer_guid or x.sequence == value.sequence for x in ledger.values()):
                self._fault("WRITER_OR_SEQUENCE")
            if any((x.ordinal < value.ordinal and x.sequence >= value.sequence) or
                   (x.ordinal > value.ordinal and x.sequence <= value.sequence) for x in ledger.values()):
                self._fault("ORDINAL_SEQUENCE_ORDER")
            peer = self._state[value.writer]["writes"] if take else self._state[Side.B if envelope.side is Side.A else Side.A]["takes"]
            if value.ordinal in peer and peer[value.ordinal] != value: self._fault("CROSS_SAMPLE_MISMATCH")
            ledger[value.ordinal] = value; self._cv.notify_all()

    def write(self, envelope, report): self._sample(envelope, report, False)
    def take(self, envelope, report): self._sample(envelope, report, True)

    def data_done(self, envelope, report):
        with self._cv:
            state = self._begin(envelope); value = self._report(report, DataDone)
            if (not self._ready(ReleaseStage.SEND) or
                    (envelope.side, ReleaseStage.SEND) not in self._releases): self._fault("EVENT_ORDER")
            self._once(state, "done")
            if len(state["writes"]) != self._count or len(state["takes"]) != self._count:
                self._fault("MISSING_SAMPLES")
            if type(value.dds_wait_condition_ok) is not bool or not value.dds_wait_condition_ok:
                self._fault("DDS_WAIT_CONDITION")
            state["done"] = True; self._cv.notify_all()

    def release(self, envelope, stage):
        with self._cv:
            self._identity(envelope); self._expire()
            if self._failure is not None: raise CoordinationError(self._failure)
            if type(stage) is not ReleaseStage: self._fault("UNKNOWN_RELEASE")
            if not self._ready(stage): raise NotReleased(stage.value)
            key = (envelope.side, stage)
            if key not in self._releases: self._releases[key] = ControlPermit(envelope, stage.value)
            return self._releases[key]

    def _wait_connect(self, envelope):
        with self._cv:
            state = self._identity(envelope)
            while True:
                self._expire()
                if self._failure is not None: raise CoordinationError(self._failure)
                if self._ready(ReleaseStage.CONNECT): return self.release(envelope, ReleaseStage.CONNECT)
                state["waiting"] += 1; self._cv.notify_all()
                try: self._cv.wait(timeout=max(0, self.deadline - self._last_time))
                finally: state["waiting"] -= 1; self._cv.notify_all()

    def cancel(self, reason="explicit"):
        if not _text(reason, 128): raise ValueError("bounded cancellation reason required")
        with self._cv: self._abort("CANCELLED")

    def peer_lost(self, side, reason="EOF"):
        if type(side) is not Side or not _text(reason, 128): raise ValueError("bounded peer-loss event required")
        with self._cv: self._abort("PEER_LOST")

    def notify_time_advanced(self):
        """Injected clock notification only; never transport generation/work."""
        with self._cv: self._expire(); self._cv.notify_all()

    def wait_until_blocked(self, side, timeout):
        """Bounded Host observation of actual parked control wait, no state credit."""
        if type(side) is not Side or not 0 < timeout <= 2: raise ValueError("bounded observation required")
        with self._cv: return self._cv.wait_for(lambda: self._state[side]["waiting"] > 0, timeout)

    def creation_unwound(self, envelope):
        """Caller acknowledgment; not a proof of native callbacks/SDK quiescence."""
        with self._cv:
            state = self._identity(envelope)
            if state["hooks"]: raise NotReleased("hook still entered")
            state["unwound"] = True; self._cv.notify_all()

    def snapshot(self):
        with self._cv:
            self._expire()
            issued = lambda stage: self._ready(stage) and all((side, stage) in self._releases for side in Side)
            return Snapshot(issued(ReleaseStage.CONNECT), issued(ReleaseStage.SEND),
                issued(ReleaseStage.DELETE), self._failure,
                sum(x["hooks"] for x in self._state.values()), self._events)

    def _attach_resource(self, envelope, resource):
        with self._cv:
            state = self._identity(envelope)
            if resource is None or state["resource_key"] is not None: self._fault("RESOURCE_REGISTRATION")
            key = object(); state["resource_key"] = key; state["resource"] = resource
            return key

    def _resource_state(self, envelope, key):
        state = self._identity(envelope)
        if state["resource_key"] is not key: self._fault("RESOURCE_IDENTITY")
        return state

    def _teardown_permit(self, envelope, key, aborted):
        with self._cv:
            state = self._resource_state(envelope, key); self._expire()
            if type(aborted) is not bool: self._fault("TEARDOWN_MODE")
            if any(x["hooks"] for x in self._state.values()) or not state["unwound"]:
                raise NotReleased("hook/creation must unwind before lifecycle")
            if state["teardown_attempted"]: raise CoordinationError("TEARDOWN_ALREADY_ATTEMPTED")
            if aborted:
                if self._failure is None: raise NotReleased("not aborted")
            elif (self._failure is not None or not self._ready(ReleaseStage.DELETE) or
                    (envelope.side, ReleaseStage.DELETE) not in self._releases):
                raise NotReleased("DELETE_RELEASE required")
            permit_key = (envelope.side, aborted)
            if permit_key not in self._teardown:
                self._teardown[permit_key] = ControlPermit(envelope, "ABORT_TEARDOWN" if aborted else "NORMAL_TEARDOWN")
            state["lifecycle_started"] = True
            return self._teardown[permit_key]

    def _complete_teardown(self, envelope, key, permit, report):
        with self._cv:
            state = self._resource_state(envelope, key); self._expire()
            permits = [p for (side, _), p in self._teardown.items() if side is envelope.side]
            if (not any(permit is p for p in permits) or id(permit) in self._consumed or
                    state["teardown_attempted"]): self._fault("TEARDOWN_PERMIT")
            if any(x["hooks"] for x in self._state.values()) or not state["unwound"]:
                raise NotReleased("hook/creation still active")
            if permit.stage == "NORMAL_TEARDOWN" and self._failure is not None:
                raise NotReleased("normal cleanup invalid after failure")
            if type(report) is not TeardownReport or any(type(getattr(report, f.name)) is not bool for f in dataclasses.fields(report)):
                self._fault("TEARDOWN_REPORT")
            self._consumed.add(id(permit)); state["teardown_attempted"] = True
            if not all(getattr(report, f.name) for f in dataclasses.fields(report)):
                state["teardown_failed"] = True; self._abort("TEARDOWN_FAILED"); return False
            state["resource"] = None
            self._cv.notify_all()
            return True

    def _transfer_failed(self, envelope, key):
        with self._cv:
            state = self._resource_state(envelope, key)
            if (not state["teardown_failed"] or state["resource"] is None or
                    any(x["hooks"] for x in self._state.values()) or not state["unwound"]):
                raise NotReleased("failed retained cleanup must be acknowledged")
            owner = state["resource"]; state["resource"] = None
            return owner  # caller explicitly retains it; no SDK cleanup/exit is asserted.


class HookWaitAdapter:
    """In-process Host hook contract; no Runtime/lifecycle/IO reference.

    listener_source must be a finite nonblocking snapshot producer. Actual SDK
    producers are unavailable; a Host FixtureReport is mandatory in this subset.
    """
    def __init__(self, coordinator, envelope, listener_source: Callable[[], FixtureReport]):
        if not callable(listener_source): raise ValueError("explicit bounded evidence producer required")
        self._coordinator = weakref.ref(coordinator)
        self._envelope = envelope
        self._source = listener_source

    def __call__(self):
        coordinator = self._coordinator()
        if coordinator is None: return False
        entered = False
        try:
            with coordinator._cv:
                state = coordinator._identity(self._envelope); coordinator._expire()
                if coordinator._failure is not None: return False
                # Same CV as teardown admission: never observe a source after
                # one-shot entry, creation unwind or external cleanup authority.
                if (state["hook_entered"] or state["listener"] or state["unwound"] or
                        state["lifecycle_started"] or state["teardown_attempted"]): return False
                state["hook_entered"] = True
                state["hooks"] += 1; entered = True
            observation = self._source()
            coordinator.listener_ready(self._envelope, observation)
            coordinator._wait_connect(self._envelope)
            return True
        except CoordinationError:
            return False
        except Exception:
            with coordinator._cv: coordinator._abort("EVIDENCE_SOURCE_ERROR")
            return False
        finally:
            if entered:
                with coordinator._cv:
                    coordinator._state[self._envelope.side]["hooks"] -= 1
                    coordinator._cv.notify_all()


class LifecycleRetainer:
    """Explicit opaque Host owner bundle, not a native persistent anchor.

    Caller must keep the coordinator/owner contract until cleanup or explicit
    failed-owner transfer. Raw DDS destruction outside this adapter is outside
    its guarantees. No method calls Runtime.Close or any SDK operation.
    """
    def __init__(self, coordinator, envelope, owner_bundle):
        self._coordinator = coordinator
        self._envelope = envelope
        self._key = coordinator._attach_resource(envelope, owner_bundle)

    def teardown_permit(self, *, aborted=False):
        return self._coordinator._teardown_permit(self._envelope, self._key, aborted)

    def complete(self, permit, report):
        return self._coordinator._complete_teardown(self._envelope, self._key, permit, report)

    def transfer_failed(self):
        return self._coordinator._transfer_failed(self._envelope, self._key)
