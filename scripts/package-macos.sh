#!/bin/bash
# Builds the standalone macOS package of mod-hw-emulator (universal: Intel and Apple Silicon): a whole MOD Desktop
# app with mod-host, mod-ui, the launcher of mod-hw-emulator and the emulator in it, and the Dwarf and
# Duo X firmwares next to it. Not signed with a Developer ID: ad-hoc signatures only.
#
#   scripts/package-macos.sh MOD_DESKTOP MOD_HOST [OUT]
#
# MOD_DESKTOP is a mod-desktop checkout built for macos-universal-10.15 on macOS as mod-desktop's CI does it, with
# the mod-ui of mod-hw-emulator in place of its own (sejerpz/mod-ui, branch feature/mod-hw-emulator):
#
#   rm -rf src/mod-ui && git clone -b feature/mod-hw-emulator https://github.com/sejerpz/mod-ui.git src/mod-ui
#   src/PawPaw/.github/workflows/bootstrap-deps.sh macos-universal-10.15
#   utils/apply-patches.sh
#   src/PawPaw/bootstrap-mod.sh macos-universal-10.15
#   make PAWPAW_TARGET=macos-universal-10.15
#
# MOD_HOST is a mod-host checkout on branch feature/mod-hw-emulator. OUT is the zip,
# mod-hw-emulator-desktop-macos.zip by default. Unpack it anywhere and run ./mhe-desktop.sh.

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
MOD_DESKTOP=$(cd "$1" && pwd)
MOD_HOST=$(cd "$2" && pwd)
OUT=${3:-mod-hw-emulator-desktop-macos.zip}
case "$OUT" in /*) ;; *) OUT=$PWD/$OUT ;; esac
TARGET=macos-universal-10.15
RUN="$MOD_DESKTOP/utils/run.sh $TARGET"
PREFIX=$HOME/PawPawBuilds/targets/$TARGET
DEVICES="dwarf duox"
ARCHS="x86_64 arm64"
MIN_MACOS=10.15

# the mod-ui MOD Desktop was built with: the one of mod-hw-emulator has the HMI line over TCP
if ! grep -q "tcp://" "$MOD_DESKTOP/src/mod-ui/mod/hmi.py"; then
    echo "$MOD_DESKTOP is built with its own mod-ui: put sejerpz/mod-ui feature/mod-hw-emulator in src/mod-ui" >&2
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
PKG=$STAGE/mod-hw-emulator
APP="$PKG/MOD Desktop.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
mkdir -p "$PKG/skins" "$PKG/devices"

# MOD Desktop as it is built, symlinks resolved, as mod-desktop's utils/macos/macos-pkg.sh copies it
cp -RL "$MOD_DESKTOP/build/mod-desktop.app" "$APP"

# mod-host, built as MOD Desktop builds its own plus the HMI control
cp -a "$MOD_HOST" "$STAGE/mod-host"
if [ -f "$MOD_DESKTOP/patches/mod-host.patch" ] && patch -s -p1 --dry-run -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch" > /dev/null 2>&1; then
    patch -s -p1 -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch"
fi
$RUN make -C "$STAGE/mod-host" clean > /dev/null
$RUN make -C "$STAGE/mod-host" MOD_DESKTOP=1 MOD_HMI_EMULATOR=1 SKIP_READLINE=1 SKIP_FFTW335=1 mod-host.so
cp "$STAGE/mod-host/mod-host.so" "$MACOS/jack/mod-host.so"

# MOD Desktop's launcher with the button that starts the emulator and the data folder of the package
# (patches/mod-desktop); its Makefile points it at the Qt frameworks of the bundle
mkdir -p "$STAGE/launcher/src"
cp "$MOD_DESKTOP/VERSION" "$STAGE/launcher/"
cp -a "$MOD_DESKTOP/res" "$STAGE/launcher/"
cp -a "$MOD_DESKTOP/src/systray" "$STAGE/launcher/src/"
rm -f "$STAGE"/launcher/src/systray/*.o "$STAGE"/launcher/src/systray/mod-desktop
for p in "$EMU"/patches/mod-desktop/*.patch; do
    patch -s -p1 -d "$STAGE/launcher" < "$p"
done
$RUN make -C "$STAGE/launcher/src/systray"
cp "$STAGE/launcher/src/systray/mod-desktop" "$MACOS/mod-desktop"

# what macos-pkg.sh does to the bundle: the frameworks flattened, the Qt and jack libraries found inside it
(
    cd "$APP/Contents"
    rm -rf Frameworks/*/*.prl Frameworks/*/Headers Frameworks/*/Versions MacOS/data
    for f in Frameworks/*/Qt* PlugIns/*/libq*.dylib; do
        for q in Core Gui OpenGL PrintSupport Svg Widgets; do
            install_name_tool -change "@rpath/Qt$q.framework/Versions/5/Qt$q" "@executable_path/../Frameworks/Qt$q.framework/Qt$q" "$f"
        done
    done
    for f in MacOS/lib/libmod_utils.so MacOS/libjack*.dylib; do
        [ -e "$f" ] || continue
        install_name_tool -change "$PREFIX/lib/libjack.0.1.0.dylib" "@executable_path/libjack.0.dylib" "$f"
        install_name_tool -change "$PREFIX/lib/libjackserver.0.1.0.dylib" "@executable_path/libjackserver.0.dylib" "$f"
    done
    for f in MacOS/jackd MacOS/jack/*.so; do
        install_name_tool -change "$PREFIX/lib/libjack.0.1.0.dylib" "@executable_path/libjackserver.0.dylib" "$f"
        install_name_tool -change "$PREFIX/lib/libjackserver.0.1.0.dylib" "@executable_path/libjackserver.0.dylib" "$f"
    done
)

# the emulator and the firmwares, once per architecture (the Makefile builds one at a time) and joined: the
# programs in the bundle, with its Qt, the firmwares next to it
for a in $ARCHS; do
    flags="-arch $a -mmacosx-version-min=$MIN_MACOS"
    $RUN make -C "$EMU" BUILD=build-macos-$a ARCH_FLAGS="$flags" all gui
    for d in $DEVICES; do
        $RUN make -C "$EMU" BUILD=build-macos-$a ARCH_FLAGS="$flags" DEVICE=$d firmware
    done
done
join() {
    lipo -create $(for a in $ARCHS; do echo "$EMU/build-macos-$a/$1"; done) -output "$2"
}
join mod-hw-emulator "$MACOS/mod-hw-emulator"
join mhe-run "$MACOS/mhe-run"
for d in $DEVICES; do
    join "fw-$d.dylib" "$PKG/fw-$d.dylib"
done
for q in Core Gui Widgets; do
    install_name_tool -change "@rpath/Qt$q.framework/Versions/5/Qt$q" "@executable_path/../Frameworks/Qt$q.framework/Qt$q" "$MACOS/mod-hw-emulator"
done
otool -L "$MACOS/mod-hw-emulator" | grep -i qt

# MOD Desktop's descriptor keeps what tells the desktop apart (name, binaries); the device's one gives the rest
for d in $DEVICES; do
    cp -R "$EMU/devices/$d/skin" "$PKG/skins/$d"
    mkdir -p "$PKG/devices/$d"
    cp "$EMU/devices/$d/mod-hardware-descriptor.json" "$PKG/devices/$d/"
    python3 - "$RES/mod-hardware-descriptor.json" "$EMU/devices/$d/mod-hardware-descriptor.json" \
              "$PKG/devices/$d/mod-desktop-descriptor.json" << 'EOF'
import json, sys
desktop, device = (json.load(open(p)) for p in sys.argv[1:3])
for key in ('name', 'architecture', 'bin-compat'):
    if key in desktop:
        device[key] = desktop[key]
json.dump(device, open(sys.argv[3], 'w'), indent=2)
EOF
done
cp "$RES/mod-hardware-descriptor.json" "$PKG/devices/mod-desktop.json"
# mhe-desktop.sh writes the descriptor of the device here at each start: a link out of the sealed bundle
rm "$RES/mod-hardware-descriptor.json"
ln -s ../../../devices/current.json "$RES/mod-hardware-descriptor.json"
cp "$PKG/devices/mod-desktop.json" "$PKG/devices/current.json"

cp "$EMU/scripts/mhe-desktop.sh" "$PKG/"
chmod +x "$PKG/mhe-desktop.sh"
cp "$EMU/LICENSE" "$PKG/"
cp "$MOD_DESKTOP/LICENSE" "$PKG/LICENSE-mod-desktop.txt"

# ad-hoc signatures: install_name_tool breaks the ones the linker made, and Apple Silicon runs no unsigned code
find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" "$PKG"/fw-*.dylib -type f | while read -r f; do
    if file -b "$f" | grep -q Mach-O; then
        codesign --force -s - "$f"
    fi
done
codesign --force --deep -s - "$APP" || echo "the bundle as a whole is not signed: its programs are" >&2

DWARF=$(cd "$EMU" && cd "${FW_DWARF:-../mod-dwarf-controller}" && pwd)
DUOX=$(cd "$EMU" && cd "${FW_DUOX:-../mod-duox-controller}" && pwd)
rev() { git -C "$1" rev-parse HEAD 2> /dev/null || echo "${2:-unknown}"; }
describe() { git -C "$1" describe --tags --always --dirty 2> /dev/null || echo unknown; }
{
    echo "mod-hw-emulator with MOD Desktop, for macOS (universal: Intel and Apple Silicon, $MIN_MACOS or later)"
    echo
    echo "Built from these sources; each is free software under its own license (GPL/AGPL: LICENSE*):"
    echo
    echo "mod-hw-emulator       https://github.com/sejerpz/mod-hw-emulator      $(rev "$EMU" "$MHE_REV")"
    echo "mod-desktop           https://github.com/mod-audio/mod-desktop         $(rev "$MOD_DESKTOP") ($(describe "$MOD_DESKTOP"), VERSION $(cat "$MOD_DESKTOP/VERSION"))"
    git -C "$MOD_DESKTOP" submodule status --recursive 2> /dev/null | awk '$2 != "src/mod-ui" { printf "  %-40s %s\n", $2, $1 }'
    echo "mod-ui                https://github.com/sejerpz/mod-ui                $(rev "$MOD_DESKTOP/src/mod-ui" "$MOD_UI_REV") (branch feature/mod-hw-emulator, replaces mod-desktop's)"
    echo "mod-host             https://github.com/sejerpz/mod-host              $(rev "$MOD_HOST" "$MOD_HOST_REV") (branch feature/mod-hw-emulator, replaces mod-desktop's)"
    echo "mod-system-control    https://github.com/sejerpz/mod-system-control    ${MSC_REV:-$(rev "$EMU/../mod-system-control")} (branch feature/hmi-widgets)"
    echo "Dwarf firmware        https://github.com/sejerpz/mod-dwarf-controller   $(rev "$DWARF") ($(describe "$DWARF"), branch feature/mod-hw-emulator)"
    echo "Duo X firmware        https://github.com/sejerpz/mod-duox-controller   $(rev "$DUOX") ($(describe "$DUOX"), fork of mod-audio/mod-duox-controller)"
    echo
    echo "with patches/mod-desktop of mod-hw-emulator applied to the launcher."
    echo "Not an official MOD Audio build: the Tone3000 integration has no client id and does not work."
    echo "Not signed with a Developer ID: macOS asks before opening it the first time, see README.txt."
} > "$PKG/SOURCES.txt"
cat > "$PKG/README.txt" << 'EOF'
mod-hw-emulator with MOD Desktop for macOS

This package is not signed with an Apple Developer ID. macOS marks what is downloaded from the internet and then
refuses to run it ("damaged" or "cannot be verified"). Once, after unpacking, remove that mark:

    xattr -dr com.apple.quarantine mod-hw-emulator

then start it from a terminal:

    mod-hw-emulator/mhe-desktop.sh                  # --device duox for the Duo X

Its data (pedalboards, user files) is in mod-hw-emulator/data, apart from an installed MOD Desktop
(--shared-data uses that one's, ~/Documents/MOD Desktop).
EOF
# plugins of MOD Desktop that did not build for macOS (missing-plugins.txt, written by the release workflow)
if [ -s "$MOD_DESKTOP/missing-plugins.txt" ]; then
    {
        echo
        echo "These plugins of MOD Desktop do not build for macOS yet and are not in this package:"
        echo
        sed 's/^/    /' "$MOD_DESKTOP/missing-plugins.txt"
    } >> "$PKG/README.txt"
fi

rm -f "$OUT"
(cd "$STAGE" && ditto -c -k --keepParent mod-hw-emulator "$OUT")
echo "$OUT"
