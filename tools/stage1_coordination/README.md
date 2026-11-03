# Positive-window Host control component

`positive_window.py` is reusable standard-library control logic, with a targeted
project entry at `tests/stage1_coordination`. It does not import FastDDS, use HDC,
launch a process, choose an identity, open transport input or call Runtime.Close.

All reports require an explicit `FixtureReport` matching the session fixture ID;
all snapshots/control permits say `HOST_CONTROL_FIXTURE_ONLY`. No fixture READY
is native Listen proof. `native_runtime_adapter()` always raises: real C++ SDK
evidence, bounded cross-process/framing/IO, startup grant and native lifecycle
wiring are **NOT_IMPLEMENTED**. There is no constant-ready native fallback.

The immutable envelope binds run/side/serial/PID/executable+deployment hashes,
domain/namespace and expected own token. Payload expectations are independent
immutable bytes. CONNECT requires both qualified startup/listener reports;
SEND requires both Participant/QoS and exact peer matches; DELETE requires both
DATA_DONE plus exact bidirectional GUID/sequence/payload multiplicities.
Cross-side log arrival can interleave, but duplicates/mismatch/order violations
abort with the first reason. One absolute finite deadline is read-only through
the public API and never resets. Readiness does not issue a release: each side
must obtain CONNECT before Participant, SEND before sample/DATA_DONE, and DELETE
before normal teardown permission. Snapshot released flags require both side
permits to have been issued, as well as continued valid readiness.

`HookWaitAdapter` captures controller weakly, takes a required finite nonblocking
snapshot producer and parks on a condition variable. Explicit cancel, peer loss
and clock expiry wake it without a transport Close or slot acquisition. The real
hook retains slot/control/opening pins: a future native producer must not reenter
that lifecycle, wait for application input/DDS match, or rely on Close to cancel.
No native observer is implemented by this Host adapter. Hook admission is
one-shot under the same condition lock as cleanup permission. Sequential entry,
creation unwind or lifecycle permission prevents a new producer invocation,
including while external cleanup is held and after failed-owner transfer.

`LifecycleRetainer` holds an opaque owner bundle in the controller. Caller keeps
the controller and creation resources alive, acknowledges creation unwind, and
obtains a normal DELETE permit (or aborted cleanup permit after hook exit) before
performing external teardown. A matching one-use permit and complete successful
report release the bundle. Failed teardown retains it; `transfer_failed()` moves
continued ownership to the caller without claiming safe SDK cleanup. No global
hidden hold, sibling, native anchor, SDK lease/generation or callback quiet is
created. This controls the adapter contract; raw DDS calls outside it and SDK
callback/parameter last-use are unverified. Normal references/destructors still
require callers to honor that lifetime contract.

Host limits: two endpoints,1..16 samples/writer,4096 bytes/payload,256 event
reports, bounded envelope strings, coordinator deadline<=300 seconds from start.
Injected clocks/producers must themselves be finite and nonblocking. The default
clock is monotonic; test clock advancement only notifies control state and never
manufactures transport work/generation. No blocking IO/logging occurs here.

Use fresh **E** output and process-only TEMP/TMP/PYTHONDONTWRITEBYTECODE:

```text
cmake -S tests/stage1_coordination -B <owned-E-build> -G Ninja -DPython3_EXECUTABLE=<verified-python>
cmake --build <owned-E-build> --target coordinator-syntax --parallel 1
cmake --build <owned-E-build> --target check-positive-window-coordinator --parallel 1
```

The standalone project has LANGUAGES NONE: no native compiler is selected or
bypassed, no SDK/product/archive is rebuilt. CTest retains25s; deterministic
thread/condition/manual-clock tests do not simulate SDK timing. Initial/review/
NEWfinal evidence must be read from the current increment, not inferred here.
Real Socket/DDS/dual-board and Shutdown quiescence remain NOT_VERIFIED;
Stage2all37HELD/globalPRE_COMMITFAIL.
