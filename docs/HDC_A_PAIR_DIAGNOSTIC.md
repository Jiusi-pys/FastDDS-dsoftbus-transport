# Fixed A-pair diagnostic proposal

Implementation: PRESENT. Host compilation/tests: PASS. Device test: NOT_RUN (DEVICE_TESTED=false). Stage1: NOT_VERIFIED; Stage2: all37 HELD. This is an offline qualified proposal, not a device-readiness or execution authorization claim. Final freeze follows Main's source commit and published-byte binding; Main owns staging, commit and push. This document is not a publication receipt.

## Proposed scope and gate

Scope `ONE_NEW_SAME_SERVER_A_PAIR_60S_D01`, nonce `A8D3F641_A_PAIR_60S_D01`: one owned official SDK23 HDC server under the fixed Windows17715 account and only original fixed A. Private `COMMANDS_D01.json` binds the exact executable, endpoint, target and arguments; this public card omits device identifiers and private argv. At most three HDC creates: server, A1 and conditional A2; two distinct fixed printf payload tags, serial clients, no B/DDS/deployment or identity/permission/network changes. An existing unknown endpoint owner means HOLD, not termination. Preserve this attempt's evidence without deleting history or copying the repository.

The total window is60 seconds, work stops for cleanup at56 seconds, and each client has15 seconds (server readiness10 seconds; binding2 seconds). A1 success skips A2. A2 is eligible only after A1 whole stdout exactly equals these three public lines with one uniform LF, CRLF or CRCRLF ending on every line:

```text
[Fail][E000004]:The communication channel is being established.
Please wait for several seconds and try again.
[Fail]ExecuteCommand need connect-key? please confirm a device by help info
```

A1 must also have empty stderr, native exit0, no stop attempt, original-handle terminal identity proof, EOF, normal close, its RESULT and durable rejection journal, unchanged owned server, and no UNKNOWN/request/denial/record failure. Only the designated complete known-failure prefix is temporarily collected until A1 goal_started+15 seconds; every other blocking event stops immediately, including server UNKNOWN. The old server UNKNOWN exception is not enabled in this scope.

A1's failure is retained with accepted=false and business_blocked=true. A2 must start within2 seconds of A1 close with at least15 seconds left in the original work window. Both native create and resume use a deadline tightened to `min(existing work deadline, first_closed+2s, original_work_deadline-15s)`; preparation time cannot escape the checks. There is no fixed inter-sample sleep, retry loop, third sample or automatic failure retry. Ordinary bounded monitor polling is not an extra diagnostic sample.

Results distinguish FIRST_SAMPLE_SUCCESS (one observed successful roundtrip), OBSERVED_FAILURE_THEN_RECOVERY (failed A1 retained and successful A2), and NO_SECOND_SUCCESS_OBSERVED (no second success established). A separate diagnostic-completion result never makes the failed sample business success. None establishes cold-start causality, continuous/permanent failure or an internal handshake bit.

## Resource and evidence envelope

Candidate `goal_contract.py` preserves LIMITS: per-root Private1355161600 bytes and working set256MiB; aggregate Private2710323200 bytes and working set512MiB. Startup requires available commit8GiB and physical4GiB; runtime stops below commit4GiB/physical2GiB or above3GiB commit growth. These sampled thresholds are not hard OS caps. Active8MiB plus run16MiB gives combined24MiB; active128 files, run96, closing32 files, closing byte reserve256KiB, minimum disk1GiB. API journal limit8MiB; server capture4MiB, each client64KiB, each stream32KiB, total decoded320KiB. Sampling ceilings: system2000 epochs, process1700 samples, server900, each client160; the maintained monitor uses approximately100ms sampling. Original-handle cleanup allows at most one stop and a2-second wait.

Actual candidate metadata-only cache grammar has these three exact paths relative to the new run root, sharing128KiB cumulative peak:

- `cases/server/tmp/.hdc.cache.log`
- `cases/probe.A1/tmp/.hdc.cache.log`
- `cases/probe.A2/tmp/.hdc.cache.log`

The scanner also permits `cases/server/tmp/hdc.log` and `cases/server/tmp/.HDCServer.pid`, covered by overall run accounting. Other paths are rejected. The earlier six-path summary does not apply to this scope; Main confirmed the three-path grammar as intended, without expanding implementation. Run scanning reads metadata only, including cache identity/non-shrink/peak checks; it does not read or hash cache/log bodies. Fixed inputs/tools, generated sources, dynamic outputs and closing reserves remain part of package accounting.

Official startup may create/update host keys, enumerate USB and affect automatic authentication state. This proposal accepts no new pairing or permanent trust and no manual security/network changes. Residual risks include read-only misdelivery if the endpoint is replaced, opaque external Jobs/descendants, synchronous disk writes and sampling overshoot, remote-shell termination uncertainty, and UI/trust/restoration uncertainty. Original-handle finite cleanup cannot guarantee whole-tree or remote termination.

## Qualification and remaining gates

Safe evidence is under `build/hdc-a-pair-package-20261102-d01/prepare/`: initial36 PASS (`INITIAL_TEST_RESULT_D01.json`); Tier3 Astra P2 native second-client budget finding (`AGENTIC_REVIEW_D01.json`); two boundary counterexamples RED/exit1 preserved; tightened deadline and focused closure CLOSED (`AGENTIC_REVIEW_CLOSURE_D04.json`). Fresh independent `FINAL_TEST_RESULT_D01.json` runs `python -I -B tests/test_hdc_probe_pair.py`13/13, `tests/test_hdc_notice_family.py`13/13 and `tests/test_hdc_goal_cleanup.py`10/10, all exit0/native0/outer0. It records three in-memory source compilations, all16 runtime byte matches and18 source-binding rows, default entry78 with live WinDLL0, no actual controls or run, and unchanged source/candidate bytes. Binding SHA256: `6db55615337aed933c8c1ed5a900c9e9827cb01e62cee95c881c1c32ba2e6628`. These are pure/Fake host tests, not live SDK/HDC/device or two-board DDS evidence.

A new run requires fresh direct approval for this scope/nonce and its frozen private binding, followed by separate authorization, Main admission, fresh preflight and exclusive once controls. None is prepared by this documentation or the offline builder, and no actual execution has occurred in this phase. Old exhausted opportunities are not reset or reused. Final freeze and root PRE_COMMIT remain separate gates. The corrected three-path cache description above governs this scope; old six-path descriptions cannot substitute for it.
