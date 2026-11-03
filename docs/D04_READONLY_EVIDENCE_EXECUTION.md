# D04 fixed read-only evidence collection

This is a diagnostic-tool stage, not a production Transport change. D04 DDS acceptance remains FAIL/HOLD; its once record is consumed. This new scope is prepared offline and requires a new direct user approval before any HDC call. No trial, deployment or identity action is authorized by this document or package generation.

## Purpose and evidence limit

D04 rejected A's long-client receipt before the unique zero remote-RC marker was verified. `NATIVE_REMOTE_NONZERO` also covers missing, duplicate or malformed markers; the actual numeric return code and Native failure phase remain unknown. All11 payload pins match their provenance. The original Native parser and request constructors passed the independent CPU-only control-input test, including16 context comparisons. These do not prove remote pair consumption or SDK entry.

The executable writes small typed records at the original data root. Collect those records rather than redeploying or guessing an SDK/permission cause. B's stopped local HDC process is not proof of remote exit. This scope observes B's previously bound PID and original proc starttime; it grants no remote stop authority.

## Fixed actions

- Windows user17715, the existing official SDK23 HDC binary and the same fixed A/B identifiers from D04. The private identifiers are in the frozen package, not this public document.
- One new task-owned foreground server on127.0.0.1:8710, then exactly the fixed B query and the fixed A query, serially. At most3 HDC creations and2 queries. A starts only after B's complete local/remote query receipt is accepted.
- The client form is `hdc.exe -p -s 127.0.0.1:8710 -t <fixed target> shell <fixed body>`. `-p` prevents implicit server creation. There is no list query, retry, polling, catch-up, fallback body or replacement target.
- Admission refuses existing HDC/port conflicts, old/new run collisions, pin changes, wrong caller or insufficient resources. It does not stop or take over another instance.

Fixed original root on each board:

`/data/local/tmp/mdds_direct_20261103_d5a29f38/data`

| Target | Exact inputs | Source-byte maximum |
|---|---|---:|
| A | `failure.A`, `data-result.A`, `exit.A`, `stage.A` below that root | 1,024 each;4,096 total |
| B | `failure.B`, `data-result.B`, `exit.B`, `stage.B` below that root | 1,024 each;4,096 total |
| B | `/proc/<historically bound Native PID>/stat` before and after the other reads | 4,096 each;8,192 total |
| B | `/proc/<same PID>/status` once, only after the first PID/starttime check | 8,192 |

The original proposal allowed an additional1,024 bytes for the exe link. **This executable card does not read that link at all**: the available `readlink` interface cannot enforce that source-byte bound. It returns `EXE_LINK_NOT_READ_BOUNDED_INTERFACE_UNAVAILABLE` and `IMAGE_BYTES_UNVERIFIED`. Executable source reads therefore total at most24,576 bytes, within the unchanged **25,600-byte** approval ceiling. This omission is not a successful image verification.

No other file, glob, directory listing, PID discovery or process-tree expansion is included. No `app.out`, `app.err`, old raw log, key, token, environment, command line, executable body or content hash is read.

## Read and projection contract

The fixed shell opens only the permitted file, checks ordinary/single-link/non-symlink metadata, compares the opened FD with the path, and compares identity/size/mtime again after the bounded read. Existing record size must be1..1,024 bytes; oversize or unsupported checks abort without a body read. A missing record produces `MISSING` only after the fixed path's metadata tool reports the C-locale ENOENT suffix and the root passes directory/search checks; permission errors, other errors and unknown wording abort. There is no waiting. Metadata/path-open checks remain sampled, non-atomic observations. A change between checks can still race the open, including a blocking special file; no `O_NOFOLLOW` guarantee is claimed.

Source reads use `dd bs=1 count=N`; `od` only encodes the already bounded output pipe. A separate read-command status footer must be zero. The public [Toybox dd implementation](https://github.com/landley/toybox/blob/master/toys/posix/dd.c) sets the input block size from `bs` and bounds the input read by the remaining count. This is an interface/reference-source basis, not proof that the deployed firmware is byte-identical to that source. Unsupported or nonconforming tool behavior is not converted to success. Real local shell syntax/utility tests are distinct from OpenHarmony execution.

Proc snapshots use complete bounded records. Reading exactly the cap is `BOUNDARY_UNKNOWN`, even if the prefix happens to parse. The first stat must match the retained PID and native starttime before status is read. The second stat and owner predicates must also match. A changed parent is retained as a false association predicate and prevents acceptance of B and the start of A. Kernel X/x states have a separate dead-snapshot enum and are never called running. Invisibility, reuse, permission/read failure and partial snapshots remain distinct unknown/failure observations; neither an open error nor PID invisibility proves all remote work exited. The snapshot cannot prove a current token, complete ancestry, image bytes, future state or SDK callback quiet.

Hex-encoded private data is still private. Raw/hex streams are processed only in bounded volatile buffers and discarded. No raw capture file is created. Runtime client INTENT/receipts reference a fixed command pin instead of duplicating the private query body; original creation/permit checks still compare the actual fixed argv. The existing notice detector sees only the live transport stream; it persists controlled event enums, not text. The typed decoder accepts exact public producer keys/order/number ranges and emits only:

- Fixed record/format/error enums and byte counters.
- Public Native phase and signed32 return/status codes with validity bits.
- Restore/drain/retained/quiescence flags; writes/takes categories, never payloads.
- Run/PID/owner/parent association booleans, never the private values.
- Fixed B process-state enums and explicit unknown remote descendants/SDK silence.

`failure`, `data-result` and `stage` do not independently bind a complete run/PID. Their fixed-path association remains weaker than a matching `exit` record and accepted historical context. A last-stage snapshot is not an execution trace. Missing records and successful query execution do not become successful DDS or restored-identity evidence.

## Limits and stop policy

| Bound | Value |
|---|---:|
| Total window / start cleanup | 60s /56s |
| Each serial client / original-handle bind | 15s /2s |
| Each task-owned original-handle stop / wait | At most1 /2s |
| Simultaneous HDC processes | At most2 |
| Per-process sampled Private / working set | 1,355,161,600 B /268,435,456 B |
| Aggregate sampled Private / working set | 2,710,323,200 B /536,870,912 B |
| Admission commit / physical headroom | 8 GiB /4 GiB |
| Running minimum commit / physical headroom | 4 GiB /2 GiB |
| System commit growth stop | Greater than3 GiB |
| Per-client complete transport envelope | 65,536 B |
| Server capture cap | 4 MiB; volatile only |
| New run / package+run | 16 MiB /24 MiB |
| `cases/server/tmp/.hdc.cache.log` | Three cache paths share128 KiB cumulative peak; metadata only |
| `cases/read.B/tmp/.hdc.cache.log` | Same shared cache peak; metadata only |
| `cases/read.A/tmp/.hdc.cache.log` | Same shared cache peak; metadata only |
| `cases/server/tmp/.HDCServer.pid` | Existing run budget only, outside shared cache peak; metadata only |
| `cases/server/tmp/hdc.log` | Existing run budget only, outside shared cache peak; metadata only |

These five paths are relative to the fresh run root and must be ordinary single-link files. All five count toward the existing run-byte budget, including its closing reserve; only the three `.hdc.cache.log` paths share the128 KiB cumulative peak. Contents are never opened or hashed by the metadata scan.

These are sampled resource stops, not hard memory caps. Overshoot, disk stalls and external job interference remain possible. The fresh local run must be absent and distinct from D04; old evidence is retained, not copied in full or deleted.

Explicit authorization requests/denials, client authentication UNKNOWN, unsupported or malformed data, resource/record failures and identity contradictions stop the batch. The new approval must expressly include the same narrow exception for complete server stdout `AUTH_WORD_ONLY` UNKNOWN telemetry; it may be recorded while observing, but stays UNKNOWN. Other server unknowns stop. No prompt is accepted and no authentication state is manually changed. The cleanup path still closes only verified task-owned local handles and reports incomplete remote termination honestly.

## Effects and recovery boundary

Official HDC can generate/update host authentication keys, enumerate/handshake connected USB devices beyond A/B, and trigger the previously disclosed automatic device-authentication-state changes. No new pairing/permanent trust is accepted. Endpoint replacement can misdirect a query. Non-atomic path checks, delayed sampling, logging/storage stalls and incomplete remote shell cleanup remain risks.

There is no upload, deployment, chmod, remote directory/file creation, identity selection/restoration, SDK/DDS business call, cancellation, remote kill, deletion or system/network/permission modification. The board's original D04 directories stay intact. Local cleanup affects only this run's original owned handles; it does not promise rollback of authentication state, remote identity, descendants or SDK callbacks.

If B is later observed alive, do not stop it under this card. A later action needs fresh native identity and a separately verified task-owned supervisor/direct-child wait right or qualified cooperative cancel path, plus exact approval. A historical detached PID alone is not a safe signal capability.

## Qualification and approval

Implementation/build/review/final-test/doc/publication evidence is recorded in the package `prepare` directory. Offline qualification is not board testing. The final card/manifest/binding/source-binding hashes and source commit must agree; the new once, admission, authorization and run directory are absent during preparation.

The retained [initial D02 report](../build/d04-readonly-evidence-package-20261103-d01/prepare/INITIAL_TEST_RESULT_D02.json) is earlier-stage evidence: nine fresh unique methods, 32 reused valid methods and the expected default-entry refusal. Its initial audit/syntax tooling failure and correction are retained; it does not substitute for current post-review validation.

The independent Astra [D04 review closure](../build/d04-readonly-evidence-package-20261103-d01/prepare/AGENTIC_REVIEW_CLOSURE_D04.json) closes all three P2 findings with zero open blockers: private client argv persistence is redacted without changing execution/permit checks, final acceptance obeys both original deadlines, and authorization source generation is deterministic. The ENOENT/denial, parent-association and dead-state corrections are included in final validation.

The canonical [final report](../build/d04-readonly-evidence-package-20261103-d01/prepare/FINAL_TEST_RESULT_D01.json) records **48 unique methods PASS**, zero current failures/errors/skips. Coverage is 44 valid passes from the first full 48 attempt plus four later targeted passes. The [first full 48 execution](../build/d04-readonly-evidence-package-20261103-d01/prepare/FINAL_SUITE_EXECUTION_D01.json) remains FAIL, exit1 with four errors: the tester wrapper passed executable=None to os.fspath before subprocess creation. After correcting only that wrapper, the [four targeted methods](../build/d04-readonly-evidence-package-20261103-d01/prepare/FINAL_TARGETED_EXECUTION_D02.json) passed, exit0, without implementation/test changes. There were 52 method invocations; this is not a second full 48 run. Original reports and isolated local failure logs remain preserved.

The separate [default-entry check](../build/d04-readonly-evidence-package-20261103-d01/prepare/FINAL_DEFAULT_ENTRY_D01.json) correctly refused with native exit78 and READINESS_NOT_GRANTED_HDC0 before any live import/process attempt. Its validation controller exited0; exit78 is an expected refusal, not an execution success. No --actual-once invocation or actual authorization was created. Top-level final-report freeze-contract aliases were added without changing tests or rerunning them, with the [pre-alias report](../build/d04-readonly-evidence-package-20261103-d01/prepare/FINAL_TEST_RESULT_D01.pre_contract_alias.json) retained.

The current source-binding SHA256 is `740cc3717ffde488938b23445ed7a753d7f9c14334a76b4d6dcf990d5142b1ab`. Final checks matched 17 generated source buffers and 19 binding rows, deterministic regeneration, exact B-before-A registry/dependencies and all read/deadline/privacy limits. The declared ceiling is 25,600 source bytes; executable reads remain 24,576 maximum because the exe link is explicitly not read. Protected 68 status/metadata and current public pins remained unchanged during final testing; five new controls and RUN remained absent.

This is IMPLEMENTED/HOST_TESTED diagnostic preparation only. Python compilation/generated syntax and host synthetic/Git Bash utility checks do not establish OpenHarmony shell/tool behavior or firmware equivalence. Native build is N/A; actual HDC/raw/SDK/device calls were all 0. DEVICE_TESTED=false, DDS acceptance remains NOT_VERIFIED, and B's current remote process/identity, descendants and SDK callback quiet remain UNKNOWN.

The final Commit gate: PASS and documentation Ready for commit: YES apply only to these four public files: readonly_evidence.py, prepare_readonly_evidence.py, their test module and this card. Only Main may stage/commit/publish. At this documentation snapshot, publication/freeze remains pending; Main's later final hashes and qualification establish the frozen artifact, not actual execution permission. A new specific single-run user approval and fresh dynamic admission remain mandatory.

Proposed user approval: allow one60-second PowerShell HDC batch, one own server and at most two fixed read-only queries, B then A, for these exact D04 typed files and B process metadata, with25,600-byte maximum source content and the stated server UNKNOWN exception/side effects. Failure stops without retry. No deployment, identity switch, remote stop or DDS trial is included.
