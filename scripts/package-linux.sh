#!/bin/bash
# Builds the standalone Linux package of mod-hw-emulator: a whole MOD Desktop with mod-host, mod-ui and the
# launcher of mod-hw-emulator in it, and the emulator with the Dwarf and the Duo X next to it.
#
#   scripts/package-linux.sh MOD_DESKTOP MOD_HOST [OUT]
#
# MOD_DESKTOP is a mod-desktop checkout built for linux-x86_64 as mod-desktop's CI does it (ubuntu:22.04), with
# the mod-ui of mod-hw-emulator in place of its own: sejerpz/mod-ui, branch feature/mod-hw-emulator (HMI widgets,
# the HMI line over TCP, hardware addressing and the Display dialog with a controller attached):
#
#   rm -rf src/mod-ui && git clone -b feature/mod-hw-emulator https://github.com/sejerpz/mod-ui.git src/mod-ui
#   src/PawPaw/.github/workflows/bootstrap-deps.sh linux-x86_64
#   utils/apply-patches.sh
#   src/PawPaw/bootstrap-mod.sh linux-x86_64
#   make
#
# MOD_HOST is a mod-host checkout on branch feature/mod-hw-emulator (sys_serial_tcp.h). OUT is the tarball,
# mod-hw-emulator-desktop-linux-x86_64.tar.xz by default. The revisions of trees that are not git checkouts come
# from MHE_REV, MOD_HOST_REV and MSC_REV (SOURCES.txt). Unpack it anywhere and run ./mhe-desktop.sh.

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
MOD_DESKTOP=$(cd "$1" && pwd)
MOD_HOST=$(cd "$2" && pwd)
ARCH=$(uname -m)
OUT=$(realpath -m "${3:-mod-hw-emulator-desktop-linux-$ARCH.tar.xz}")
TARGET=linux-$ARCH
RUN="$MOD_DESKTOP/utils/run.sh $TARGET"
DEVICES="dwarf duox duo"

# the mod-ui MOD Desktop was built with: the one of mod-hw-emulator has the HMI line over TCP
if ! grep -q "tcp://" "$MOD_DESKTOP/src/mod-ui/mod/hmi.py"; then
    echo "$MOD_DESKTOP is built with its own mod-ui: put sejerpz/mod-ui feature/mod-hw-emulator in src/mod-ui" >&2
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
PKG=$STAGE/mod-hw-emulator
MD=$PKG/mod-desktop
mkdir -p "$PKG/skins" "$PKG/devices"

# MOD Desktop as it is built, symlinks resolved, as mod-desktop's utils/linux/linux-tar.sh packs it
cp -aL "$MOD_DESKTOP/build" "$MD"

# mod-host, built as MOD Desktop builds its own plus the HMI control
cp -a "$MOD_HOST" "$STAGE/mod-host"
if [ -f "$MOD_DESKTOP/patches/mod-host.patch" ] && patch -s -p1 --dry-run -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch" > /dev/null 2>&1; then
    patch -s -p1 -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch"
fi
$RUN make -C "$STAGE/mod-host" clean > /dev/null
$RUN make -C "$STAGE/mod-host" MOD_DESKTOP=1 MOD_HMI_EMULATOR=1 SKIP_READLINE=1 SKIP_FFTW335=1 mod-host.so
cp "$STAGE/mod-host/mod-host.so" "$MD/jack/mod-host.so"

# MOD Desktop's launcher with the button that starts the emulator and the data folder of the package
# (patches/mod-desktop); its sources expect the checkout around them (VERSION, res/)
mkdir -p "$STAGE/launcher/src"
cp "$MOD_DESKTOP/VERSION" "$STAGE/launcher/"
cp -a "$MOD_DESKTOP/res" "$STAGE/launcher/"
cp -a "$MOD_DESKTOP/src/systray" "$STAGE/launcher/src/"
rm -f "$STAGE"/launcher/src/systray/*.o "$STAGE"/launcher/src/systray/mod-desktop
for p in "$EMU"/patches/mod-desktop/*.patch; do
    patch -s -p1 -d "$STAGE/launcher" < "$p"
done
$RUN make -C "$STAGE/launcher/src/systray"
cp "$STAGE/launcher/src/systray/mod-desktop" "$MD/mod-desktop"

# the emulator, with the Qt of MOD Desktop, and the firmware of each device
$RUN make -C "$EMU" BUILD=build-$TARGET all gui
for d in $DEVICES; do
    $RUN make -C "$EMU" BUILD=build-$TARGET DEVICE=$d firmware
    cp -L "$EMU/build-$TARGET/fw-$d.so" "$PKG/"
done
cp "$EMU/build-$TARGET/mod-hw-emulator" "$EMU/build-$TARGET/mhe-run" "$PKG/"

# MOD Desktop's descriptor keeps what tells the desktop apart (name, binaries); the device's one gives the rest
for d in $DEVICES; do
    cp -r "$EMU/devices/$d/skin" "$PKG/skins/$d"
    mkdir -p "$PKG/devices/$d"
    cp "$EMU/devices/$d/mod-hardware-descriptor.json" "$PKG/devices/$d/"
    python3 - "$MD/mod-hardware-descriptor.json" "$EMU/devices/$d/mod-hardware-descriptor.json" \
              "$PKG/devices/$d/mod-desktop-descriptor.json" << 'EOF'
import json, sys
desktop, device = (json.load(open(p)) for p in sys.argv[1:3])
for key in ('name', 'architecture', 'bin-compat'):
    if key in desktop:
        device[key] = desktop[key]
json.dump(device, open(sys.argv[3], 'w'), indent=2)
EOF
done
cp "$MD/mod-hardware-descriptor.json" "$PKG/devices/mod-desktop.json"

cp "$EMU/scripts/mhe-desktop.sh" "$PKG/"
chmod +x "$PKG/mhe-desktop.sh"
cp "$EMU/LICENSE" "$PKG/"
cp "$MOD_DESKTOP/LICENSE" "$PKG/LICENSE-mod-desktop.txt"

DWARF=$(cd "$EMU" && realpath -m "${FW_DWARF:-deps/mod-dwarf-controller}")
DUOX=$(cd "$EMU" && realpath -m "${FW_DUOX:-deps/mod-duox-controller}")
DUO=$(cd "$EMU" && realpath -m "${FW_DUO:-deps/mod-duo-controller}")
rev() { git -C "$1" rev-parse HEAD 2> /dev/null || echo "${2:-unknown}"; }
describe() { git -C "$1" describe --tags --always --dirty 2> /dev/null || echo unknown; }
{
    echo "mod-hw-emulator with MOD Desktop, for Linux ($ARCH)"
    echo
    echo "Built from these sources; each is free software under its own license (GPL/AGPL: LICENSE*):"
    echo
    echo "mod-hw-emulator       https://github.com/sejerpz/mod-hw-emulator      $(rev "$EMU" "$MHE_REV")"
    echo "mod-desktop           https://github.com/sejerpz/mod-desktop           $(rev "$MOD_DESKTOP") ($(describe "$MOD_DESKTOP"), VERSION $(cat "$MOD_DESKTOP/VERSION"))"
    git -C "$MOD_DESKTOP" submodule status --recursive 2> /dev/null | awk '$2 != "src/mod-ui" { printf "  %-40s %s\n", $2, $1 }'
    echo "mod-ui                https://github.com/sejerpz/mod-ui                $(rev "$MOD_DESKTOP/src/mod-ui" "$MOD_UI_REV") (branch feature/mod-hw-emulator, the src/mod-ui of sejerpz/mod-desktop)"
    echo "mod-host              https://github.com/sejerpz/mod-host              $(rev "$MOD_HOST" "$MOD_HOST_REV") (branch feature/mod-hw-emulator, replaces the src/mod-host of mod-desktop)"
    echo "mod-system-control    https://github.com/sejerpz/mod-system-control    ${MSC_REV:-$(rev "$EMU/deps/mod-system-control")} (branch feature/hmi-widgets)"
    echo "Dwarf firmware        https://github.com/sejerpz/mod-dwarf-controller   $(rev "$DWARF") ($(describe "$DWARF"), branch feature/mod-hw-emulator)"
    echo "Duo X firmware        https://github.com/sejerpz/mod-duox-controller   $(rev "$DUOX") ($(describe "$DUOX"), fork of mod-audio/mod-duox-controller)"
    echo "Duo firmware          https://github.com/sejerpz/mod-duo-controller    $(rev "$DUO") ($(describe "$DUO"), fork of mod-audio/mod-duo-controller)"
    echo
    echo "with patches/mod-desktop of mod-hw-emulator applied to the launcher."
    echo "Not an official MOD Audio build: the Tone3000 integration has no client id and does not work."
} > "$PKG/SOURCES.txt"

rm -f "$OUT"
tar cJf "$OUT" -C "$STAGE" mod-hw-emulator
echo "$OUT"
