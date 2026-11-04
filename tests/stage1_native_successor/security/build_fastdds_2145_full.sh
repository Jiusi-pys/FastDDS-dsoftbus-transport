#!/bin/bash
# Rebuild Fast DDS 2.14.5 (unmodified source; same source tree the boards use) for OpenHarmony
# SDK 23 arm64 with every feature 2.14.5 offers enabled: SECURITY, FASTDDS_STATISTICS, TLS
# (follows OpenSSL), SQLITE3 and the discovery-server tool. Inputs: $SRC (verified against the
# stock-sdk23-d03 SOURCE_INVENTORY.json), $SSL (OpenSSL 3.5.9 cross-built with the OHOS clang,
# shared, linux-aarch64, no-asm), $OUT (build/install root). Run from Git Bash on the Windows host.
set -e
ROOT=C:/Users/17715/Documents/codes/FastDDS-dsoftbus-Transport
A=$ROOT/.artifacts/ros2-device-acceptance/run-20260930-local/target-environment
SDK=C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native
CMAKE=${CMAKE:-C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe}
"$CMAKE" -S "$SRC" -B "$OUT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_MAKE_PROGRAM=C:/Users/17715/scoop/shims/ninja.exe -DCMAKE_TOOLCHAIN_FILE=$SDK/build/cmake/ohos.toolchain.cmake \
  -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DOHOS_STL=c++_shared -DBUILD_SHARED_LIBS=ON \
  -DSECURITY=ON -DFASTDDS_STATISTICS=ON -DNO_TLS=OFF -DSQLITE3_SUPPORT=ON -DCOMPILE_TOOLS=ON \
  -DBUILD_TESTING=OFF -DTHIRDPARTY=OFF -DTHIRDPARTY_UPDATE=OFF -DCOMPILE_EXAMPLES=OFF -DTINYXML2_FROM_SOURCE=OFF \
  -DCMAKE_INSTALL_PREFIX="$OUT/install" -DTHIRDPARTY_BOOST_INCLUDE_DIR="$SRC/thirdparty/boost/include" \
  -DAsio_INCLUDE_DIR=$ROOT/build/ros2-target/src/asio/asio-ed6aa8a13d51dfc6c00ae453fc9fb7df5d6ea963/asio/include \
  -Dfastcdr_DIR=$A/sdk23-shared-prefix-fastcdr-225/lib/cmake/fastcdr \
  -DTinyXML2_DIR=$A/sdk23-shared-prefix-tinyxml2-6/lib/cmake/tinyxml2 \
  -Dfoonathan_memory_DIR=$A/sdk23-shared-prefix-foonathan-074/lib/foonathan_memory/cmake \
  -DOPENSSL_ROOT_DIR="$SSL" -DOPENSSL_INCLUDE_DIR="$SSL/include" \
  -DOPENSSL_CRYPTO_LIBRARY="$SSL/lib/libcrypto.so" -DOPENSSL_SSL_LIBRARY="$SSL/lib/libssl.so" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=$ROOT/build/r39-ros2-complete-matrix/candidate-d03/integrations/ros2/target/RosTargetBuildOptions.cmake
"$CMAKE" --build "$OUT/build" --parallel 16
"$CMAKE" --install "$OUT/build"
