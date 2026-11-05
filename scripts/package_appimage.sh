#!/bin/bash
# Builds the x86_64 AppImage into dist/ :  scripts/package_appimage.sh
#
# Needs linuxdeploy and its AppImage plugin (https://github.com/linuxdeploy). Put them in build-appimage/tools/ yourself, or run with ALLOW_DOWNLOAD=1 to fetch the
# current ones from their GitHub releases (about 10 MB). The AppImage bundles SDL2, GLEW, OpenAL Soft and their dependencies; the graphics driver, libc, FreeType and
# the sound server stay the system's (linuxdeploy's exclude list). It contains no game data: it has to be put next to the tech demo's F3.exe (or started with -game DIR, or with VB_GAME_DIR set).
# For the widest compatibility run it on an old distribution (the CI workflow uses Ubuntu 22.04): the result needs at least the glibc of the machine that built it.
set -euo pipefail
cd "$(dirname "$0")/.."
ARCH=x86_64
VERSION="${VERSION:-$(git describe --tags --match 'v*' --always --dirty 2>/dev/null || echo dev)}"
B=build-appimage; TOOLS="$B/tools"; mkdir -p "$TOOLS" dist
LD="$TOOLS/linuxdeploy-$ARCH.AppImage"; PLUGIN="$TOOLS/linuxdeploy-plugin-appimage-$ARCH.AppImage"
if [ ! -x "$LD" ] || [ ! -x "$PLUGIN" ]; then
  [ "${ALLOW_DOWNLOAD:-0}" = 1 ] || { echo "linuxdeploy and its appimage plugin are missing from $TOOLS (set ALLOW_DOWNLOAD=1 to fetch them)" >&2; exit 1; }
  curl -fsSL -o "$LD" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
  curl -fsSL -o "$PLUGIN" "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-$ARCH.AppImage"
  chmod +x "$LD" "$PLUGIN"
fi

cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$B" -j"$(nproc)" --target vanburen
rm -rf "$B/AppDir"; mkdir -p "$B/AppDir/usr/bin"
install -m755 "$B/vanburen" "$B/AppDir/usr/bin/vanburen"; strip "$B/AppDir/usr/bin/vanburen"

# (APPIMAGE_EXTRACT_AND_RUN: linuxdeploy and the plugin are AppImages themselves; this lets them run where FUSE is not available, e.g. in CI containers)
export APPIMAGE_EXTRACT_AND_RUN=1 PATH="$PWD/$TOOLS:$PATH" OUTPUT="vanburen-$VERSION-$ARCH.AppImage" LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
"$LD" --appdir "$B/AppDir" --executable "$B/AppDir/usr/bin/vanburen" \
      --desktop-file packaging/vanburen.desktop --icon-file packaging/vanburen.png --icon-filename vanburen --output appimage
mv -f "$OUTPUT" dist/
echo "built dist/$OUTPUT"
