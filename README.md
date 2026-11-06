# Fast DDS OpenHarmony DSoftBus Transport

English | [简体中文](README_zh.md)

A custom [Fast DDS](https://github.com/eProsima/Fast-DDS) transport for
OpenHarmony devices, using the **DSoftBus Socket API** to carry RTPS traffic.
The project targets applications that need DDS communication over DSoftBus,
including integration with ROS 2 through Fast DDS.

The transport is responsible for addressing, connections, sending, receiving and
resource lifetime. Discovery, reliability, fragmentation, history and QoS remain
responsibilities of Fast DDS. The integration uses the Socket API exclusively.

## Architecture

```text
DDS application / ROS 2 application
                |
       Fast DDS DDS / RTPS
                |
        TransportInterface
                |
       DSoftBus Transport
                |
     OpenHarmony DSoftBus Socket API
                |
        Peer OpenHarmony device
```

The repository contains the transport implementation, SDK-free lifecycle and
routing components, native Socket bindings, integration examples, and host and
device test tools. The stock integration registers the transport through Fast DDS
public transport configuration. Earlier integration and diagnostic paths are kept
in the repository with their own build instructions and evidence.

## Choose a build path

| Goal | Entry point | Scope |
| --- | --- | --- |
| Build the native transport for OpenHarmony | [Version and release guide](docs/FASTDDS_VERSIONS_AND_RELEASE.md), `scripts/release/`, `integrations/ros2/stock/` | Stock Fast DDS 2.14.5, 3.6.2 or 3.2.5; version-specific libraries and device suite |
| Develop the cores and inspect interface contracts on Windows | Root `CMakeLists.txt`; commands below | Default `SOURCE_PROBE` binding, SDK-free components and host fixtures |
| Run unmodified ROS 2 nodes on two boards | [Hub deployment guide](docs/ROS2_HUB_DEPLOYMENT.md), [Stage 2 record](docs/STAGE2_ROS2_DEVICE_20261105.md) | `2.14.5-ros` only; bounded `DEVICE_TESTED`, `NOT_PRODUCTION_QUALIFIED` |
| Work with the earlier installed package or experimental consumer | [Installation guide](docs/INSTALL.md), [input preflight](docs/INSTALL_PREFLIGHT.md) | Separate root `CONFIG` and historical integration paths; follow their exact dependency and ABI requirements |

The default root build is for host development. A device runtime requires the
explicit native build, platform libraries and deployment configuration described
in the version and release guide. The root `CONFIG` binding and the stock build
are separate CMake entry points.

## Get the source

Git and access to the configured submodule remotes are required. The current
submodule URLs use SSH.

```sh
git clone --recurse-submodules git@github.com:Jiusi-pys/FastDDS-dsoftbus-transport.git
cd FastDDS-dsoftbus-transport
```

For a new checkout whose submodules have not been initialized:

```sh
git submodule update --init --recursive
```

The parent repository pins the submodule commits. Keep those pins when reproducing
a checkout; updating a submodule to its remote branch changes the source inputs.
The release scripts fetch their own explicitly selected upstream versions as
described in the release guide.

## Host development on Windows

Prerequisites: CMake 3.22 or later, Visual Studio 2022 with the C++ tools,
initialized submodules, Python 3, Node.js (`node` on `PATH`) and Windows
PowerShell. Use a fresh build directory.

```powershell
cmake -S . -B build/host-source-probe -G "Visual Studio 17 2022" -A x64 -DFASTDDS_DSOFTBUS_FASTDDS_MODE=SOURCE_PROBE -DBUILD_TESTING=ON -DBUILD_NATIVE_PERMISSION_PROBE=OFF
cmake --build build/host-source-probe --config Debug --parallel 4
ctest --test-dir build/host-source-probe -C Debug --output-on-failure
```

These commands exercise the selected host configuration. They do not deploy to a
board or establish device acceptance. Tests that use fake Socket symbols validate
the host contracts within that fixture's scope.

## OpenHarmony build and use

The native workflow uses an OpenHarmony SDK 23 AArch64 toolchain and platform
libraries matching the target firmware. Cross-board examples require two paired
OpenHarmony devices, `hdc`, and the process identity and permissions required by
DSoftBus. The recorded hardware is RK3588A running KaihongOS/OpenHarmony.

Follow [the release workflow](docs/FASTDDS_VERSIONS_AND_RELEASE.md) for the exact
prerequisites, version selection, build commands, deployment paths and cross-board
procedure. Its tools cover:

- Building pinned Fast DDS and dependency sources with `build_fastdds_ohos.sh`.
- Collecting target link inputs with `pull_platform_libs.py`.
- Building the transport and device programs with `build_transport_ohos.sh`.
- Deploying and running the selected two-board scenarios with `run_cross_board.py`.

For application integration, start with the
[stock bootstrap headers](integrations/ros2/stock/TransportBootstrap.hpp),
[scoped bootstrap wrapper](integrations/ros2/stock/ScopedTransportBootstrap.hpp)
and [device suite examples](tests/device_suite/README.md). Select the matching
Fast DDS line and supply explicit runtime and peer configuration. Keep dependency
ABIs consistent within each process.

## Validation and limitations

Build and test evidence is tied to a specific version, configuration, device image
and scenario. The [version matrix](docs/FASTDDS_VERSIONS_AND_RELEASE.md) records
builds and two-board tests for Fast DDS 2.14.5, 3.6.2 and 3.2.5, including the scope
of host testing. This is not a blanket qualification of every ROS 2 distribution
or deployment configuration.

- DDS feature coverage, security, statistics and recovery scenarios are described
  in the version matrix and its acceptance records.
- A ContentFilteredTopic filter-factory race is recorded for Fast DDS 2.14.5
  and 3.6.2. The documented workaround disables writer-side filtering and requires
  application serialization of filter changes and teardown. Proposed upstream
  patches are not applied. See the
  [CFT results and workaround](docs/FASTDDS_VERSIONS_AND_RELEASE.md#64-content-filter-race-on-fast-dds-3x-re-evaluated-not-assumed)
  and the [2.14.5 investigation](docs/FASTDDS_FULL_BUILD_AND_CFT_RACE_20261105.md).
- ROS 2 acceptance is recorded separately in the
  [ROS 2 capability matrix](docs/ROS2_CAPABILITY_MATRIX.md). Stage 2 is bounded
  `DEVICE_TESTED` and `NOT_PRODUCTION_QUALIFIED` on `2.14.5-ros` only, which is
  Fast DDS 2.14.5 plus one approved patch; see the
  [Stage 2 record](docs/STAGE2_ROS2_DEVICE_20261105.md). The
  [hub deployment guide](docs/ROS2_HUB_DEPLOYMENT.md) covers several ROS processes per
  board, the Python layer built from pinned sources, its limits, and what production
  qualification would need. Stage 2 remains `NOT_RUN` on
  stock 2.14.5, 3.6.2 and 3.2.5. ROS applications must not create
  ContentFilteredTopics while writers are active, because the workaround cannot be
  applied through `rmw_fastrtps`. Earlier ROS runs do not qualify a newly built
  Fast DDS or transport binary automatically.
- Platform deployment contracts, production qualification and distribution rights
  have separate requirements; consult the
  [production support contract](docs/PRODUCTION_SUPPORT_CONTRACT_V1.md).

For dated development progress and remaining work, use
[STATUS](docs/STATUS.md) and the acceptance documents it links. [PLAN](PLAN.md)
contains design and historical work records; a dated checkpoint applies to its
own evidence scope.

## Repository layout

| Path | Contents |
| --- | --- |
| `include/fastdds_dsoftbus/`, `src/` | Transport interfaces, implementation and supporting components |
| `integrations/ros2/stock/` | Stock Fast DDS transport bootstrap and native library build |
| `integrations/ros2/` | ROS 2 integration, per-board hub (`hub/`), preload library (`autoload/`), historical adapters and diagnostics |
| `scripts/release/` | Native dependency builds, transport builds and cross-board runner |
| `examples/`, `tests/`, `probes/` | Consumers, host contracts, device scenarios and probes |
| `docs/` | Architecture, installation, support contracts and acceptance records |
| `Fast-DDS/` | Fast DDS source submodule |
| `communication_dsoftbus/` | OpenHarmony DSoftBus source submodule |

## Development and contribution

Follow [AGENTS.md](AGENTS.md) for the architectural boundaries and validation
workflow. Keep changes within the transport and necessary adapters, preserve
DDS/RTPS semantics, and use independent execution evidence and review for
implementation changes. Communication changes require the applicable real-device
and cross-device checks.

## Licensing and third-party materials

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency attribution
and distribution constraints. The repository does not currently provide a root
first-party release license; dependency licenses do not grant a license to all
project code. Vendor SDKs, device images and platform binaries have their own terms.
