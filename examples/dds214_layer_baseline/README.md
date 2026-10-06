# Direct Fast DDS 2.14 layer baseline

This example is a small, installed-package consumer for the Fast DDS 2.14.5
integration layer. It is deliberately below ROS 2 and `rmw_fastrtps`: it uses
the public Fast DDS Participant, Topic, Writer, Reader and deletion APIs and
the installed `factory_configuration` target. It does not implement a second
transport, call DSoftBus APIs directly, or own a replacement pump.

The example is a Host/static integration baseline. The Host pair uses the
declared `HOST_OPAQUE_BYTES_TEST_ONLY` seam; that seam is test infrastructure
and is not evidence of OpenHarmony DSoftBus device execution. The API23 target
consumer is compile/link/static-audited only in the current evidence.

## Install and consume

Configure and build the independent Fast DDS 2.14.5 integration project first.
For Host use `integrations/ros2`; for API23 use `integrations/ros2/target` with
the OpenHarmony toolchain. Do not use the repository's default 3.6 build as
the source of this example. Install that matching project prefix, then install
this optional component:

```powershell
cmake --build <project-build> --config Release
cmake --install <project-build> --config Release --prefix <install-prefix>
cmake --install <project-build> --config Release `
  --prefix <install-prefix> --component LayerBaselineExamples
```

The package exports the existing target package. Configure the installed
example into an external consumer build directory; do not use `add_subdirectory`
on the repository and do not include `tests/`, `probes/` or `.artifacts/`
headers:

```powershell
$example = '<install-prefix>/share/FastDDSDSoftBusTransportRos2Host/examples/dds214_layer_baseline'
cmake -S $example -B <consumer-build> `
  -DCMAKE_PREFIX_PATH='<install-prefix>;<fastdds-2.14.5-prefix>;<fastcdr-2.2-prefix>'
cmake --build <consumer-build> --config Release
```

For an API23 target build, use the installed target package instead of the
Host package and the project OpenHarmony toolchain. The target package retains
`libfastrtps.so.2.14` and `libfastcdr.so.2`; the platform
`libsoftbus_client.z.so` remains an external device dependency and is not
copied into the package.

## Host pair

The maintained pair harness starts two fresh processes, creates run-scoped
directional Topics, and controls `MATCH` and `DATA_DONE` using exact per-run
records. It verifies five writes and five peer takes per side, Writer/Reader
GUID and DDS sequence identity, the public wait result, and public deletion.
Use the actual installed executable, bridge DLL, Fast DDS 2.14.5 prefix,
Fast CDR prefix, and dependency bin directory:

```powershell
python tests/dds214_layer/run_pair.py `
  --exe <consumer-build>/Release/fastdds_dsoftbus_dds214_baseline.exe `
  --bridge <install-prefix>/bin/fastdds_dsoftbus_ros2_host_bridge.dll `
  --fastdds-prefix <fastdds-2.14.5-prefix> `
  --fastcdr-prefix <fastcdr-2.2-prefix> `
  --dependency-bin <dependency-bin> `
  --output <new-output-directory> `
  --mode normal
```

`--mode delete-refusal` is a Host-only lifecycle comparison. It intentionally
calls the public Participant deletion while the public owned endpoint lists
still contain one Writer and one Reader, expects PRECONDITION=4, then continues
with data and normal public cleanup. It is not a device fault-injection grant
and is rejected by the target build. `--forward 0` and
`--config-fault missing-bridge` are expected-failure controls; they must not
produce a business release or data.

The example requires `FASTDDS_DSOFTBUS_REQUIRED_CONFIG` before Participant
creation. The config identifies the installed bridge, Fast DDS/Fast CDR modules,
run, side, domain and the explicitly named Host test backend. Missing or
invalid configuration fails closed; it must not silently fall back to UDP,
TCP, SHM or an in-process shortcut. Fast DDS built-in transports and data
sharing are disabled by the tested fixture, while the opaque Host seam remains
distinct from the SDK's DSoftBus bearer.

## Evidence boundary

The following paragraph records the pre-device-authority Host/static state at
the time this example was first documented; it predates the bounded device
window recorded below. The direct-DDS Host normal run is `71997f4878cde186`; the Host
delete-refusal run is `0af25762d43a13cf`. Both are separate Host runs and are
not ROS or device results. The API23/AArch64 package has eight statically
audited members and had not yet been loaded or executed on a board at that
earlier documentation point. The historical ROS device failures and balances
remain unchanged; the later bounded direct-DDS result is recorded separately
below and does not retroactively change this historical statement.

## Current bounded device evidence

The first bounded direct-DDS device window is complete in its stated scope:
query `a5542422e96f4508`, data `058f528038f39cc6`, with five writes and five
peer takes per board, structural native observations, public deletion returns
of zero, and normal recovery. This is the supported evidence for the direct
Fast DDS 2.14 layer on the registered pair.

The subsequent ROS 2 window is a separate result and failed. Its 20 application
writes/takes and graph/service records do not promote this direct-DDS example
to ROS acceptance, and the ROS window had no structural native summary or
real-socket record. The Host `delete-refusal` mode remains Host-only; it was not
used as a device fault injection. Production qualification, SDK drain/epoch/
cancellation, and the original ROS failure cause remain unresolved.

The offline teardown repair adds a separate Host lifecycle record. A normal ROS
Host run completes the 20/20 application checks, graph, service, and normal
close. A Host-only injected close failure records the raw DDS deletion result,
the project Close result, and the mapped public result separately. A deliberate
live-entity refusal records Topic deletion `4` before legal cleanup. These are
diagnostic and responsibility tests; they do not rewrite the failed ROS device
window or prove its missing SDK return value.
