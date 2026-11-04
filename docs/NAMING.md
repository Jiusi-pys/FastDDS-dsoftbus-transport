# Fast DDS OpenHarmony DSoftBus Transport naming

This project implements a Transport for Fast DDS using the OpenHarmony DSoftBus
Socket API. It is not a separate DDS middleware, RMW implementation or runtime.

The public source and build interfaces are deliberately renamed:

| Previous interface | Current interface |
| --- | --- |
| `mdds::dsoftbus` | `fastdds_dsoftbus::dsoftbus` |
| `mdds::experiment` | `fastdds_dsoftbus::experiment` |
| `<mdds/dsoftbus/...>` | `<fastdds_dsoftbus/dsoftbus/...>` |
| `<mdds/examples/...>` | `<fastdds_dsoftbus/examples/...>` |
| `find_package(MDDS CONFIG REQUIRED)` | `find_package(FastDDSDSoftBusTransport CONFIG REQUIRED)` |
| `MDDS::mdds_dsoftbus_transport_facade` | `FastDDSDSoftBusTransport::fastdds_dsoftbus_transport_facade` |
| `MDDS::mdds_experimental_runtime` | `FastDDSDSoftBusTransport::fastdds_dsoftbus_experimental_runtime` |
| `MDDS_*` build options | `FASTDDS_DSOFTBUS_*` build options |
| `MddsKeyedSample` public example type | `DSoftBusKeyedSample` |

No old header, namespace, archive or CMake package aliases are installed. Build and
install into a fresh prefix, then rebuild consumers; do not overlay an old prefix
and interpret leftover archives/configs as this delivery. Generated example fields,
keys and CDR layout are unchanged, but registered type identity changes. No ABI or
wire interoperability with the old generated type is asserted.

The eight former public CMake cache options fail with `OBSOLETE_TRANSPORT_OPTION`
and their exact replacement name. Unrelated parent-project variables with a
similar prefix are not rejected. New keyed-reference contracts explicitly set
`registered_type` to `DSoftBusKeyedSample`; historical contracts default to their
old name. Both sides must match the selected type, so mixed or unknown types fail.

## Preserved historical identities

Original artifacts, sealed contracts, run/source/package hashes and device logs
are not renamed. Existing device results prove their original binaries only.
Historical executor scenes retain exact program/member names, `MDDS_REMOTE_EXIT`,
`mdds_headers=INSTALLED`, remote-directory grammar, and private external fixture
paths. They replay old inputs; the renamed consumer requires an appropriately
bound new execution contract before any future device admission. This work runs
no device operation and grants no such admission.

`com.kaihong.mdds` permission/session identifiers and the `MDDS-V1-DEVICE` address
hash domain separator are protocol/platform inputs, not this project's name. Their
bytes remain unchanged. Arbitrarily changing them would alter authorization or
locator identity. Historical analyzers also retain the old type/topic names for
their frozen fixtures; they are not evidence that new type names were run on SDK.

Default NativeBackendUnavailable, P0/PF/P4 and QualificationProfile boundaries do
not change. This naming work does not add ROS integration or production capability.
