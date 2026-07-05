#!/usr/bin/env bash
set -euo pipefail

NO_SUDO=0
for arg in "$@"; do
    case "$arg" in
        --no-sudo) NO_SUDO=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

FOLLY_VERSION="v2026.06.29.00"
FASTFLOAT_VERSION="v8.2.9"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FOLLY_SRC="$ROOT_DIR/third_party/folly-src"
FOLLY_PREFIX="$ROOT_DIR/third_party/folly-install"
FASTFLOAT_SRC="$ROOT_DIR/third_party/fast_float-src"
JOBS="${JOBS:-$(nproc)}"


if [ "$NO_SUDO" -eq 1 ]; then
    echo "== [1/4] --no-sudo: skipping apt-get, assuming these are already installed: =="
    echo "  cmake ninja-build pkg-config git g++"
    echo "  libboost-context-dev libboost-filesystem-dev libboost-program-options-dev"
    echo "  libboost-regex-dev libboost-system-dev libboost-thread-dev"
    echo "  libdouble-conversion-dev libevent-dev libgflags-dev libgoogle-glog-dev"
    echo "  libicu-dev liblz4-dev liblzma-dev libsnappy-dev libssl-dev zlib1g-dev libfmt-dev"
else
    echo "== [1/4] Installing system dependencies (requires sudo) =="
    sudo apt-get update
    sudo apt-get install -y \
        cmake ninja-build pkg-config git g++ \
        libboost-context-dev libboost-filesystem-dev libboost-program-options-dev \
        libboost-regex-dev libboost-system-dev libboost-thread-dev \
        libdouble-conversion-dev libevent-dev libgflags-dev libgoogle-glog-dev \
        libicu-dev liblz4-dev liblzma-dev libsnappy-dev libssl-dev zlib1g-dev libfmt-dev
fi

echo "== [2/5] Fetching folly $FOLLY_VERSION =="
mkdir -p "$ROOT_DIR/third_party"
if [ ! -d "$FOLLY_SRC" ]; then
    git clone --branch "$FOLLY_VERSION" --depth 1 \
        https://github.com/facebook/folly.git "$FOLLY_SRC"
else
    echo "  $FOLLY_SRC already exists, skipping clone"
fi

FASTFLOAT_CMAKE_ARG=()
if command -v pkg-config >/dev/null 2>&1 && \
   find /usr/include /usr/local/include -path "*/fast_float/fast_float.h" 2>/dev/null | grep -q .; then
    echo "== [3/5] fast_float already present system-wide, not vendoring =="
else
    echo "== [3/5] Vendoring fast_float $FASTFLOAT_VERSION (header-only, no system package needed) =="
    if [ ! -d "$FASTFLOAT_SRC" ]; then
        git clone --branch "$FASTFLOAT_VERSION" --depth 1 \
            https://github.com/fastfloat/fast_float.git "$FASTFLOAT_SRC"
    else
        echo "  $FASTFLOAT_SRC already exists, skipping clone"
    fi
    FASTFLOAT_CMAKE_ARG=(-DFASTFLOAT_INCLUDE_DIR="$FASTFLOAT_SRC/include")
fi

echo "== [4/5] Building folly (this takes a while) =="
cmake -S "$FOLLY_SRC" -B "$FOLLY_SRC/_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DCMAKE_INSTALL_PREFIX="$FOLLY_PREFIX" \
    "${FASTFLOAT_CMAKE_ARG[@]}"
cmake --build "$FOLLY_SRC/_build" -j"$JOBS"

echo "== [5/5] Installing to $FOLLY_PREFIX =="
cmake --install "$FOLLY_SRC/_build"

