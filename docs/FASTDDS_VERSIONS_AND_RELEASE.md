# Fast DDS versions, ROS 2 compatibility and the release workflow

Date: 2026-11-05. Scope: the DSoftBus Transport on two RK3588A OpenHarmony boards (SDK 23,
arm64-v8a). Status words follow AGENTS.md (BUILDS / HOST_TESTED / DEVICE_TESTED).

## 1. Answer

Fast DDS 3.x is required. Supported ROS 2 distributions use it today:

- **Kilted:** Fast DDS 3.2.x.
- **Lyrical** (the 2026 LTS) and **Rolling:** Fast DDS 3.6.x.

2.14.5 alone covers only Jazzy.

This project now supports three Fast DDS lines from one source tree:

| Fast DDS | ROS 2 | Status in this project |
| --- | --- | --- |
| 2.14.5 | Jazzy | BUILDS, HOST_TESTED, DEVICE_TESTED. This is the existing line, unchanged and still the default. |
| **3.6.2** (selected 3.x) | Lyrical, Rolling | BUILDS, HOST_TESTED (root suite against host Fast DDS 3.6.2.0), DEVICE_TESTED. Full suite, Security, Statistics, outage, restart, CFT race. |
| 3.2.5 | Kilted (EOL 2026-11) | BUILDS. DEVICE_TESTED by the full suite (see section 6). |

## 2. ROS 2 to Fast DDS matrix (evidence)

Every row comes from three upstream sources:

- `ros2/ros2` `ros2.repos` on the distribution branch: the source pin for a from-source build.
- `ros/rosdistro` `<distro>/distribution.yaml`: the released binary package version.
- `ros2/rmw_fastrtps` CMake files: the version the RMW requires.

All were read on 2026-11-05 through the GitHub API (`gh api repos/<owner>/<repo>/contents/<file>?ref=<branch>`).

| Distro | Status (rosdistro index-v4) | REP-2000 window | `ros2.repos` Fast DDS / Fast CDR | rosdistro release | rmw_fastrtps requirement |
| --- | --- | --- | --- | --- | --- |
| Humble | active | May 2022 to May 2027 | 2.6.x / v1.0.24 | fastrtps 2.6.12-1 | `find_package(fastrtps 2.6.2 REQUIRED CONFIG)` (rmw_fastrtps 6.2.11) |
| Iron | end-of-life | n/a | 2.10.x | n/a | n/a |
| Jazzy | active | May 2024 to May 2029 | 2.14.x / 2.2.x | fastrtps 2.14.7-1 | `find_package(fastrtps 2.13 REQUIRED CONFIG)`, `fastcdr 2` (rmw_fastrtps 8.4.5) |
| Kilted | active | May 2025 to **Nov 2026** | **3.2.x** / 2.3.x | **fastdds 3.2.5-1**, fastcdr 2.3.7-1 | `find_package(fastdds 3 REQUIRED)`, `fastcdr 2` (rmw_fastrtps 9.3.6) |
| Lyrical | active | not yet in REP-2000 | **3.6.x** / 2.3.x | **fastdds 3.6.2-1**, fastcdr 2.3.7-1 | `find_package(fastdds 3 REQUIRED)`, `fastcdr 2` (rmw_fastrtps 9.4.11) |
| Rolling | rolling | ongoing | **3.6.x** / 2.3.x | fastdds 3.6.2-1 | `rmw_fastrtps_cpp`: `find_package(fastdds 3 REQUIRED)` (9.6.1) |

Source revisions read:

- `ros2/ros2`: humble `0c6ced5a`, jazzy `cf1dbea3`, kilted `dbce0ebe`, lyrical `c2cce984`, rolling `f6edfcd5`.
- `rmw_fastrtps`: humble `e86bd8ef`, jazzy `6bba0a74`, kilted `1895dbe9`, lyrical `46111733`, rolling `6b8d7646`.

Notes:

- REP-2000's Kilted table still says "Fast-DDS 2.14.4". That entry is stale. The pin in `ros2.repos`, the released package in rosdistro, and `rmw_fastrtps` (which needs `fastdds 3`) all say 3.2.x.
- Fast DDS 3.x renamed the library and its CMake package from `fastrtps` to `fastdds`. A 2.x Fast DDS cannot satisfy `find_package(fastdds 3)`. So Kilted, Lyrical and Rolling cannot be built on 2.14.5.
- **Selected 3.x release: v3.6.2** (`39303846fb85`), the version Lyrical (LTS) and Rolling release. It has the same package version (3.6.2.0) as the repository's existing root CONFIG binding.
- **3.2.5** (`a060352adec2`) is also built and tested for Kilted, which reaches end of life this month.

## 3. API and ABI differences that matter to this project (2.14.5 to 3.x)

The checks below were made against the 2.14.5, 3.2.5 and 3.6.2 sources, and by compiling the
Transport for all three.

| Area | 2.14.5 | 3.x | Handling |
| --- | --- | --- | --- |
| Transport headers and namespaces | `*.h`, `eprosima::fastrtps::rtps::{Locator_t, LocatorList_t, LocatorSelector(Entry), PortParameters, PropertyPolicy, SenderResource}` | `*.hpp`, `eprosima::fastdds::rtps::*` | `FastddsApi.hpp` binds per line (it existed for 3.6 and is now built). |
| `TransportInterface` | no `is_locator_reachable` | `is_locator_reachable` is pure virtual; otherwise identical from 3.2.5 to 3.6.2 | Implemented (`override` on 3.x). |
| `SenderResource` send | `send_lambda_(const octet*, uint32, it*, it*, deadline)`, one contiguous buffer | 3.0 to 3.3: `send_buffers_lambda_(const std::vector<NetworkBuffer>&, uint32, it*, it*, deadline)`. 3.4 and later: `send_lambda_(..., int32 transport_priority)`; `send_buffers_lambda_` deprecated until 4.0 | Three branches in `DSoftBusSenderResource.cpp`. **New:** the 3.0 to 3.3 branch, needed for Kilted. |
| `LocatorSelectorEntry` | no `allowed_to_send`; selection from `enabled` | `allowed_to_send` added in **3.5.0**; 3.0 to 3.4 behave like 2.14 | **New:** `select_locators` falls back to `enabled` below 3.5 (found by the 3.2.5 build). |
| Transport priority QoS | not passed to transports | 3.4 and later pass `transport_priority` to `SenderResource::send` | Recorded in the transport's send record, no priority scheduling. Device case `transport_priority_23_delivery`. |
| Secure receive buffers | `RTPSParticipantImpl` caps them at 65535 (uint16) | same cap in 3.6.2 (`RTPSParticipantImpl.cpp:2010`) | A transport `maxMessageSize` of 65535 or less is still required for Security on both lines. Tests use 65500. |
| DDS `DataWriter::write` | returns `bool` | returns `ReturnCode_t` | Application code only (test `compat.hpp`). |
| `ReturnCode_t` | class with static members (`RETCODE_OK`) | `int32_t` alias with namespace constants | Bootstrap and tests. |
| `Duration_t` | `eprosima::fastrtps::Duration_t` | `eprosima::fastdds::dds::Duration_t` | Tests. |
| `TopicDataType` | `serialize(void*, SerializedPayload_t*)`, `getSerializedSizeProvider`, `createData`, `getKey`, `setName`, `m_typeSize` | `serialize(const void*, SerializedPayload_t&, DataRepresentationId_t)`, `calculate_serialized_size`, `create_data`, `compute_key` (two overloads), `set_name`, `max_serialized_type_size` | Types generated by fastddsgen 2.x must be regenerated with fastddsgen 4.x (this is what ROS 2's `rosidl_typesupport_fastrtps` does per distro). |
| Dynamic types | `eprosima::fastrtps::types` (XTypes 1.1 subset) | XTypes 1.3 API (`TypeDescriptor`/`MemberDescriptor`/`traits<>`; `set_*_value(id, value)` argument order swapped) | Tests only. |
| TypeLookup | `typelookup_config`, `register_remote_type`, `on_type_information_received` | removed. Remote types are resolved automatically into `DomainParticipantFactory::type_object_registry()`; discovery carries TypeInformation only | Device case `type_lookup_remote_type` uses each API. |
| Discovery listener | `on_participant_discovery(ParticipantDiscoveryInfo&&)`, `on_publisher_discovery(WriterDiscoveryInfo&&)` | `on_participant_discovery(status, const ParticipantBuiltinTopicData&, bool& ignore)`, `on_data_writer_discovery(status, const PublicationBuiltinTopicData&, bool&)` | Tests. |
| Discovery Server | `DiscoveryProtocol_t`, `RemoteServerAttributes` with the server GUID prefix | `DiscoveryProtocol` enum class; `m_DiscoveryServers` is a `LocatorList`; no prefix needed | Device case `discovery_server` uses each. |
| Renamed QoS members | `reliable_reader_qos().disable_positive_ACKs`, `times.heartbeatPeriod` | `disable_positive_acks`, `heartbeat_period` | Tests. |
| Security | plugin property names | unchanged; `onParticipantAuthentication` kept | Same PKI and governance used on both lines. |
| Statistics | `statistics/types/*PubSubTypes.h` (source tree), topic names | generated types `*.hpp`; same topics, `narrow`, `enable_statistics_datawriter`, `enable_monitor_service` | Same case on both lines. |
| Libraries / SONAMEs | `libfastrtps.so.2.14`, Fast CDR 2.2.5 | `libfastdds.so.3.6` (3.2: `.so.3.2`), Fast CDR 2.3.7 | **Both Fast CDR versions use SONAME `libfastcdr.so.2`**. The lines cannot share a library directory; each gets its own deploy root. |
| Public layout (ABI) | `LocatorSelectorEntry` and `TopicDataType` differ between lines | | A Transport, rmw or typesupport binary works only with the line it was built for. Rebuild everything above Fast DDS per line. |

QoS that Fast DDS itself does not implement:

- **2.14.5 and 3.6.2:** TimeBasedFilter, DestinationOrder BY_SOURCE_TIMESTAMP (rejected with `RETCODE_UNSUPPORTED` in 3.6.2), Presentation coherent/ordered access, LatencyBudget and DurabilityService. In 3.6.2 these policies are only copied into discovery data (verified in the `src/cpp` sources).
- **2.14.5 only:** TransportPriority is also missing.

## 4. Changes in this repository

Transport (no Fast DDS source change; ROS 2 layers untouched):

- `src/DSoftBusSenderResource.cpp`: 3.0 to 3.3 `send_buffers_lambda_` branch.
- `src/DSoftBusTransport.cpp`: `allowed_to_send` used only from 3.5.
- `include/.../FastddsApi.hpp`: includes `fastdds/config.hpp` for the version macros.
- The 2.14.5 and 3.6.2 libraries rebuilt after both edits are byte-identical to the ones tested (SHA-256 `bdeb7227…` and `e327562a…`).

Stock product (`integrations/ros2/stock`):

- `FASTDDS_DSOFTBUS_STOCK_FASTDDS=2.14|3.6|3.2` selects the line:
  - 2.14 (default) is unchanged; the library is `libfastdds_dsoftbus_stock_214.so`.
  - 3.6 builds `..._36.so`.
  - 3.2 builds `..._32.so`.
- `TransportBootstrap` uses the binding types.
- Optional `BUILD_STOCK_DEVICE_SUITE` builds `dsb_suite` and `cft_race`.

Release tooling (`scripts/release/`):

| Script | Purpose |
| --- | --- |
| `build_fastdds_ohos.sh <2.14.5\|3.6.2\|3.2.5> <work>` | Fetches pinned upstream tags and builds the closure (OpenSSL 3.5.9, foonathan_memory v0.7-4, tinyxml2 8c8293b, asio ed6aa8a, Fast CDR, Fast DDS) with SECURITY, FASTDDS_STATISTICS, TLS, SQLITE3 and COMPILE_TOOLS. Writes `BUILD_INFO.txt` with commits and SHA-256. |
| `pull_platform_libs.py <target> <dir>` | Copies the board's `libsoftbus_client.z.so` closure. It is used only for link-time `rpath-link`; nothing on the board changes. |
| `build_transport_ohos.sh <version> <work> <platform dir>` | Builds the Transport, `dsb_suite` and `cft_race`. |
| `run_cross_board.py` | Deploys to `/data/local/tmp/fdds_dsb/<version>/` and runs one of: the suite (`--groups`), a route outage (`--route 1\|2`), the DSoftBus CFT stress (`--stress wsf1\|wsf0`), or the UDP/TCP race reproducer (`--cft-race udp\|tcp`). |

Device suite (`tests/device_suite/`):

- One source for every line; `compat.hpp` holds the version differences.
- No host controller is involved. The boards synchronize through a TRANSIENT_LOCAL barrier topic carried over DSoftBus.

## 5. Reproducing from a clean clone

Prerequisites on the Windows host:

- Git Bash, Python 3, and GNU make (`scoop install make`).
- DevEco OHOS SDK 23 at `%LOCALAPPDATA%/OpenHarmony/Sdk/23/native`. Override with `OHOS_NATIVE` if it lives elsewhere.
- `hdc` from the same SDK.
- Two boards that are already DSoftBus-paired.
- The native token `mdds_actionbag_…` with `ohos.permission.DISTRIBUTED_DATASYNC`, provisioned on both boards. Select it with `--token-process`.
- The `softbus_trans_permission.json` entry for `com.kaihong.mdds.*`. The suite's session names derive from the package `com.kaihong.mdds`.

Steps:

```bash
# The GitHub default branch is not main; clone main explicitly.
git clone -b main --recursive https://github.com/Jiusi-pys/FastDDS-dsoftbus-transport.git && cd FastDDS-dsoftbus-transport
W=E:/fdds-rel     # any work directory
bash scripts/release/build_fastdds_ohos.sh 3.6.2 $W          # or 2.14.5 / 3.2.5
python scripts/release/pull_platform_libs.py <board A target> $W/platform
bash scripts/release/build_transport_ohos.sh 3.6.2 $W $W/platform
python scripts/release/run_cross_board.py --version 3.6.2 --work $W --a <A> --b <B>
python scripts/release/run_cross_board.py --version 3.6.2 --work $W --a <A> --b <B> --no-deploy --groups core --route 1
python scripts/release/run_cross_board.py --version 3.6.2 --work $W --a <A> --b <B> --no-deploy --cft-race udp --race-mode wsf0
```

Each run writes `run.log` and `result.json` under `$W/runs/` (or `--out`). The script's exit status is 0 only when every case on both boards passes and both Runtimes close.

## 6. Results (2026-11-05, both boards; evidence under `E:/fdds-rel-20261105/`)

### 6.1 Builds

All builds use OHOS SDK 23 clang 15 (aarch64-linux-ohos, `c++_shared`, Release) and come from the scripts above, starting from an empty source directory.

| Line | Fast DDS | Fast CDR | `config.h(pp)` | Libraries (SHA-256 prefix) |
| --- | --- | --- | --- | --- |
| 2.14.5 | `ebcb5c05` | 2.2.5 | HAVE_SECURITY, HAVE_SQLITE3, TLS_FOUND, FASTDDS_STATISTICS | `libfastrtps.so.2.14` `3713bba0`; transport `_214.so` `bdeb7227` |
| 3.6.2 | `39303846` | 2.3.7 | same | `libfastdds.so.3.6` `b8183d97`; transport `_36.so` `e327562a` |
| 3.2.5 | `a060352a` | 2.3.7 | same | `libfastdds.so.3.2` `6ba4f951`; transport `_32.so` |

Every line also builds OpenSSL 3.5.9 (`libssl.so.3` `42e0a6fd`, identical across builds), foonathan_memory 0.7.4 and tinyxml2 6.

The full `BUILD_INFO.txt` and the logs are in each `<work>/<version>/` directory.

### 6.2 Cross-board device results (A = `…4033bf453b00`, B = `…4433991c3b00`, DSoftBus)

| Run | 2.14.5 | 3.6.2 | 3.2.5 |
| --- | --- | --- | --- |
| Full suite: core 3, QoS 13 (incl. transport priority), feature 14, Security 2, Statistics 1 = 35 per board | 4 runs, 70/70 each | 4 runs, 70/70 each, plus 1 earlier run at 68/68 (before the priority case existed) | 3 runs, 70/70 each |
| Route outage, one 15 s IP drop (`--route 1`) | PASS | PASS (2 runs) | PASS |
| Route outage, two 15 s IP drops (`--route 2`) | PASS (max gap 17.4 s) | PASS (max gap 16.8 s) | not run |
| Peer OS-process SIGKILL and restart; Participant hot rebuild; second domain; four Participants per Runtime | inside the full suite | inside the full suite | inside the full suite |

The suite covers:

- **Core:** RELIABLE ordered exchange; 150000-byte fragmented sample; same-name Participant hot rebuild with TRANSIENT_LOCAL.
- **QoS:** keyed dispose/unregister, TRANSIENT_LOCAL late joiner, 300-sample KEEP_ALL burst, DEADLINE, AUTOMATIC liveliness, EXCLUSIVE ownership, LIFESPAN, partitions, BEST_EFFORT, KEEP_LAST 1, reader resource limits, positive ACKs disabled, 1 MiB sample, 20 topics, TRANSPORT_PRIORITY 23.
- **Features:**
  - Participants and discovery: second Participant; user/topic/group data in discovery; four Participants on one Runtime; Discovery Server; static EDP.
  - Writers, readers and types: asynchronous publishing with a flow controller (measured against an unthrottled baseline); SQLite TRANSIENT persistence; DataWriter loans; request/reply; WaitSet/ReadCondition; DDS-SQL ContentFilteredTopic; TypeLookup of a remote type.
  - Process and domain: concurrent second domain; peer process restart.
- **Security:** PKI-DH, Access-Permissions, AES-GCM-GMAC with everything ENCRYPT, a 150000-byte sample, and a deny rule. An untrusted identity is rejected.
- **Statistics:** peer statistics and monitor-service samples arrive over DSoftBus (`dst_locator_kind` = DSoftBus).

A first route2 attempt failed on both lines because of a host-script scheduling bug: the second outage started at 44 s, so samples could not resume before the 60 s window closed. After the fix (outages at 5 s and 29 s), both lines pass.

The legacy 2.14.5 direct-goal harness (`/data/local/tmp/mdds_direct_*`) is unchanged.

Results are in `E:/fdds-rel-20261105/batch/<version>-<run>/result.json` and `run.log`.

### 6.3 Host results

- **Stock suite, Fast DDS 2.14.5** (`integrations/ros2/stock`, Windows Release): 67/67 on this branch.
- **Root suite, CONFIG mode, host Fast DDS 3.6.2.0:** 303 of 391 pass on this branch and also on `origin/main`, with an identical failure set:
  - 87 tests are "Not Run" because of root link-graph gaps that already exist on `origin/main` (`HostProviderLoop` lacks `PublicSocketBackend`).
  - `dsoftbus.naming.configuration` fails only without the Fast-DDS submodule, and passes once it is initialized.

### 6.4 Content-filter race on Fast DDS 3.x (re-evaluated, not assumed)

Source (3.6.2):

- The removal path (`DataWriterImpl::remove_reader_filter`) now takes the writer mutex. That is upstream fix 0bd912836.
- `process_reader_filter_info` (the add/update path from discovery) still runs without it.
- The `DDSFilterFactory` expression pool is still an unlocked `ObjectPool`.

Board-to-board runs, 60 s each for UDP/TCP and 30 s for the DSoftBus stress:

| Transport | 3.6.2, writer-side filtering ON | 3.6.2, workaround (`reader_filters_allocation.maximum = 0`) | Controls |
| --- | --- | --- | --- |
| UDP | crash 2/3 (SIGSEGV) | 2/2 clean | unfiltered readers 1/1 clean; no local churn 1/1 clean |
| TCP | crash 1/3 | 2/2 clean | unfiltered 1/1 clean; no local churn 1/1 clean |
| DSoftBus | crash 3/3 | 2/2 clean | |
| 2.14.5, for comparison | UDP crash 1/1; DSoftBus crash 1/1 | UDP 1/1 clean; DSoftBus 1/1 clean | |

All 3.6.2 crash stacks:

- They enter at `DataWriterImpl::write` → `perform_create_new_change` → `ReaderFilterCollection::update_filter_info` → `DDSFilterExpression::evaluate`.
- They end either in a null dereference or in heap corruption in `DynamicDataImpl::clear_all_values`.
- The logs are in `E:/fdds-rel-20261105/crashes/`.

Conclusion: the defect is still present in Fast DDS 3.6.2. It does not depend on the transport, and the same workaround prevents it.

`docs/proposals/fastdds-3.6.2-content-filter-race.patch` adds the writer-mutex guard on the add path and a mutex on the expression pool. It is **not applied**, because changing Fast DDS needs approval. It is also not upstream on 3.6.2.

## 7. Limitations and open items

- **Content-filter race:** unresolved upstream in 2.14.5 and 3.6.2.
  - Use the workaround. It can also be set in XML: `writer_resource_limits/reader_filters_allocation/maximum = 0`.
  - Alternatively, apply the proposed patch once it is approved.
  - Making content-filter calls from several application threads still needs serialization.
- **Secure Participants:** they still require a transport `maxMessageSize` of 65535 or less on 3.6.2 (the same `uint16` cap).
- **Lines are ABI-separate:**
  - Fast CDR 2.2.5 and 2.3.7 share the SONAME `libfastcdr.so.2`, so each line needs its own library directory.
  - For Stage 2, `rmw_fastrtps` and the typesupport packages must be built per distro against the matching line: Jazzy against 2.14, Kilted against 3.2, Lyrical and Rolling against 3.6.
- **Kilted** reaches end of life in November 2026. Its 3.2.5 support is tested here, but 3.6.2 is the 3.x line to maintain.
- **Not exercised on the boards on any line:**
  - security logging plugin;
  - TCP with TLS;
  - the `fast-discovery-server` CLI;
  - PKCS#11;
  - same-host-only paths (data sharing, SHM, zero-copy, intraprocess).
- **`TRANSPORT_PRIORITY`** (3.4 and later) reaches the transport and is recorded, but DSoftBus sends are not reordered by priority.
- **Root host CONFIG suite:** the 87 link-blocked tests remain an existing gap; this change does not affect it.
- **Device suite prerequisites:**
  - It requires the provisioned native token and the `com.kaihong.mdds.*` permission entry on both boards. Provisioning them is outside these scripts.
  - The token lookup parses `nativetoken.json` in the field order these boards use.
- **ROS 2 over these lines:** NOT_RUN. That is Stage 2.
