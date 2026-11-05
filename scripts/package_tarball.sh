#!/bin/bash
# Builds dist/vanburen-<version>-linux-x86_64.tar.gz: the stripped binary plus README, LICENSE and the third-party notices.
# It is linked against the system's SDL2, GLEW, FreeType and OpenAL (the AppImage bundles them instead); build it on an old distribution for the widest compatibility.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${VERSION:-$(git describe --tags --match 'v*' --always --dirty 2>/dev/null || echo dev)}"
B=build-tarball; N="vanburen-$VERSION-linux-x86_64"
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$B" -j"$(nproc)" --target vanburen
rm -rf "$B/$N"; mkdir -p "$B/$N" dist
install -m755 "$B/vanburen" "$B/$N/vanburen"; strip "$B/$N/vanburen"
cp README.md LICENSE THIRD_PARTY_NOTICES.md "$B/$N/"
tar -C "$B" -czf "dist/$N.tar.gz" "$N"
echo "built dist/$N.tar.gz"
