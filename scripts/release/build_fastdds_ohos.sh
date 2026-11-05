#!/bin/bash
# Reproducible OpenHarmony (SDK 23, arm64-v8a, shared libc++) build of a Fast DDS release plus
# every dependency, all fetched from pinned upstream tags/commits. Releases are unmodified, except
# the ROS 2 Jazzy variant 2.14.5-ros, which has exactly one change: the TopicDescription key
# function (integrations/ros2/apply_topic_rtti_key.py). Unmodified rmw_fastrtps needs it on OHOS,
# otherwise its dynamic_cast to Topic returns null and Participants cannot be deleted.
#
#   scripts/release/build_fastdds_ohos.sh <2.14.5|2.14.5-ros|3.6.2|3.2.5> <work_dir>
#
# Every Fast DDS feature that applies to the boards is enabled: SECURITY (OpenSSL 3.5.9),
# FASTDDS_STATISTICS, TLS (NO_TLS=OFF), SQLITE3_SUPPORT and COMPILE_TOOLS.
# Result: <work_dir>/<version>/prefix (headers, libraries, CMake packages), plus
# <work_dir>/<version>/BUILD_INFO.txt with the pins and the SHA-256 of every installed .so.
#
# Host: Windows + Git Bash with git, perl, GNU make (scoop install make), python.
# Environment overrides: OHOS_NATIVE (SDK native dir), CMAKE, NINJA, JOBS.
set -euo pipefail
VER=${1:?usage: build_fastdds_ohos.sh <2.14.5|2.14.5-ros|3.6.2|3.2.5> <work_dir>}
WORK=${2:?usage: build_fastdds_ohos.sh <2.14.5|2.14.5-ros|3.6.2|3.2.5> <work_dir>}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OHOS_NATIVE=${OHOS_NATIVE:-C:/Users/$USERNAME/AppData/Local/OpenHarmony/Sdk/23/native}
CMAKE=${CMAKE:-$OHOS_NATIVE/build-tools/cmake/bin/cmake.exe}
NINJA=${NINJA:-$OHOS_NATIVE/build-tools/cmake/bin/ninja.exe}
JOBS=${JOBS:-16}
case "$VER" in
  2.14.5|2.14.5-ros) FASTDDS_REF=v2.14.5; FASTDDS_SHA=ebcb5c05e9da9e262041e8616bbaff61ab40a296
          FASTCDR_REF=v2.2.5;  FASTCDR_SHA=9f4e60f7ac958e0fbbfe949102de3929cbbc226f ;;
  3.6.2)  FASTDDS_REF=v3.6.2;  FASTDDS_SHA=39303846fb8534ef69fa65f9fa4bcc9e6a7c995a
          FASTCDR_REF=v2.3.7;  FASTCDR_SHA=0801699ef5232ced3ddb2b713d6c04527bcb5a6c ;;
  3.2.5)  FASTDDS_REF=v3.2.5;  FASTDDS_SHA=a060352adec20671add3e6e488b6037bd9cea09f   # ROS 2 Kilted (EOL 2026-11)
          FASTCDR_REF=v2.3.7;  FASTCDR_SHA=0801699ef5232ced3ddb2b713d6c04527bcb5a6c ;;
  *) echo "unsupported Fast DDS version $VER (2.14.5, 2.14.5-ros, 3.6.2 or 3.2.5)"; exit 2 ;;
esac
# Shared pins (identical for both versions; asio and tinyxml2 match the Fast DDS thirdparty pins).
OPENSSL_REF=openssl-3.5.9
FOONATHAN_REF=v0.7-4
TINYXML2_SHA=8c8293ba8969a46947606a93ff0cb5a083aab47a
ASIO_SHA=ed6aa8a13d51dfc6c00ae453fc9fb7df5d6ea963

TC=$OHOS_NATIVE/build/cmake/ohos.toolchain.cmake
test -f "$TC" || { echo "OHOS toolchain not found: $TC"; exit 2; }
SRC=$WORK/src; OUT=$WORK/$VER; PREFIX=$OUT/prefix
mkdir -p "$SRC" "$OUT"
log() { echo "[$(date +%H:%M:%S)] $*"; }

fetch() { # dir url ref [expected_sha]
  local d=$SRC/$1
  if [ ! -d "$d/.git" ]; then
    git init -q "$d"; git -C "$d" remote add origin "$2"
    git -C "$d" fetch -q --depth 1 origin "$3"; git -C "$d" checkout -q FETCH_HEAD
  fi
  local got; got=$(git -C "$d" rev-parse HEAD)
  if [ -n "${4:-}" ] && [ "$got" != "$4" ]; then echo "$1: $got != pinned $4"; exit 3; fi
  echo "$1 $3 $got" >> "$OUT/BUILD_INFO.txt"
}

COMMON=(-G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_TOOLCHAIN_FILE="$TC" -DOHOS_ARCH=arm64-v8a
  -DOHOS_PLATFORM=OHOS -DOHOS_STL=c++_shared -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
  -DCMAKE_C_FLAGS=-Wno-unused-command-line-argument -DCMAKE_CXX_FLAGS=-Wno-unused-command-line-argument
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX"
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$REPO/integrations/ros2/target/RosTargetBuildOptions.cmake")
cm() { # name src [args...]
  local n=$1 s=$2; shift 2
  log "configure $n"; "$CMAKE" -S "$s" -B "$OUT/build-$n" "${COMMON[@]}" "$@" > "$OUT/$n-configure.log" 2>&1 \
    || { tail -40 "$OUT/$n-configure.log"; exit 4; }
  log "build $n"; "$CMAKE" --build "$OUT/build-$n" --parallel "$JOBS" > "$OUT/$n-build.log" 2>&1 \
    || { tail -60 "$OUT/$n-build.log"; exit 4; }
  "$CMAKE" --install "$OUT/build-$n" > "$OUT/$n-install.log" 2>&1 || { tail -20 "$OUT/$n-install.log"; exit 4; }
}

: > "$OUT/BUILD_INFO.txt"
echo "fastdds_version $VER" >> "$OUT/BUILD_INFO.txt"
echo "ohos_native $OHOS_NATIVE" >> "$OUT/BUILD_INFO.txt"
fetch openssl https://github.com/openssl/openssl.git "$OPENSSL_REF"
fetch foonathan_memory https://github.com/foonathan/memory.git "$FOONATHAN_REF"
fetch tinyxml2 https://github.com/leethomason/tinyxml2.git "$TINYXML2_SHA" "$TINYXML2_SHA"
fetch asio https://github.com/chriskohlhoff/asio.git "$ASIO_SHA" "$ASIO_SHA"
fetch "fastcdr-$FASTCDR_REF" https://github.com/eProsima/Fast-CDR.git "$FASTCDR_REF" "$FASTCDR_SHA"
fetch "fastdds-$FASTDDS_REF" https://github.com/eProsima/Fast-DDS.git "$FASTDDS_REF" "$FASTDDS_SHA"

# OpenSSL 3.5.9: OHOS clang, linux-aarch64, shared, no-asm (one build per version prefix).
if [ ! -f "$PREFIX/lib/libssl.so.3" ]; then
  log "build openssl"
  B=$OUT/build-openssl; rm -rf "$B"; mkdir -p "$B"; cp -r "$SRC/openssl/." "$B/"
  LLVM=$OHOS_NATIVE/llvm/bin
  # Git for Windows perl lacks a few core modules; OpenSSL already puts util/perl on @INC.
  cp -r "$REPO/scripts/release/perl/." "$B/util/perl/"
  export PERL5LIB=util/perl # relative (no drive colon), every OpenSSL perl step runs in $B
  ( cd "$B" && perl Configure linux-aarch64 no-asm no-tests no-docs shared \
      --prefix="$PREFIX" --openssldir=/data/local/tmp/ssl --libdir=lib \
      CC="$LLVM/clang.exe --target=aarch64-linux-ohos --sysroot=$OHOS_NATIVE/sysroot" \
      AR="$LLVM/llvm-ar.exe" RANLIB="$LLVM/llvm-ranlib.exe" > "$OUT/openssl-configure.log" 2>&1 \
    && make -j"$JOBS" build_libs > "$OUT/openssl-build.log" 2>&1 \
    && make install_dev > "$OUT/openssl-install.log" 2>&1 ) || { tail -40 "$OUT"/openssl-*.log; exit 4; }
fi

[ -f "$PREFIX/lib/libfoonathan_memory-0.7.4.so" ] || cm foonathan "$SRC/foonathan_memory" \
  -DFOONATHAN_MEMORY_BUILD_EXAMPLES=OFF -DFOONATHAN_MEMORY_BUILD_TESTS=OFF -DFOONATHAN_MEMORY_BUILD_TOOLS=OFF
[ -f "$PREFIX/lib/libtinyxml2.so" ] || cm tinyxml2 "$SRC/tinyxml2" -DBUILD_TESTS=OFF -DBUILD_TESTING=OFF
[ -f "$PREFIX/lib/libfastcdr.so" ] || cm fastcdr "$SRC/fastcdr-$FASTCDR_REF" -DBUILD_TESTING=OFF
FD=$SRC/fastdds-$FASTDDS_REF
if [ "$VER" = 2.14.5-ros ]; then
  # The patch tool accepts only an isolated copy under the repository's build/ directory.
  PATCHED=$REPO/build/stage2/fastdds-2.14.5-ros-src
  if [ ! -f "$PATCHED/FDDST_TOPIC_RTTI_PATCH.json" ]; then
    test ! -e "$PATCHED" || { echo "unfinished patched source: $PATCHED"; exit 3; }
    mkdir -p "$PATCHED"; ( cd "$FD" && tar --exclude=.git -cf - . ) | ( cd "$PATCHED" && tar -xf - )
    python "$REPO/integrations/ros2/apply_topic_rtti_key.py" --source "$(cygpath -m "$PATCHED")" >> "$OUT/BUILD_INFO.txt"
  fi
  FD=$PATCHED
fi
cm fastdds "$FD" -DSECURITY=ON -DFASTDDS_STATISTICS=ON -DNO_TLS=OFF -DSQLITE3_SUPPORT=ON \
  -DCOMPILE_TOOLS=ON -DCOMPILE_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DOCUMENTATION=OFF \
  -DTHIRDPARTY=OFF -DTHIRDPARTY_UPDATE=OFF -DTINYXML2_FROM_SOURCE=OFF \
  -DTHIRDPARTY_BOOST_INCLUDE_DIR="$FD/thirdparty/boost/include" -DAsio_INCLUDE_DIR="$SRC/asio/asio/include" \
  -Dfastcdr_DIR="$PREFIX/lib/cmake/fastcdr" -DTinyXML2_DIR="$PREFIX/lib/cmake/tinyxml2" \
  -Dfoonathan_memory_DIR="$PREFIX/lib/foonathan_memory/cmake" \
  -DOPENSSL_ROOT_DIR="$PREFIX" -DOPENSSL_INCLUDE_DIR="$PREFIX/include" \
  -DOPENSSL_CRYPTO_LIBRARY="$PREFIX/lib/libcrypto.so" -DOPENSSL_SSL_LIBRARY="$PREFIX/lib/libssl.so"
grep -E "HAVE_SECURITY|HAVE_SQLITE3|FASTDDS_STATISTICS|TLS_FOUND" "$PREFIX"/include/fast*/config.h* >> "$OUT/BUILD_INFO.txt" || true
for f in "$PREFIX"/lib/*.so*; do [ -L "$f" ] || echo "sha256 $(sha256sum "$f" | cut -c1-64) $(basename "$f")"; done >> "$OUT/BUILD_INFO.txt"
log "done: $PREFIX"; cat "$OUT/BUILD_INFO.txt"
