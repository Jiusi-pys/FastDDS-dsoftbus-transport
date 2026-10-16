# Offline rosbag2 String fixture and POSIX entry

R20 final testing passes the generated `bundle/run` and four fixed String
write-close-reopen/raw + separate typed readback on A/B independently. This is
declared-bundle entry and local SQLite persistence, not cross-device communication
or general ROS setup/CLI qualification.

## Host controls and fresh build

Run from the W3 root in the qualified Windows environment. Existing pinned
cpp/storage/SQLite/ROS artifacts and evidence are prerequisites; this driver
does not fetch/build a complete ROS distribution.

```powershell
$python = 'C:/pixi_ws/.pixi/envs/default/python.exe'
$artifactRepo = 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport'
$sdk = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native'
& $python -B tests/ros2_host/rosbag_string/package_elf_tests.py
& $python -B tests/ros2_host/runtime_entry_tests.py --shell 'C:/Program Files/Git/bin/sh.exe'
# Choose an unused positive integer; previous output cannot be overwritten.
$output = "$artifactRepo/build/r19baglocal6"
& $python -B tests/ros2_host/rosbag_string/build_and_bundle.py `
  --artifact-repo $artifactRepo --sdk $sdk --output $output
```

Canonical final build is r19baglocal5, freshly built at 2026-10-16 15:49:46 UTC:
compile/link/readelf/package exit 0; 851 header occurrences, 97 link members,
89 ELF members and 71 resources. libc.so stays external. Two pinned runtime
line-list indexes receive audited CRLF-to-LF copies; original copies are retained
and XML/package.xml/.msg/.idl remain byte-identical. The builder generates LF-only
run, RUNTIME_ENTRY.json, BUNDLE_MANIFEST.json and RUN_SPEC.json. RUN_SPEC describes
the invocation and does not itself prove deployment/runtime.

20 legacy mock/byte controls pass. The entry suite uses real POSIX shell dummy
consumers: 15 methods, 11 executed PASS and 4 Windows SKIP. Native Windows shell
supervisor codes are separate from the actual POSIX child status sidecar. Direct
PID/mode/symlink/colon/physical-LF obligations have separate actual board evidence;
the host skips remain SKIP. Earlier failed outputs remain immutable evidence.

## Direct device usage

Use explicit target A `3e01ff55454d202020104033bf453b00` or B
`3e01ff55454d202020104433991c3b00`, a new absent deployment directory and an absent
absolute bag URI. Verify board/platform/tools/space and loader, deploy exact
lib/share/run/receipt bytes and hashes, and preserve/set project run and executable
mode 0755 on the POSIX target. Existing prefixes remain evidence; never reuse
their bag directories. No token/permission/identity operation is part of this run.

After qualified deployment, invoke the physical entry directly from an unrelated
cwd. It replaces inherited AMENT_PREFIX_PATH and LD_LIBRARY_PATH with its own
physical prefix/lib; all other variables are inherited. Set RMW explicitly only
as required by this fixture. Do not source run or manually export AMENT/LD paths.

```sh
cd /data/local/tmp
# bundle is this run's new absolute deployed bundle path (spaces are supported).
env RMW_IMPLEMENTATION=rmw_fastrtps_cpp "$bundle/run" \
  rosbag_string_persistence "$bundle/bag" \
  "$bundle/share/std_msgs/msg/String.msg"
```

`run EXECUTABLE_BASENAME [ARG ...]` selects only declared lib executables and
checks declared required resources before exec. String's two arguments remain
an absent absolute bag URI and original String.msg; relative child arguments
retain caller-cwd semantics. The entry preserves argument bytes, cwd, PID/exec
and child exit/signal status. Fixture main has a 30-second deadline; retain the
recorded independent 40-second startup/runtime supervisor, remote exit/log/PID
capture, and explicit-target HDC invocation. HDC exit alone is insufficient.
Exact actual supervision/deployment/collection commands remain in the ignored
artifact repository's `build/r20-runtime-entry/final-test/board_{A,B}/` receipts.

Entry-file symlinks and physical bundle prefixes containing colon or LF are
unsupported (64); missing/nonexecutable child/resources reject with 66, noarg or
undeclared executable with 64. Use an absolute or `./` entry pathname, especially
for a leading-dash directory: `./-bundle/run consumer`. Explicit `/bin/sh --
-bundle/run consumer` resolver behavior is also tested; direct unprefixed
leading-dash shebang parsing is a separate interpreter limitation. Parent aliases
do not bypass physical colon/LF checks. Do not invoke the entry through PATH.

## Acceptance evidence and limits

Final direct entries were:

- A: `/data/local/tmp/r20_entry_20261016T155120_a/relocated bundle with space/run`
- B: `/data/local/tmp/r20_entry_20261016T155157_b/independent-runtime/run`

Both returned HDC 0/remote 0 from /data/local/tmp. Require exactly four CDR rows,
receive timestamps/order, topic/type/CDR/count, exact original message definition,
raw Reader followed by distinct typed reopen, retrieved SQLite integrity/metadata,
all three bag_fds=0/task_count=2 release phases with baseline thread IDs restored,
PID absence and deployment/retrieved hashes. Expected payloads remain empty,
local-string, Chinese text plus emoji, last, with receive times beginning
1700000000000000000 ns in 1000000 ns increments. Fixture PASS alone does not
replace independent collection checks.

Review R1/R2 repairs pass actual A/B physical one/two-LF and parent-alias cases
with existing distinguishable trimmed siblings (64, no child/wrong marker), and
explicit-shell/direct-./ leading-dash cases (37, exact argv/cwd/prefix). Generated
entry SHA256 is `1f429852d31a2e859a938989082fbf29039661c290ff65ad223b7b6191548a2b`.
See [current matrix and local final evidence](../../../docs/ROS2_ROSBAG_TARGET_PREPARATION.md).

Only declared-bundle/dummy behavior and this offline String ROS fixture are
qualified. ROS_DISTRO warning/empty distro metadata, empty offered_qos_profiles
and type_description_hash remain. Full ROS setup/CLI/record/replay, general
types/QoS/send-time persistence/large/concurrent/cache/crash/native cross-device
behavior remain NOT VERIFIED. Native full current project-only token-state and
supported lossless restoration material are still missing; do not retry rejected
whole-registry access or change security through this fixture. R21 bounded Host
delivery diagnostic is PLANNED, not implemented.
