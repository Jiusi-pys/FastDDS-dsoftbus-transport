# ROS 2 offline closed SQLite/CDR oracle

Evidence snapshot: 2026-10-18 05:45 UTC. **Implementation PRESENT; HOST_TESTED
PASS; real record/play and native reader NOT_RUN; overall NOT VERIFIED.**
This two-file helper has final tester Commit gate PASS. Main graph/PRE_COMMIT
and publication remain pending. See [current status](STATUS.md) and the separately
published [no-Node ABI prerequisite](ROS2_ROSBAG_ABI_ACCEPTANCE.md).

## Contract and limits

`tests/ros2_target/bag_sqlite_oracle.py` exports `verify_recorded_bag(root, expected)`
and an independent XCDR1 String decoder. The caller supplies the exact run/topic,
selected ROS distribution, two received-file hashes, ten publisher CDR records,
offered QoS, type hash and source-bound message definition. The helper checks one
SQLite `.db3` plus `metadata.yaml`, SQLite schema 4 / metadata 9, one String/CDR
topic, ten distinct expected payloads in `(timestamp,id)` order, exact CDR bytes,
timing/counts, definitions and recursively type-exact metadata/QoS values.
It distinguishes the external YAML wrapper from the latest direct database
metadata row and permits historical metadata rows from the selected writer.

Input paths must be ordinary files/directories without symlinks/reparse points;
extra files, including WAL/journal/SHM, are rejected. SQLite opens with
`mode=ro&immutable=1`, query-only and trusted-schema off. File hashes/membership
are checked before and after; connections close explicitly. There is no repair,
checkpoint, reindex, recorder control, target deployment or transport action.
The return status is `PASS_CLOSED_BAG_CONTENT_ONLY`, with
`device_execution_verified=false` and `transport_close_verified=false`.

This is intentionally bounded to the selected ten-String fixture, one SQLite
file, one QoS profile, no compression and no split bags. It does not establish
general rosbag compatibility, live/concurrent recording, crash recovery, other
types, MCAP or full ROS 2 parity. The caller must separately prove native
recording, normal recorder close, pre/received/post collection hashes, real
publisher expectations, native playback and received payload/order. None of
those integration steps is executed or qualified by the current helper.

## Execution evidence

Working tree: `build/r15-lifecycle/publish-worktree`, based on published HEAD
`06e5917abd8906a5d17066b87223d239109c5374`. Final tester command, from that tree:

```powershell
C:/pixi_ws/.pixi/envs/default/python.exe -X utf8 -B tests/ros2_target/test_bag_sqlite_oracle.py
```

2026-10-18 05:44:24.197276--05:44:25.092518 UTC: **23 tests PASS, exit 0**.
Environment: Windows 11 build 26300; CPython 3.12.3 conda-forge; SQLite 3.45.2;
PyYAML 6.0.1. These are host-generated SQLite/YAML/CDR fixtures. They are not
actual rosbag recorder output or a native-reader test. Native compilation is
N/A for this standalone Python helper. Fast DDS/DSoftBus/device integration is
N/A at this helper stage because it is not yet connected to record/play.

Independent tester additionally passed 63 controls: 18 content/schema/file/YAML/
SQLite controls and 45 scalar-type corruptions across nested external/database
metadata and stored QoS. All 63 compare input membership, hashes, sizes and
modification times plus unchanged expectations. All fixture roots disappear by
normal cleanup with cyclic GC disabled, without `gc.collect` or monkeypatching.

The first fixture run failed with Windows `WinError 32` (BSI-01); explicit
connection release fixed the fixture and later tests passed. Independent review
then found float/int coercion accepted in metadata/QoS (BSO-01); recursive exact
type comparisons and isolated fixture expectations fixed it. Fix2 review closed
BSO-01 with no actionable findings, followed by the independent final tester.
The first final audit also failed after its suite passed: its helper incorrectly
required all publication manifest members inside one directory. v2 allows only
the exact sealed sibling handoff and rehashes it. The successful suite was reused
only with identical source/Git/index guards. All failed scripts, logs and exit
codes remain sealed; no failure was relabeled PASS.

Evidence paths below are relative to root `build/r25-native-actionbag/`:

| Evidence | SHA-256 / scope |
| --- | --- |
| `bag-recordplay-implementation/FIX2_FREEZE.json` | `0580f826a93d3d770bfe9f477f52be03805d6977f135077e52d4787fb13eeaf4` |
| `bag-sqlite-oracle-fix2-review/RESULT.json` | `15762e4c92255297172d136253161ecf3daebeaa28acc97955f46faf7013b0bc` |
| `bag-sqlite-oracle-fix2-review/SEAL.json` | `ce3b9506ee8c0867726a446585a0e0d2b15a274986afa9e08f68c05e8efd54e6`, 12 members |
| `bag-sqlite-oracle-final-test/RESULT.json` | `476b92cc83d2c8ba801481df0603691fb9ab8fc56925542c70b435d91d9cfdc1` |
| `bag-sqlite-oracle-final-test/SEAL.json` | `56f695350e5e923a713186e4809ebe0614b7cfe505e149ea53d11fa809648814`, 66 members |
| `main-bag-abi-publication/SHA256_MANIFEST.json` | `90191ddf9348749111d5b46a21798812cf62a8071181bffa275795ce2e447d30`, 96 members |

Final source hashes: oracle
`ea2ace8d576355e79e78c8566ae02bf2052a23e299fe2528bdd2caaf62377b32`;
test `0801224fa26d22ea006abeff13b564faf9c9cef7e987b3ca19926e25a77e2427`.
The freeze also pins local official SQLite table creation, YAML conversion,
metadata I/O and sequential writer contracts. Final tester rechecked seven
preceding seals (962 members), including the prior 753-file real no-Node ABI
seal, without rerunning device execution. Complete argv/cwd/time/exit and raw
streams are preserved in the final tester directory.

## Publication, device observation and next gate

The ABI prerequisite was normally published to existing
`origin/codex/flow-encoding-published-20261014` as
`06e5917abd8906a5d17066b87223d239109c5374`; exact-SHA CI at
2026-10-18 05:23:02 UTC was NOT_RUN, zero runs/checks/status contexts.
`ROSBAG_ABI_PUBLISHED_HANDOFF_20261018.json` and publication receipts retain proof.
That prior publication does not include this new helper and supplies no
record/play or Transport-close qualification.

Main's separate read-only `hdc list targets -v` observation at 05:42:32 UTC,
exit 0, showed `3e01ff55454d202020104033bf453b00` and
`3e01ff55454d202020104433991c3b00` as USB Connected. Its raw output is retained
under `bag-recordplay-visibility-20261018/user.stdout.bin`; an earlier sandbox
empty listing was not proof of offline boards. This visibility observation is
not a device acceptance test. No HDC call was made by this helper's tester.

Next work requires separately reviewed native record/play integration and fresh
real-board execution, normal close and immutable collection before using this
oracle, then native replay and exact received payload/order verification.
R22 remains STOPPED; prior failures and native ABI reproducibility limitations
remain unchanged. No DDS/RTPS/RMW/Transport semantics were modified.
