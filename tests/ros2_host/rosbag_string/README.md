# Offline rosbag2 String persistence fixture

This fixture proves direct local rosbag2_cpp Writer/Reader persistence using
sqlite3/CDR, four fixed std_msgs/msg/String values and no cache. Independent
post-review runs passed separately on both RK3588A boards; this is not
cross-device ROS 2 or DSoftBus communication.

## Host controls and build

Run from the W3 repository root. These paths describe the qualified Windows
environment; the builder requires its existing pinned cpp/storage/SQLite/ROS
artifacts and evidence, rather than fetching or building a ROS distribution.

```powershell
$python = 'C:/pixi_ws/.pixi/envs/default/python.exe'
$artifactRepo = 'C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport'
$sdk = 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native'
& $python -B tests/ros2_host/rosbag_string/package_elf_tests.py
# Select an unused positive integer; existing output is deliberately rejected.
$output = "$artifactRepo/build/r19baglocal4"
& $python -B tests/ros2_host/rosbag_string/build_and_bundle.py `
  --artifact-repo $artifactRepo --sdk $sdk --output $output
```

The 20 host controls use mocked ELF metadata or pure resource bytes. The build
driver separately compiles, links, inspects and packages the real AArch64 fixture;
it never deploys or runs target code. It records commands/environment/raw logs,
headers/link inputs, 89 ELF members and 71 resources. libc.so remains external.
Only two exact runtime line-list indexes receive audited CRLF-to-LF conversion;
their original bytes are retained. Original XML/package.xml/.msg/.idl stay intact.
`RUN_SPEC.json` is a prospective invocation, not runtime evidence.

The canonical actual build is `$artifactRepo/build/r19baglocal3`, started
2026-10-16 14:19:01 UTC. Final testing reused it unchanged after complete binding
checks by explicit Main decision; it did not recompile. The builder is specific
to this pinned artifact tree, not a portable general ROS bag builder.

## Actual device fixture usage

Use an explicit HDC target and a new absent prefix/bag URI on every run. Final
targets were A `3e01ff55454d202020104033bf453b00` and B
`3e01ff55454d202020104433991c3b00`. Before deployment, confirm aarch64, the tested
board platform, free space, timeout/tar/sha256sum/env tools and loader presence.
Preserve existing prefixes. Deploy the bundle's exact lib/share tree, executable
mode and hashes into the new prefix; verify every hash before and after runtime.
Do not copy platform libc or change tokens/permissions.

Inside that qualified deployment, the executable accepts exactly two arguments:

```sh
# Set prefix to this run's new absolute deployment directory; bag must be absent.
env AMENT_PREFIX_PATH="$prefix" LD_LIBRARY_PATH="$prefix/lib"   RMW_IMPLEMENTATION=rmw_fastrtps_cpp   "$prefix/lib/rosbag_string_persistence"   "$prefix/bag" "$prefix/share/std_msgs/msg/String.msg"
```

Use bounded supervision around this invocation: the fixture has a 30-second
deadline after main entry, while the recorded shell runner uses
`timeout -s KILL 40` to cover loader/startup and preserves remote exit/stdout/
stderr plus runner/supervisor/fixture PIDs. HDC client exit alone is insufficient.
The actual final HDC form was `hdc -t <explicit-target> shell "sh <prefix>/run.sh"`.
The exact existing runner and deployment/collection commands are retained under
`build/r19-rosbag-preparation/rosbag-local-final-test/board_{A,B}/run.sh`,
`DEVICE_*.json` and raw logs in the artifact repository. Generate a new prefix
for any repeat; do not rerun these retained directories against an existing bag.
This README adds no execution/orchestration script.

Require remote exit 0, terminal status PASS and exactly four rows. Expected
payloads are empty string, local-string, Chinese text plus emoji, and last;
main.cpp binds their exact CDR bytes and receive times starting at
1700000000000000000 ns in 1000000 ns increments. Raw Reader checks topic/type/
CDR/count/order/time/bytes and the exact original String.msg definition. After
its destruction a distinct typed Reader reopens and checks payloads. All three
release phases must have bag_fds=0 and the exact baseline thread-ID set restored
(task_count=2 in these runs). Collect database/metadata/logs, match device hashes,
independently inspect SQLite integrity/schema/rows/definition/metadata, and
confirm all recorded PIDs are absent. A terminal fixture PASS alone does not
replace those independent collection checks.

## Qualified results and limits

Final A/B prefixes were `/data/local/tmp/r19_rosbag_local_20261016T143727_final_a`
and `/data/local/tmp/r19_rosbag_local_20261016T143739_final_b`. Both returned
HDC 0/remote 0; database SHA256 was
`ec0dc919934f7e66ba67f975d44067757e2e0fda88158d1755013cec7cc618f6`.
The canonical initial/review/final seals and capability matrix are in
[target preparation](../../../docs/ROS2_ROSBAG_TARGET_PREPARATION.md).

ROS_DISTRO unset warning is retained; optional ros_distro/type_description_hash
are empty and offered_qos_profiles=[]. No endpoint QoS, independent send-time
persistence, full CLI/record/replay, arbitrary types/sizes/concurrency/caching,
crash durability, general ABI/relocation or native/cross-device qualification
is implied. Named native/identity libraries cause the fixture to stop at its
observed-map checks. Actual native communication still lacks full current
project-only token-state and supported lossless restoration material; previously
rejected whole-registry access must not be retried through this local fixture.
Earlier initial/FIX1/FIX2 failures and harness failures remain sealed evidence.
