# D04 prepared read-only evidence successor

This is offline diagnostic preparation. No HDC call is authorized by this document, its generation or its tests. A new direct single-run approval is required. D04 DDS remains unverified and its original directories/evidence are retained.

## Evidence and change

The consumed E31B704C read-only run at 2026-11-03 04:05:32 UTC accepted its own listening server, then B reported the exact channel-establishing/device-selection failure enums. B's typed BEGIN was not accepted, A was not run and no remote evidence was qualified. Both original local roots had terminal/EOF/close receipts. Remote Native state, identity restoration, descendants and SDK quiet remain unknown.

The D04 execution previously reached both boards, deployment and Native probe after a monitored ten-second observation followed by an accepted target listing. This difference supports reusing that existing sequence; it does not prove startup delay caused the new failure or that ten seconds guarantees readiness.

## Exact proposed sequence and gates

1. Verify new package/nonce/run absence, current Windows17715 caller, resources and no existing HDC/8710 conflict. Create only one task-owned foreground official SDK23 HDC server.
2. After owned listener acceptance, monitor for ten seconds using the same pump/resource/notice/deadline mechanism as D04. This is an experimental observation window, not a handshake wait. Errors stop immediately.
3. Execute exactly one `hdc.exe -p -s 127.0.0.1:8710 list targets -v`, at most five seconds including terminal acceptance. The same bounded listing is projected separately for the fixed A/B identifiers. Both must be present exactly once with `CONNECTED`, complete format/EOF, local exit0, original-handle identity/close and persistent record qualification. Other fields are discarded in memory. CONNECTED is only a snapshot, not a handshake or future-connectivity assertion.
4. Only an accepted listing permits the unchanged B typed/proc query; only accepted B permits unchanged A. Each client has at most15 seconds including final acceptance. Exact BEGIN/RC framing, semantic parsing, EOF, normal local exit and original-handle close remain mandatory. No retry, polling, catch-up or replacement target.

The server's original ten-second first-ready acceptance cap is separate from subsequent continuous monitoring: an already accepted server is still checked for original-handle LIVE identity, listener, notice and work/root deadlines, without restarting startup acceptance or granting it twice. The targets client's original five-second deadline is checked before and after observation and at final acceptance; equality or overrun rejects.

A complete unsuccessful listing retains only fixed A/B presence/uniqueness/state with accepted=false and handshake_proved=false plus a fixed reason. Malformed/incomplete input retains only a fixed format reason. Neither projection grants the next query. Only Listing.row is adapted to the bounded whitespace-separated four-column grammar accepted during D04; feed/finish/erase and the other Listing method ASTs are reused. This does not mean the old strict-TAB Listing parser was device-verified. Byte/row caps, duplicate rejection and all terminal/close/identity acceptance gates remain required.

At most four HDC creations: one own server, one list and two serial targeted shells; at most two concurrent HDC processes. Total60 seconds, cleanup starts by56 seconds, each original owned handle gets at most one stop and two-second wait. Nominal query work is10+5+15+15=45 seconds; startup, sampling, records and close also consume time. Every step needs its remaining fixed budget, and lateness stops without extending the deadline. Existing services are never killed, reused or taken over.

## Fixed content

Original board root: `/data/local/tmp/mdds_direct_20261103_d5a29f38/data`.

| Board | Fixed source | Maximum bytes |
|---|---|---:|
| A | `failure.A`, `data-result.A`, `exit.A`, `stage.A` | 1024 each;4096 total |
| B | `failure.B`, `data-result.B`, `exit.B`, `stage.B` | 1024 each;4096 total |
| B | historical Native PID `/proc/<PID>/stat`, before and after | 4096 each;8192 total |
| B | same PID `/proc/<PID>/status`, only after first PID/starttime match | 8192 |

The unchanged source ceiling is25600 bytes; actual fixed typed/proc caps total24576. The exe link is not read; image bytes remain unverified. No app.out/app.err, old raw logs, keys, tokens, environment, command lines, executable content or content hash is read. The new target-list transport has a separate32768-byte parsing cap,512-byte rows/128 rows, inside the unchanged65536-byte per-client transport envelope. No list text or other-device fields persist.

The unchanged [typed/proc contract](D04_READONLY_EVIDENCE_EXECUTION.md) retains strict number/units/order/completeness/duplicate checks, non-atomic fixed-path/FD metadata comparisons, size and permission failures, sampled ENOENT handling and the run/PID association limitations. Private bytes, including hex, are live bounded memory only. Public output contains enum/phase/int32 return values, counts and association booleans. A queried record is not DDS success; invisibility is not remote termination.

## Resource, authentication and cleanup limits

Per-process sampled Private1355161600B /WS268435456B; aggregate2710323200B /536870912B. Admission requires commit8GiB/physical4GiB headroom; running below4GiB/2GiB or system commit growth greater than3GiB stops. Disk minimum1GiB. These are sampled stops with possible overshoot and blocking I/O, not hard limits.

Server volatile capture4MiB; each complete client65536B, new run16MiB, package+run24MiB. Four exact `cases/{server,targets,read.B,read.A}/tmp/.hdc.cache.log` paths share128KiB cumulative peak, metadata only. The two additional metadata paths are exactly `cases/server/tmp/.HDCServer.pid` and `cases/server/tmp/hdc.log`. All six count toward the same overall run budget/closing reserve; only the four cache paths share the cache peak. Ordinary/single-link/non-reparse and unknown-neighbor/rotation refusal rules remain. No cache content/hash is read; no history is deleted or fully copied.

Complete reliable server-stdout AUTH_WORD_ONLY unknown telemetry is the same explicitly scoped proposed exception and stays UNKNOWN; other server unknowns, client unknowns, explicit authorization requests/denials, Unauthorized target state, format/record/resource/identity errors stop. No authentication prompt is accepted. Unknown device state is distinct from authorization UNKNOWN and fails the CONNECTED gate.

Official HDC may generate/update host keys, enumerate/handshake connected USB devices beyond A/B and trigger automatic authentication-state changes. No new pairing/permanent trust, manual security/network/permission changes, deployment, chmod, identity selection/restoration, SDK/DDS execution, remote kill/cancel/deletion or file modification is included. Endpoint replacement can misdirect even a read-only query. Path-open races, external Job interference, disk stalls and remote shell termination remain risks. Only this run's verified original local handles are cleaned; remote Native identity/descendants/SDK and authentication restoration are not guaranteed. A later remote stop requires separate exact authorization and qualified ownership, not a historical PID.

## Qualification

Implementation is PRESENT and independently HOST_TESTED. The retained [initial 23-method report](../build/d04-prepared-readonly-package-20261103-d01/prepare/INITIAL_TEST_RESULT_D01.json) covers an earlier candidate, not the final corrected binding. The [formal D01 review](../build/d04-prepared-readonly-package-20261103-d01/prepare/AGENTIC_REVIEW_D01.json) retains its one P1 and three P2 findings. The subsequent Astra [D04 closure](../build/d04-prepared-readonly-package-20261103-d01/prepare/AGENTIC_REVIEW_CLOSURE_D04.json) closes all four with zero open blockers: first-server acceptance versus monitoring, targets deadline enforcement, safe rejected-list state/reason, and bounded D04 row compatibility. Its own 30-method review run is review evidence, not the independent final test.

The fresh independent [final report](../build/d04-prepared-readonly-package-20261103-d01/prepare/FINAL_TEST_RESULT_D01.json) records 30 new methods executed once after closure, native exit0 and outer-controller exit0, zero failures/errors/skips. This is the canonical execution evidence for the current source binding `902d8f45293b63bd3b920f0463f4b8261cf6e6ea3085efff20574909f8747c41`; neither the old 23 nor the review 30 substitutes for it. The prior typed/proc 48 tests were not rerun; their three public source/test pins remain exact and the query bodies are reused unchanged.

The separate [default-entry check](../build/d04-prepared-readonly-package-20261103-d01/prepare/FINAL_DEFAULT_ENTRY_D01.json) correctly returned native78 with controller0 and READINESS_NOT_GRANTED_HDC0. No --actual-once or live process/API action occurred. A retained [pre-test inventory attempt](../build/d04-prepared-readonly-package-20261103-d01/prepare/FINAL_SNAPSHOT_ATTEMPT_D01.json) was corrected only in the tester comparison after temporary imports were restored; no implementation/test edit or suite rerun followed.

Final checks matched 19 generated buffers/21 binding rows, the four-case registry, fixed limits and privacy/deadline gates. Current code/test pins and protected 68 status/metadata were unchanged; the five new controls and RUN remained absent. Typed/proc caps are 24,576 bytes within 25,600; the separate list cap remains 32,768 and the exe link is not read. All actual HDC/raw/key/cache/SDK/device actions were 0, with no new Native child or sampling process. Python/generated syntax and host-synthetic integration are not OpenHarmony or real-device evidence; Native rebuild is N/A. CONNECTED and process-state enums remain snapshots. B's current remote process/identity/descendants/SDK quiet remain UNKNOWN; the consumed E31 failure and both earlier DDS/read-only consumed opportunities are not changed or retried.

The final Commit gate: PASS and documentation Ready for commit: YES apply only to the four new public files and offline diagnostic preparation. This tool does not change production Transport or Fast DDS semantics. DEVICE_TESTED=false, future business readiness is NOT_PROVED and DDS remains NOT_VERIFIED. At this documentation snapshot, Main publication/freeze and final immutable hashes are pending; they establish the artifact, not execution authority. Only Main may change Git state. New actual execution requires a new exact single-run approval and fresh dynamic admission.

Proposed approval: one60-second PowerShell HDC batch, one own server, monitored ten-second observation, one fixed target-list snapshot plus unchanged B-then-A old-result queries, at most four HDC creations and three queries, with the exact content/resource/notice/cleanup rules above. Failure stops without retry. The new list and its metadata path are additions to the consumed read-only card; typed/proc access and all security/remote-operation boundaries remain unchanged.
