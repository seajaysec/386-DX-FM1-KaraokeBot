#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Download JieLi's Linux toolchain (clang 4.0.1 for pi32v2) and link it as DEST/toolchain.
#   tools/get_toolchain.sh [DEST]      (default: ~/.jieli)
# The release builds use toolchain 20250324.1, pinned by URL and SHA-256 so a rebuild matches.
# JIELI_TOOLCHAIN_URL / JIELI_TOOLCHAIN_SHA256 override it (e.g. JieLi's current
# https://pkgman.jieliapp.com/s/linux-toolchain, with JIELI_TOOLCHAIN_SHA256= to skip the check).
set -e
DEST="${1:-$HOME/.jieli}"
URL="${JIELI_TOOLCHAIN_URL:-https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20250324.1.tar.xz}"
SHA="${JIELI_TOOLCHAIN_SHA256-f686586bcfb45e0f0bb27fd2b39c7a7f313cb4f0e88a66a14da621ffa8225958}"
mkdir -p "$DEST"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
curl -fL --retry 3 "$URL" -o "$TMP"
if [ -n "$SHA" ]; then
    echo "$SHA  $TMP" | shasum -a 256 -c - >/dev/null || { echo "get_toolchain.sh: SHA-256 mismatch for $URL" >&2; exit 1; }
fi
TC="$(tar -tJf "$TMP" | head -1 | cut -d/ -f1)"
tar -xJ -C "$DEST" -f "$TMP"
ln -sfn "$TC" "$DEST/toolchain"
echo "JIELI_TOOLCHAIN=$DEST/toolchain ($TC)"
