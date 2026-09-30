# Installed Runtime dual-board acceptance

This record describes the bounded installed-consumer result currently supported by
the saved evidence. It is a runtime acceptance record, not a production qualification
or a general DSoftBus guarantee.

## Accepted window

The independently reviewed window is:

| Item | Value |
| --- | --- |
| Query run | `52093e521faf0dcb` |
| Data run | `73ff0560343b4afb` |
| Consumer | `mdds_installed_consumer` |
| Mode | `installed_runtime` |
| Configuration | builtin transports off, intraprocess off, DataSharing off, RELIABLE/VOLATILE |
| Samples | 5 writes and 5 takes on each side |
| Result | PASS for this bounded window |

The two directional ledgers contain ordinals 1 through 5, the peer current Writer
GUID, DDS sequence numbers 1 through 5, the run field and payload checks. A side's
five deliveries were evaluated independently; one side cannot compensate for a
missing delivery on the other side.

Startup evidence records `LISTENER_READY` on both sides before connection release;
matching and data activity follow that release. The observed native counters include
two socket calls, one listen, one bind and an `accepted=3` numeric socket handle per
side; this is not a count of three accepted connections.
Shutdown counts were A=3 and B=1 and are retained as observed responsibility; they
are not treated as a symmetry requirement or as proof of physical SDK shutdown.

The normal raw sequence was public DDS deletion with zero entity and participant
return codes, followed by `CLOSED`, token restoration, the `DDS_DELETED` receipt,
`INST_EXIT=0` and `REMOTE_EXIT=0`. The public DDS deletion API occurs before Runtime
close; `DDS_DELETED` is the subsequent recorded stage marker. Both business and load
collectors closed without overflow or late bytes. The recorded runtime state is local
completion; SDK drain remains unproven.

## Verification scope

The final host acceptance preparation executed the standard CONFIG suite as
`527/527 PASS` and the affected final set as `7/7 PASS`. The installed-runtime
Experimental Runtime ASan gate was `6/6 PASS` using its own rebuilt local inputs;
imported Fast DDS, Fast CDR and foonathan_memory dependencies were not instrumented.
The existing Host cycle-100 result was reused because the expanded configuration
did not change that target's source or compile definitions; it was not counted as a
new cycle-100 execution.

The Runtime assembly change keeps the gate capture per Runtime instance, links the
captured lifetime to the common closure, rejects invalid or repeated SDK-free entry
use, and retains the native token through the Runtime and descriptor lifetime before
restoration. These claims are supported by the affected Host and ASan evidence;
they do not expand the device result into an SDK cancellation or drain guarantee.

The installed executor and local Fake-driver suites remain separate local evidence.
They verify command and recovery contracts but are not used as device runtime or
permission evidence.

## Installation and provenance

The external consumer was built from the installed prefix with public MDDS/Fast DDS
headers and the exported package targets. The accepted package has six declared
members; the fresh API23 build installed fourteen MDDS archives and the consumer
link map identifies thirteen linked archives. The target executable is AArch64 with
`$ORIGIN` runpath and the declared token bridge, DSoftBus client, Fast DDS, Fast CDR,
foonathan and libc++ shared objects.

The saved source/package acceptance checked 64 source entries with zero mismatches.
The accepted main package identity is recorded in the immutable acceptance receipt;
the local evidence index points to that receipt rather than copying private device
configuration into project documentation.

The public Socket declarations come from the authorized `communication_dsoftbus`
source headers. They are separate from the OpenHarmony SDK and from the target
platform DSO. The installed example does not package system DSOs, permission JSON,
tokens, board identifiers, routes or claims.

## Lifecycle boundary

The supported experimental entry point is one `experimental::Runtime`, one
descriptor and one Fast DDS Participant per process. The caller owns the application
threads and performs normal DDS deletion before stopping and joining its pump, then
calls `Runtime::Close` and releases the descriptor/runtime. The default constructor
path remains `NativeBackendUnavailable`; selecting the experimental target is an
explicit build and configuration choice.

The accepted result does not establish SDK epoch continuity, internal drain,
bounded SDK cancellation, complete physical module closure, arbitrary topologies,
PF-01/PF-02, P0/PF/P4 or production readiness. No `QualificationProfile` was
generated.

## Historical and budget boundaries

The earlier `data-1` failure (`a0ebc6aec0da2bac`) remains preserved with its original
failure and restoration records. The historical local `query-r6-1` remains
`FAIL / RECOVERY_BLOCKED`; it was not repaired by this acceptance.

The saved ledger contains two query attempts and two data attempts, all consumed.
No additional query, data, repair, performance, fault-injection or 100-cycle window
is represented by this record.

## Evidence index

- [Acceptance tester result](../.artifacts/installed-runtime-device/run-20260929-local/device-resume/device-acceptance/TEST_RESULT.json)
- [Independent acceptance report](../.artifacts/installed-runtime-device/run-20260929-local/device-resume/device-acceptance-review/REPORT.md)
- [Independent review JSON](../.artifacts/installed-runtime-device/run-20260929-local/device-resume/device-acceptance-review/REVIEW.json)
- [Host and ASan scope](../.artifacts/installed-runtime-device/run-20260929-local/device-resume/host-tester/TEST_RESULT.json)
- [Executor and package wiring scope](../.artifacts/installed-runtime-device/run-20260929-local/device-resume/executor-tester/TEST_RESULT.json)

These links point to local evidence and are not remote artifact distribution links.
