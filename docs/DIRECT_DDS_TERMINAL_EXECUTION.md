# Complete stock DDS execution with original-handle terminal proof

This is an offline-qualified candidate and proposed execution scope, not an
execution grant. At this documentation/final-test snapshot, publication and
freeze have not occurred. Root's later final hashes, private fixed card and
successful qualification establish the exact frozen package; they do not grant
actual execution. This document contains no self-hash or provisional final
manifest hash. A new specific single-run approval and fresh dynamic admission
are required before HDC.

## Binding and exact change

Scope `ONE_NEW_CURRENT_STOCK_DDS_AB_TERMINAL_PROOF_240S_D04`;
nonce `D5A29F38_DIRECT_DDS_240S_D04`.
Package `build/directdds-terminal-package-20261103-d01`.
Host run `E:/Codex-FastDDS-directdds-once-20261103_d5a29f38`.
Each privately fixed A/B board uses the new directory
`/data/local/tmp/mdds_direct_20261103_d5a29f38`; an existing root stops admission.
Exact device identifiers and the existing Native identity name are recorded
only in Root's private fixed card, with the final entry argv and artifact pins.
The isolated Python bootstrap adds only verified ACTIVE for the frozen entry.

The [proposal basis](../build/directdds-terminal-package-20261103-d01/prepare/PROPOSAL_BASIS_D01.json)
retains the complete [prior DDS action/risk scope](DIRECT_DDS_DF_EXECUTION.md).
Relative to full DDS baseline 5578a61, this package includes the published
0d0bb762 host fix and eight namespace bindings. Relative to that published fix,
the builder changes only PACKAGE, RUN, DEVICE, SCOPE, NONCE, PROBE_RUN, DATA_RUN
and CARD_PATH. The lifecycle helper, generated Win32 observation adapter and
backend carry the reviewed host changes. Queries, payloads, resource thresholds,
notice policy, standard DDS/RTPS behavior and success oracles are unchanged.

The [host fix](HDC_TERMINAL_IDENTITY.md) keeps original process-handle/PID/native
birth, previously verified full live-image and retained image-pin/FileID anchors
strict. Only ImageUnavailable may lead to one additional same-handle read-only
wait in that failure branch, at most 50ms and clamped to the original remaining
deadline. This is not a termination guarantee or added timeout. Observation
failure can permit later terminal-only proof; contradictions latch permanently.
It never restores revoked signal/resume authority, and require_live rejects a
terminal process while retaining its valid proof. Journal root/server labels
are scoped before observation.

Per-root RESULT remains a pre-acceptance content candidate: PENDING with
accepted_evidence=False on disk. Final server/deadline checks precede in-memory
acceptance through the existing authoritative global accepted ledger; exceptions
reset memory acceptance without rewriting that immutable receipt. DDS retains
its original work/server deadlines and the probe retains its separate original
listener deadline. No LIMITS key or timeout is added.

## Full procedure, artifacts and finite actions

Reference and freshly verify 14 fixed external inputs totaling 22,839,168 bytes,
including the same 11 Native payload files. No Native rebuild or archive
duplication occurs; consumed authorization/once controls are not reused.
The Native product belongs to the bound
candidate-d03 source view; this package does not rebuild or certify every dirty
file in the root checkout. Each board receives own_goal_runner,
own_goal_launcher, goal_supervisor, readonly_frontend and private lib copies of
the stock DSoftBus Transport, FastDDS 2.14.5/FastCDR 2.2.5, foonathan, tinyxml2,
libc++ and project identity bridge. Only those four newly uploaded programs get
mode0700. New test deployment does not overwrite old project/system files.

The complete sequence is retained:

1. Create at most one owned foreground HDC service. After the existing owned
   server acceptance, passively observe for ten seconds within the original
   budget, then perform the fixed targets/provider/resource sequence. This is
   not a handshake guarantee, cold-start diagnosis or connection-sampling run.
2. Stage and hash-verify all 11 files on both boards, chmod the four programs on
   each, and query the fixed existing project Native identity.
3. Perform the per-board identity inheritance/restore probes, then the two
   standard DDS Participant/Topic/Writer/Reader consumers. Each side sends and
   receives exactly three run/side/ordinal-bound payloads. Four releases,
   READY/DATA_DONE, DDS entity deletion, restore and normal-exit receipts remain
   required; local process exit alone cannot substitute for these proofs.

At most one service, 121 registered fixed HDC clients (115 normal plus six
currently unreachable cancellation-publication steps) and one non-HDC Windows
controller: 123 possible roots. At most three clients run concurrently, for at
most five live roots including service/controller. No automatic command retry.
The six cancellation steps are not published by this workflow; host cleanup
does not implement remote cancellation.

Across the four probe/data windows, existing Native selection/restoration
attempts total at most eight. They activate the complete existing permission
set of the privately fixed dedicated project Native identity, package
com.kaihong.mdds. There is no new identity, account/profile, registration,
init/Reload/delete or persistent permission change. Abnormal restoration is
not guaranteed. No ROS2 or above-Transport behavior change is included.

Total 240 seconds; work stops at 236 seconds, with the final four reserved for
cleanup. Ordinary clients 15 seconds, long launches 125 seconds, controller 110
seconds, bind 2 seconds. Each proved original owned handle permits at most one
stop attempt and at most a two-second wait while its authority remains valid.
An existing HDC/8710 listener blocks startup; there is no takeover or unrelated
instance termination. Existing new device directories also block admission.

## Unchanged limits

All 34 LIMITS are retained exactly. Byte values below are sampled stopping or
storage/output bounds, not a hard reservation or a hard real-time guarantee.
Start requires commit/physical free 8/4GiB; runtime free below 4/2GiB or commit
growth above 6GiB stops. C and E each require 1GiB free. Per-root private stop is
1,355,161,600B and working set 256MiB; aggregate private 5,420,646,400B and working
set 1GiB. Sampling, API calls and synchronous writes can overshoot these stops.

| LIMITS key | Fixed value and meaning |
| --- | --- |
| total_ms | 240000ms |
| work_ms | 236000ms |
| client_ms | 15000ms |
| long_ms | 125000ms |
| bind_ms | 2000ms |
| stop_ms | 2000ms |
| clients | 128 internal cap; the registry still contains only 121 HDC clients |
| concurrent_clients | 3 |
| private | 1355161600B per root |
| ws | 268435456B per root |
| aggregate_private | 5420646400B |
| aggregate_ws | 1073741824B |
| start_commit | 8589934592B free |
| start_physical | 4294967296B free |
| stop_commit | 4294967296B free |
| stop_physical | 2147483648B free |
| commit_delta | 6442450944B |
| server_capture | 4194304B |
| client_capture | 65536B per client/controller |
| decoded | 327680B total |
| active_bytes | 33554432B |
| run_bytes | 67108864B |
| total_bytes | 100663296B combined |
| files | 1536 runtime entries |
| cache_peak | 131072B fixed cache metadata |
| system_epochs | 13000 |
| process_samples | 26000 |
| api_journal_bytes | 50331648B |
| disk_min | 1073741824B per required host disk |
| closing_reserve | 262144B |
| board_total_kib | 4194304KiB |
| board_available_kib | 1048576KiB |
| board_disk_kib | 65536KiB for the fixed queried filesystem |
| native_fsize | 1048576B |

Preparation remains at most 8MiB/128 files. Active closure 32MiB, run 64MiB and
combined 96MiB; server capture 4MiB, each client/controller 64KiB, decoded 320KiB,
notice aggregate 12MiB and API journal 48MiB. Existing Native address-space/file/
time limits remain; the 1MiB image/file bound accommodates the bound runner.
Only fixed case/control/snapshot and exact HDC cache/log metadata paths are
admitted. Unknown paths, links or identity changes stop. HDC key/cache/log
contents and old raw streams are not read or hashed by this preparation/run
scope; official HDC may itself create or update its host keys as described below.

## Notice exception, effects and recovery

Only a healthy, complete server-stdout MONITOR/CLEANUP UNKNOWN record classified
AUTH_CONTEXT_UNKNOWN/AUTH_WORD_ONLY may continue under the new explicit grant.
It remains UNKNOWN and can hide unrecognized authentication meaning. Recognized
requests/denials, every client UNKNOWN, ECODE-only/BOTH, partial/oversize or
malformed records, and recording failure stop. Exact public program/step/hash
diagnostics still require independent remote return-code, identity and payload
acceptance; they are not generic authentication exceptions. No prompts, new
pairing or permanent trust are accepted. Even successful payload/exit checks
with retained UNKNOWN yield conditional AUTH_UNKNOWN/exit1, not unconditional
overall PASS.

Official HDC may create/update host keys and enumerate/handshake all connected
USB devices, including devices outside the two fixed business targets, changing
automatic authentication-flow state. Fixed-target checks are not atomic endpoint
binding: replacement can misdirect deployment or execution. This scope creates
no new pairing/trust, permission/network/security configuration or persistent
profile. External Jobs, sampled memory overshoot and blocking logs/publication
remain risks. Abnormal remote identity restoration, remote descendants and SDK
callback quiet remain UNKNOWN; own local-handle closure does not prove them.

Failure stops new business and attempts only bounded cleanup of proved original
owned handles with valid authority. No PID reopen, tree-kill, unknown-instance
stop, recursive deletion, token deletion, system rollback or trust rollback is
promised. Revoked signal/resume authority is never regained by terminal proof.
Retain new test files and evidence. No cleanup/overwrite/reuse of the consumed
old directory `/data/local/tmp/mdds_direct_20261103_c2d7a641` is authorized.

## Current evidence and freeze boundary

The [initial tester](../build/directdds-terminal-package-20261103-d01/prepare/INITIAL_TEST_RESULT_D01.json)
passed 14 composition tests, exit0 and zero skips. The subsequent Tier3 Astra
[review](../build/directdds-terminal-package-20261103-d01/prepare/REVIEW_CLOSURE_D02.json)
has zero open blockers. The fresh independent
[final tester](../build/directdds-terminal-package-20261103-d01/prepare/FINAL_TEST_RESULT_D02.json)
passed 14 composition tests, exit0, zero skips/failures/errors. It is canonical
for this binding. Source/input binding SHA256:
`779cc3afcf1c32d14ea881896cb623a6c846cc85b1ed3d66e6ae6be7b5f96cb1`.

Lifecycle 27 plus cleanup 10 are 37 reused tests from the separate
[host final51](../build/terminal-identity-analysis-20261103-d01/FINAL_TEST_RESULT_D01.json),
with exact source/test hashes and the explicit eight-binding namespace delta.
Those 37 and the native child/natural/controlled samples were not rerun here.
This phase is fresh composition 14, not fresh 51. Native binaries were neither
rebuilt nor copied into new archives.

Final checks matched 18 source/input rows, all 14 freshly rehashed external
inputs, the 123-entry registry/digest and all 34 limits. Protected 68 status and
metadata, old D03 public sources and safe frozen/control metadata were unchanged.
The new RUN and all five active controls were absent; actual execution remains
unauthorized. This documentation is added after final testing and does not
alter its runtime/test pins. Root still owns publication, exact UTF-8/LF Git
blob versus working-tree verification, freeze and static qualification; later
final hashes/private binding establish that later snapshot, not permission.

The consumed D03 [actual result](../build/directdds-df-contract-package-20261103-d01/prepare/ACTUAL_TEST_RESULT_D01.json)
remains FAIL_OR_HOLD and its once is spent. A's 11 files/verification/chmod4 and
read-only identity query were accepted; B stopped after four accepted file
transfers and an unaccepted fifth start. B's full verification, probes,
identity setters and DDS data were NOT_RUN. Current board state has not been
rechecked. The prior 405-byte local diagnostic permit is also consumed; it grants
no new HDC or DDS action.

| Evidence axis | Current state |
| --- | --- |
| New binding/current host composition | HOST_TESTED: 14 fresh PASS |
| Lifecycle/cleanup evidence | 37 reused, exact pins; not rerun |
| Independent binding review | Zero open blockers |
| Native build | NOT_REBUILT; bound candidate-d03 artifacts reused |
| New HDC/SDK/device/identity/DDS execution | NOT_RUN |
| New actual authorization | Absent; new specific grant required |
| DEVICE_TESTED / overall DDS | false / NOT_VERIFIED |

The final Commit gate: PASS and documentation Ready for commit: YES cover only
offline binding. Main retains unresolved graph/pre-commit disposition; UNKNOWN
graph targets are not a clean or zero-impact result. No new AUTH, admission,
fresh proof, once or actual entry is created by this document.

## Proposed single-run approval text

This is a template, not approval already received. It applies only after Root
appends the final immutable hashes/private binding and qualification PASS:

> 我批准按本卡及 Root 最终附加并验证通过的 manifest、binding 和私有固定卡，
> 对 ONE_NEW_CURRENT_STOCK_DDS_AB_TERMINAL_PROOF_240S_D04、nonce
> D5A29F38_DIRECT_DDS_240S_D04，在新的 d5a29f38 主机运行目录和 A/B 设备目录，
> 经 fresh admission 后仅执行一次完整 240 秒 DDS 流程：固定 HDC 服务/查询、
> 两板各 11 文件与四个 mode0700 程序、既有专用 Native 身份最多八次选取/恢复、
> 双向各三条 DDS 数据及恢复/退出证明，并严格遵守本卡全部动作、34 项限额与停止条件。
> 我理解 HDC 可影响所有已连接 USB 设备的自动认证流程及主机密钥，存在端点误送、
> 采样超调、日志阻塞和远端恢复/SDK quiet 未证风险；接受仅本卡限定的完整 server
> AUTH_WORD_ONLY UNKNOWN 继续观察，UNKNOWN 仍保留且不构成无条件 PASS。
> 不接受新配对/永久信任、权限/网络/持久 profile 变更、旧 once 复用或自动重试；
> 失败只允许本卡限定的原句柄清理，并保留旧目录、新文件和证据。
