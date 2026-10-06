# Installed Runtime dual-board example

This directory is the small experimental consumer for an installed Fast DDS OpenHarmony DSoftBus Transport package.
It is intended for an explicitly authorized OpenHarmony target build. It is not a
Fake transport, a mock DSoftBus connection, or a production default path.

## Build from an installation

The consumer must use the installed package and public headers. It must not add the
repository as a subdirectory and must not include `probes/`, `tests/` or
`.artifacts/` files.

For the target build, use the approved API23 toolchain and explicitly supplied
platform DSO, identity bridge and `communication_dsoftbus` public source headers.
Keep those inputs outside this example and record their hashes in the execution
contract. The package build must bind `FASTDDS_DSOFTBUS_SOCKET_LIBRARY`; this consumer's
`CMakeLists.txt` additionally requires the explicit `FASTDDS_DSOFTBUS_TOKEN_BRIDGE` path.

```powershell
$prefix = 'C:/path/to/fastdds-dsoftbus-install'
$deps = 'C:/path/to/fastdds-dependencies'
$toolchain = 'C:/path/to/OpenHarmony/Sdk/23/native/build/cmake/ohos.toolchain.cmake'
$socketDso = 'C:/approved/target/libsoftbus_client.z.so'
$tokenBridge = 'C:/approved/target/libfastdds_dsoftbus_token_bridge.so'
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DOHOS_ARCH=arm64-v8a `
  "-DCMAKE_PREFIX_PATH=$prefix;$deps" `
  "-DFastDDSDSoftBusTransport_DIR=$prefix/lib/cmake/FastDDSDSoftBusTransport" `
  "-DFASTDDS_DSOFTBUS_SOCKET_LIBRARY=$socketDso" `
  "-DFASTDDS_DSOFTBUS_TOKEN_BRIDGE=$tokenBridge"
cmake --build build
```

The command is a target compile/link recipe. This README does not claim a Host
execution result or device execution for an invocation that has not produced its
own evidence record.

## Runtime shape

The program creates one `experimental::Runtime`, obtains one public descriptor and
uses the standard Fast DDS factory to create one Participant, Writer and Reader.
The caller owns the pump threads. After the final write and receive, delete DDS
entities and the Participant, stop and join the pumps, call `Runtime::Close`, then
release the descriptor and runtime.

The example accepts explicit route and package arguments. It contains no board
identity, token, permission JSON, claim, or private route. The caller must provide
the platform-specific authorization and loader prerequisites through the approved
deployment workflow.

The default Fast DDS DSoftBus transport remains `NativeBackendUnavailable`. The experimental
target is opt-in and is kept separate from Host/Fake test targets. A successful
example run is evidence for the exact package, configuration and platform inputs
used in that run; it does not establish SDK drain, cancellation, PF-01/PF-02,
production qualification or a `QualificationProfile`.

See [the installation guide](../../docs/INSTALL.md) and [the bounded acceptance
record](../../docs/INSTALLED_RUNTIME_ACCEPTANCE.md) for the tested package boundary.
