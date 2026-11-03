# Fixed dual-board DDS with bounded startup evidence

Offline implementation and candidate binding qualification have passed independent
host validation. Fresh final testing passed 132 methods (26 integration + 48
read-only + 30 prepared read-only + 14 qualification + 14 DDS composition), with
zero failures, errors or skips and native/outer controller exits 0. Independent
Tier 3 review raised two P2 EOF/cleanup findings; both are closed, with zero
remaining source blockers. Historical failures and consumed execution
opportunities remain unchanged.

Execution status remains NOT_EXECUTED: no new HDC, board, Native SDK or identity
operation, AUTH, once reservation or RUN exists. This is an unfrozen offline
candidate. Main must publish and then freeze the concrete execution-card/source
bindings before requesting the specific single-run approval; no final card hash
or new execution permission is asserted here.

Evidence: [canonical final tests](../build/directdds-startup-cause-integration-20261103-d01/FINAL_TEST_RESULT_D01.json),
SHA256 `d285ce35e326dca3d539930f6f64413f805cedc5000333b000417698c304e899`;
[candidate final qualification](../build/directdds-startup-cause-package-20261103-d01/prepare/FINAL_TEST_RESULT_D02.json),
SHA256 `9b2e112df885d898900c464d3519b71a16283ada7c960e3f2da85d2aaba59644`;
[original review](../build/directdds-startup-cause-integration-20261103-d01/AGENTIC_REVIEW_D01.json)
and [targeted review closure](../build/directdds-startup-cause-integration-20261103-d01/AGENTIC_REVIEW_FIX_D02.json).
The candidate's current active source-binding SHA256 is
`babb3b764a124281a8397e7087f8e9d5813ff14b7b46f074621a5079fc576dbf`:
22 active source rows and 14 external inputs totaling 22842456 bytes. Only two
Native payloads change; the other nine payloads and three tools retain their
qualified identities. The 123-entry registry and all 34 resource limits remain
unchanged. Host evidence does not establish Native or DDS acceptance.

This document is intentionally synchronized after final testing; its draft hash
in the final evidence is historical. The four implementation/test pins and 22
active source rows remain the tested revision. Documentation changes do not
constitute a code retest or a binding freeze. Prior failed execution/fixture and
report-schema diagnostics remain preserved; the corrected binding-protection
report resolves the reporting predicate without rerunning or rewriting tests.

Scope `ONE_NEW_CURRENT_STOCK_DDS_AB_STARTUP_CAUSE_240S_D05`, nonce
`F47C9E20_DIRECT_DDS_240S_D05`. Package
`build/directdds-startup-cause-package-20261103-d01`; host output root
`E:/Codex-FastDDS-directdds-once-20261103_f47c9e20`; fixed A/B new board root
`/data/local/tmp/mdds_direct_20261103_f47c9e20`. Existing roots or unknown HDC
instances/port ownership stop admission. No previous path or once record is
reset, overwritten, resumed or deleted.

## Delta from the previous complete DDS plan

Of 11 Native payloads, only `own_goal_runner` and `own_goal_launcher` change.
They are the ARM64 outputs documented in [Native startup cause](NATIVE_STARTUP_CAUSE.md),
runner 305392 bytes/SHA256
`ec733e0cd2a7d53d8178d7082c002b7f867bc2ceb6a2e6ed185b583cfde0c393`,
launcher 91928 bytes/SHA256
`cbd24f3f45eaefba1cb711c80a56a5defa4df85baff3cf725fe60b1b863da363`.
The runner is below the preserved supervisor's 1 MiB file limit. The other nine
qualified payload identities are reused by reference, without complete archive
copies. Payload, admission, Pair, source, manifest and execution bindings must
all refer to the new hashes. Existing admission/Pair records are not reused.

No standard DDS/RTPS/Transport success oracle, Native status, phase, public NS
POD ABI, permission profile or identity bridge changes. Required outcome remains
standard DDS payload/sequence/GUID validation, three messages in each direction,
and the separate existing entry-restore and exit checks. Cause records cannot
convert a failure, UNKNOWN, missing record or `NO_RECORDED_FAILURE` into success.
SDK quiet and complete worker/descendant restoration remain separate UNKNOWNs.

## Old B state before deployment

The previously accepted observation only established that the fixed old B direct
PID was not visible at two historical snapshots. It did not prove complete tree
exit or SDK quiet. The existing B resource-query client now also takes exactly
`stat -> status -> stat` observations of that fixed PID, comparing the preserved
starttime, parent and UID/GID. Its comparison context is privately pinned; it is
not supplied by interactive input. It reads at most 4096+8192+4096 source bytes.
Only fixed state/match enums are retained; no process names or identifier values
are reported or stored in raw captures.

A visible process (including zombie/dead snapshots), reused identity, owner or
parent mismatch, inconsistent observations, unreadable state or malformed frame
stops before deployment/Native launch. Two NOT_VISIBLE snapshots only mean that
the fixed old direct PID was not observed. Proceeding still accepts the disclosed
possibility of old descendants, unrecovered worker identities, SDK activity or
interference; it is not a cleanup declaration. There is no kill, same-name
search, process-tree walk or permission change. A later stop operation would
need a separately justified, exact target and permission.

## Inline cause reads and capture boundary

Each already registered data launch command preserves its Native return code.
After the supervisor returns, a separate subshell reads only that new run's
`data/cause.A` or `data/cause.B`, at most once and at most 512 source bytes per
side. The old Native return code is printed unconditionally afterward. Query
`exec`, variable assignments and `exit` affect only the subshell. No stopped
driver is restarted and no extra HDC service/client or identity call is added.

Remote path/type/link/open/read/close consistency is sampled, not atomic
O_NOFOLLOW protection. Files larger than 512 are rejected before content reads;
the byte-at-a-time read has an explicit 512-or-smaller count. Host projection also
enforces 512 and uses the strict fixed-field decoder. Complete invalid content
is reported only as fixed UNKNOWN; MISSING remains MISSING. Framing errors,
truncation, duplicate frames, unexpected query close, notice rejection and
recording failures remain fail-closed.

The three special stdout streams use one bounded projection before either raw
capture or business-output storage. Native/resource bytes are forwarded
unchanged; diagnostic wire bytes are held only in bounded memory and replaced
with safe enums. The original notice checker and raw-byte budgets still see
every byte. Partial creation is guarded before resume: adapter installation
failure prevents raw diagnostic capture during cleanup. Existing creator,
identity, signal authority, EOF and original-handle cleanup mechanisms are not
replaced. Stderr handling remains the original policy.

During failure cleanup, cached EOF triggers projection finalization before the
existing capture closes. Reserved incomplete magic prefixes are rejected, and
ordinary partial tails are flushed before close. `Capture.close` always executes
the original close in `finally`; underlying close errors remain in the original
cleanup accounting. Completed projections or fixed diagnostic errors are then
retained, with publication attempted at most once, including journal-failure and
repeated-outcome paths. These operations add no process or identity query.
An incomplete frame or record failure cannot skip physical cleanup, erase the
original business failure or re-enable a stopped driver. Errors retain the
original failure and force `overall_unconditional_pass=False`; they cannot
produce overall PASS. Envelopes explicitly use `observation_only=True` and
`transport_qualified=False`, including a valid projection whose original close
failed. Format validity is not transport or Native execution attestation.

An already launched remote shell may execute this fixed read after Host halt or
disconnect; halt cannot retract it. A byte cap is not a time cap, and the Host
window cannot guarantee remote process exit. If B has not returned before local
cleanup, its cause may never be observed. Fixed-path cause association does not
prove same-run provenance by itself, and a record does not prove SDK silence.

## Preserved operation and resource limits

- One self-created foreground official SDK23 HDC service; at most 121 HDC client
  steps and one existing non-HDC Windows controller, registry 123 entries.
  A/B targets remain privately fixed. No connection-only sampling detour.
- Total Host window 240 seconds, work stops at 236; clients 15 seconds, Native
  long roots 125 seconds with the existing 120-second remote supervisor. No
  failed command retry. Original-handle stop at most once, wait at most 2 seconds.
- New A/B directory only, 11 files each, only four new executables set to 0700.
  No old/system file overwrite or deletion. New test outputs remain for evidence.
- Existing dedicated Native identity only: at most eight selection/restoration
  attempts across the four probe/data windows. Its existing complete permission
  set becomes active; the DATASYNC probe does not enumerate every permission.
  No identity creation, persistent permission/profile or network modification.
- Per-root sampled Private stop 1355161600 bytes and WS 268435456; aggregate
  Private 5420646400 and WS 1073741824. Start commit/physical margins 8/4 GiB;
  running margins 4/2 GiB, commit-growth stop 6 GiB. These are sampled stops and
  can overshoot, not hard caps. Original storage, output, cache and journal caps
  remain exactly in the frozen contract; diagnostic bytes consume those caps.
- Original limited complete server stdout AUTH_WORD_ONLY UNKNOWN telemetry may
  continue observation while retaining UNKNOWN semantics. Identified requests,
  denials, client UNKNOWN, malformed protocol or recording failure stop. No
  automatic pairing, permanent trust or security-setting approval is performed.

Official HDC can create/update host keys, enumerate attached USB devices and
change automatic authentication-flow state. Port replacement can misdirect
reads or deployment. External Jobs, memory overshoot, blocked I/O and incomplete
remote/identity cleanup remain risks. Cleanup is limited to this batch's proven
owned handles; it does not stop unknown HDC or old B processes. These effects,
the old-B residual uncertainty and the inline postludes require a new specific
single-run approval immediately before PowerShell HDC. This offline candidate creates no
AUTH, preflight, admission, once or run record.
