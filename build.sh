#!/usr/bin/env bash
set -euo pipefail

SRC_ROOT="$(cd "$(dirname "$0")" && pwd)"
STATIC_MODE=OFF
BUILD_DIR="$SRC_ROOT/build"

if test "${1:-}" = "static" || test "${1:-}" = "--static"; then
	STATIC_MODE=ON
	BUILD_DIR="$SRC_ROOT/build-static"
	echo "=== Building in STATIC standalone mode (pure TQt3, no TDE dependency) ==="
	if test ! -f "$SRC_ROOT/libs/libtqt-mt.a" && test -f "$SRC_ROOT/libs/libtqt-mt.a.xz"; then
		echo "Decompressing libs/libtqt-mt.a.xz..."
		xz -d -k "$SRC_ROOT/libs/libtqt-mt.a.xz"
	fi
fi

mkdir -p -- "$BUILD_DIR"

# Ensure tqmoc is findable
export PATH="/opt/trinity/bin:$PATH"

cmake -S "$SRC_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DYABATMAN_STATIC_TQT="$STATIC_MODE"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Verify binaries
for bin in yabatman yabatmand; do
	BIN_PATH="$BUILD_DIR/$bin"
	if test -x "$BIN_PATH"; then
		if command -v sstrip >/dev/null 2>&1; then
			sstrip "$BIN_PATH" >/dev/null 2>&1 || true
		else
			strip --strip-all "$BIN_PATH" >/dev/null 2>&1 || true
		fi
		echo "ok: $BIN_PATH ($(stat -c%s "$BIN_PATH") bytes)"
	else
		echo "FAIL: $BIN_PATH not found" 1>&2
		exit 1
	fi
done
