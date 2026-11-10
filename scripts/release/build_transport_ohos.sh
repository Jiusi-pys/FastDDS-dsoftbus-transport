#!/bin/bash
# Build the DSoftBus Transport product (libfastdds_dsoftbus_stock_<214|36>.so), the cross-board
# device suite (dsb_suite) and the UDP/TCP content-filter race reproducer (cft_race) for one
# Fast DDS line, against the prefix made by build_fastdds_ohos.sh.
#
#   scripts/release/build_transport_ohos.sh <2.14.5|3.6.2> <work_dir> <platform_libs_dir>
#
# <platform_libs_dir> comes from pull_platform_libs.py (the board's libsoftbus_client.z.so closure).
# Result: <work_dir>/<version>/transport-install/{bin,lib,include,...}
set -euo pipefail
VER=${1:?usage: build_transport_ohos.sh <2.14.5|3.6.2> <work_dir> <platform_libs_dir>}
WORK=${2:?work_dir}; PLAT=${3:?platform_libs_dir}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
# CMake on Windows needs drive-letter paths (MSYS only converts standalone arguments, not lists).
winpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
REPO=$(winpath "$REPO"); WORK=$(winpath "$WORK"); PLAT=$(winpath "$PLAT")
OHOS_NATIVE=${OHOS_NATIVE:-C:/Users/$USERNAME/AppData/Local/OpenHarmony/Sdk/23/native}
CMAKE=${CMAKE:-$OHOS_NATIVE/build-tools/cmake/bin/cmake.exe}
NINJA=${NINJA:-$OHOS_NATIVE/build-tools/cmake/bin/ninja.exe}
case "$VER" in
  2.14.5) LINE=2.14; FD_PKG="-Dfastrtps_DIR=$WORK/$VER/prefix/share/fastrtps/cmake"; SRC=$WORK/src/fastdds-v2.14.5 ;;
  2.14.5-ros) LINE=2.14; FD_PKG="-Dfastrtps_DIR=$WORK/$VER/prefix/share/fastrtps/cmake"; SRC=$REPO/build/stage2/fastdds-2.14.5-ros-src
              AUTOLOAD=-DBUILD_STOCK_ROS2_AUTOLOAD=ON ;;
  3.6.2)  LINE=3.6;  FD_PKG="-Dfastdds_DIR=$WORK/$VER/prefix/share/fastdds/cmake"; SRC=$WORK/src/fastdds-v3.6.2 ;;
  3.2.5)  LINE=3.2;  FD_PKG="-Dfastdds_DIR=$WORK/$VER/prefix/share/fastdds/cmake"; SRC=$WORK/src/fastdds-v3.2.5 ;;
  *) echo "unsupported version $VER"; exit 2 ;;
esac
P=$WORK/$VER/prefix
SOCKET=$PLAT/libsoftbus_client.z.so
test -f "$SOCKET" || { echo "missing $SOCKET (run pull_platform_libs.py)"; exit 2; }
SOCKET_SHA=$(sha256sum "$SOCKET" | cut -c1-64)
B=$WORK/$VER/build-transport
"$CMAKE" -S "$REPO/integrations/ros2/stock" -B "$B" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_TOOLCHAIN_FILE="$OHOS_NATIVE/build/cmake/ohos.toolchain.cmake" -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS \
  -DOHOS_STL=c++_shared -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_C_FLAGS=-Wno-unused-command-line-argument -DCMAKE_CXX_FLAGS=-Wno-unused-command-line-argument \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$REPO/integrations/ros2/target/RosTargetBuildOptions.cmake" \
  -DFASTDDS_DSOFTBUS_STOCK_FASTDDS=$LINE $FD_PKG -Dfastcdr_DIR="$P/lib/cmake/fastcdr" \
  -Dfoonathan_memory_DIR="$P/lib/foonathan_memory/cmake" -DTinyXML2_DIR="$P/lib/cmake/tinyxml2" \
  -DOPENSSL_ROOT_DIR="$P" -DOPENSSL_INCLUDE_DIR="$P/include" -DOPENSSL_CRYPTO_LIBRARY="$P/lib/libcrypto.so" \
  -DOPENSSL_SSL_LIBRARY="$P/lib/libssl.so" -DBUILD_STOCK_TRANSPORT_OHOS_PRODUCT=ON -DBUILD_STOCK_DEVICE_SUITE=ON \
  -DDSOFTBUS_SOURCE_DIR="$REPO/communication_dsoftbus" -DFASTDDS_DSOFTBUS_FASTDDS_SOURCE_DIR="$SRC" \
  -DFASTDDS_DSOFTBUS_SOCKET_LIBRARY="$SOCKET" -DFASTDDS_DSOFTBUS_SOCKET_SHA256="$SOCKET_SHA" \
  "-DFASTDDS_DSOFTBUS_PLATFORM_LINK_DIRECTORIES=$PLAT;$P/lib" \
  ${AUTOLOAD:-} -DCMAKE_INSTALL_PREFIX="$WORK/$VER/transport-install"
"$CMAKE" --build "$B" --parallel "${JOBS:-16}"
"$CMAKE" --install "$B"
echo "installed: $WORK/$VER/transport-install"
ls -l "$WORK/$VER/transport-install/bin" "$WORK/$VER/transport-install/lib" | grep -E "dsb_suite|cft_race|stock"
