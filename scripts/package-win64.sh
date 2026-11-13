#!/bin/bash
# Builds the Windows package of mod-hw-emulator: the emulator with its user interface, and the files mhe-desktop.cmd
# lays over a MOD Desktop installation.
#
#   scripts/package-win64.sh [--standalone] MOD_DESKTOP MOD_HOST [OUT]
#
# --standalone puts the whole MOD Desktop of MOD_DESKTOP/build in the package, with the overlay already laid over it
# (mod-desktop/ in place of mod-desktop-overlay/), for where there is no MOD Desktop release to install: the zip
# runs as it is, keeps its data in data/, and SOURCES.txt says what it was built from. MOD Desktop is then built
# with the mod-ui of mod-hw-emulator in src/mod-ui (sejerpz/mod-ui, branch devel: HMI widgets,
# the HMI line over TCP, hardware addressing and the Display dialog with a controller attached), not patched. The
# revisions of trees that are not git checkouts come from MHE_REV, MOD_HOST_REV, MSC_REV and MOD_UI_REV.
#
# MOD_DESKTOP is a mod-desktop checkout at the release the package is for (git checkout 0.0.13, say) whose PawPaw
# dependencies are built for win64, as mod-desktop's CI does it (ubuntu:22.04):
#
#   utils/apply-patches.sh
#   src/PawPaw/.github/workflows/bootstrap-deps.sh win64
#   src/PawPaw/bootstrap-mod.sh win64
#
# so mod-host links the same jack and lilv as the MOD Desktop it goes in. MOD_HOST is a mod-host checkout on
# branch devel (sys_serial_tcp.h); mod-ui's files are the ones of MOD_DESKTOP with
# patches/mod-ui/*.patch. OUT is the zip, mod-hw-emulator-win64.zip by default.
#
# mod-desktop-overlay/ in the package holds the files at their place in the installation, and base-sha256.txt
# the hash of the mod-ui files they replace, so mhe-desktop can tell a MOD Desktop they were not made for.

set -e

STANDALONE=false
if [ "$1" = --standalone ]; then
    STANDALONE=true
    shift
fi

EMU=$(cd "$(dirname "$0")/.." && pwd)
MOD_DESKTOP=$(cd "$1" && pwd)
MOD_HOST=$(cd "$2" && pwd)
OUT=$(realpath -m "${3:-$($STANDALONE && echo mod-hw-emulator-desktop-win64.zip || echo mod-hw-emulator-win64.zip)}")
RUN="$MOD_DESKTOP/utils/run.sh win64"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
PKG=$STAGE/mod-hw-emulator
OVL=$PKG/mod-desktop-overlay
mkdir -p "$OVL/jack" "$PKG/skins" "$PKG/platforms"

# mod-host, built as MOD Desktop builds its own plus the HMI control
cp -a "$MOD_HOST" "$STAGE/mod-host"
# the fixes MOD Desktop applies to its own mod-host, when they apply to this one
if [ -f "$MOD_DESKTOP/patches/mod-host.patch" ] && patch -s -p1 --dry-run -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch" > /dev/null 2>&1; then
    patch -s -p1 -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch"
fi
$RUN make -C "$STAGE/mod-host" clean > /dev/null
$RUN make -C "$STAGE/mod-host" MOD_DESKTOP=1 MOD_HMI_EMULATOR=1 SKIP_READLINE=1 SKIP_FFTW335=1 WINDOWS=true mod-host.so
cp "$STAGE/mod-host/mod-host.so" "$OVL/jack/mod-host.dll"

if $STANDALONE; then
    # MOD Desktop is built with the mod-ui of mod-hw-emulator (sejerpz/mod-ui devel), which
    # has the HMI line over TCP
    if ! grep -q "tcp://" "$MOD_DESKTOP/src/mod-ui/mod/hmi.py"; then
        echo "$MOD_DESKTOP is built with its own mod-ui: put sejerpz/mod-ui devel in src/mod-ui" >&2
        exit 1
    fi
else
    # mod-ui of the installed MOD Desktop with the patches: the HMI line over TCP, hardware addressing with
    # actuators
    cp -a "$MOD_DESKTOP/src/mod-ui" "$STAGE/mod-ui"
    for p in "$EMU"/patches/mod-ui/*.patch; do
        patch -s -p1 -d "$STAGE/mod-ui" < "$p"
    done
    : > "$OVL/base-sha256.txt"
    for f in $(cd "$STAGE/mod-ui" && cat "$EMU"/patches/mod-ui/*.patch | awk '/^\+\+\+ b\// { print substr($2, 3) }' | sort -u); do
        mkdir -p "$OVL/$(dirname "$f")"
        cp "$STAGE/mod-ui/$f" "$OVL/$f"
        echo "$(sha256sum "$MOD_DESKTOP/src/mod-ui/$f" | cut -d' ' -f1 | tr a-f A-F) $f" >> "$OVL/base-sha256.txt"
    done
fi

# MOD Desktop's launcher with the button that starts the emulator (patches/mod-desktop); its sources
# expect the checkout around them (VERSION, res/)
mkdir -p "$STAGE/launcher/src"
cp "$MOD_DESKTOP/VERSION" "$STAGE/launcher/"
cp -a "$MOD_DESKTOP/res" "$STAGE/launcher/"
cp -a "$MOD_DESKTOP/src/systray" "$STAGE/launcher/src/"
rm -f "$STAGE"/launcher/src/systray/*.o "$STAGE"/launcher/src/systray/mod-desktop.exe
for p in "$EMU"/patches/mod-desktop/*.patch; do
    patch -s -p1 -d "$STAGE/launcher" < "$p"
done
$RUN make -C "$STAGE/launcher/src/systray"
cp "$STAGE/launcher/src/systray/mod-desktop.exe" "$OVL/"

# the emulator, with the Qt of MOD Desktop, and the firmware of each device
DEVICES="dwarf duox duo"
$RUN make -C "$EMU" BUILD=build-win64 all gui
for d in $DEVICES; do
    $RUN make -C "$EMU" BUILD=build-win64 DEVICE=$d firmware
    cp "$EMU/build-win64/fw-$d.dll" "$PKG/"
done
cp "$EMU"/build-win64/{mod-hw-emulator.exe,mhe-run.exe} "$PKG/"

PREFIX=$($RUN sh -c 'echo $PAWPAW_PREFIX')
for dll in Qt5Core Qt5Gui Qt5Widgets; do
    cp "$PREFIX/bin/$dll.dll" "$PKG/" 2> /dev/null || cp "$MOD_DESKTOP/build/$dll.dll" "$PKG/"
done
cp "$PREFIX"/plugins/platforms/qwindows.dll "$PKG/platforms/" 2> /dev/null \
    || cp "$MOD_DESKTOP"/build/platforms/qwindows.dll "$PKG/platforms/"
# whatever else the programs need from the toolchain (winpthread, the GCC runtimes when not static)
for dll in $(x86_64-w64-mingw32-objdump -p "$PKG"/*.exe "$PKG"/*.dll "$PKG"/platforms/*.dll "$OVL"/jack/*.dll \
             | awk '/DLL Name/ { print $3 }' | sort -u); do
    [ -e "$PKG/$dll" ] && continue
    for dir in "$PREFIX/bin" "$PREFIX/lib" /usr/x86_64-w64-mingw32/lib /usr/lib/gcc/x86_64-w64-mingw32/*-posix; do
        if [ -e "$dir/$dll" ]; then
            cp "$dir/$dll" "$PKG/"
            break
        fi
    done
done

for d in $DEVICES; do
    cp -r "$EMU/devices/$d/skin" "$PKG/skins/$d"
    mkdir -p "$PKG/devices/$d"
    # with its drop-ins (the HMI widget areas): mhe-desktop.ps1 writes MOD Desktop's descriptor from it
    python3 "$EMU/scripts/merge-descriptor.py" "$EMU/devices/$d/mod-hardware-descriptor.json" \
            "$PKG/devices/$d/mod-hardware-descriptor.json"
done
cp "$EMU/scripts/mhe-desktop.ps1" "$PKG/"
sed 's/$/\r/' "$EMU/scripts/mhe-desktop.cmd" > "$PKG/mhe-desktop.cmd"
cp "$EMU/LICENSE" "$PKG/"

if $STANDALONE; then
    # MOD Desktop as it is built (symlinks resolved) with the overlay over it; mhe-desktop writes the
    # descriptor of the device at each start, from MOD Desktop's own one kept in devices/
    cp -rL "$MOD_DESKTOP/build" "$PKG/mod-desktop"
    cp "$PKG/mod-desktop/mod-hardware-descriptor.json" "$PKG/devices/mod-desktop.json"
    (cd "$OVL" && find . -type f ! -name base-sha256.txt) | while read -r f; do
        mkdir -p "$PKG/mod-desktop/$(dirname "$f")"
        cp "$OVL/$f" "$PKG/mod-desktop/$f"
    done
    rm -rf "$OVL"
    cp "$MOD_DESKTOP/LICENSE" "$PKG/LICENSE-mod-desktop.txt"

    DWARF=$(cd "$EMU" && realpath -m "${FW_DWARF:-deps/mod-dwarf-controller}")
    DUOX=$(cd "$EMU" && realpath -m "${FW_DUOX:-deps/mod-duox-controller}")
    DUO=$(cd "$EMU" && realpath -m "${FW_DUO:-deps/mod-duo-controller}")
    rev() { git -C "$1" rev-parse HEAD 2> /dev/null || echo "${2:-unknown}"; }
    describe() { git -C "$1" describe --tags --always --dirty 2> /dev/null || echo unknown; }
    {
        echo "mod-hw-emulator with MOD Desktop, for Windows (x86_64)"
        echo
        echo "Built from these sources; each is free software under its own license (GPL/AGPL: LICENSE*):"
        echo
        echo "mod-hw-emulator       https://github.com/sejerpz/mod-hw-emulator      $(rev "$EMU" "$MHE_REV")"
        echo "mod-desktop           https://github.com/sejerpz/mod-desktop           $(rev "$MOD_DESKTOP") ($(describe "$MOD_DESKTOP"), VERSION $(cat "$MOD_DESKTOP/VERSION"))"
        git -C "$MOD_DESKTOP" submodule status --recursive 2> /dev/null | awk '$2 != "src/mod-ui" { printf "  %-40s %s\n", $2, $1 }'
        echo "mod-ui                https://github.com/sejerpz/mod-ui                $(rev "$MOD_DESKTOP/src/mod-ui" "$MOD_UI_REV") (branch devel, the src/mod-ui of sejerpz/mod-desktop)"
        echo "mod-host             https://github.com/sejerpz/mod-host              $(rev "$MOD_HOST" "$MOD_HOST_REV") (branch devel, replaces the src/mod-host of mod-desktop)"
        echo "mod-system-control    https://github.com/sejerpz/mod-system-control    ${MSC_REV:-$(rev "$EMU/deps/mod-system-control")} (branch devel)"
        echo "Dwarf firmware        https://github.com/sejerpz/mod-dwarf-controller   $(rev "$DWARF") ($(describe "$DWARF"), branch devel)"
        echo "Duo X firmware        https://github.com/sejerpz/mod-duox-controller   $(rev "$DUOX") ($(describe "$DUOX"), fork of mod-audio/mod-duox-controller)"
        echo "Duo firmware          https://github.com/sejerpz/mod-duo-controller    $(rev "$DUO") ($(describe "$DUO"), branch devel)"
        echo
        echo "with patches/mod-desktop of mod-hw-emulator applied to the launcher."
        echo "Not an official MOD Audio build: the Tone3000 integration has no client id and does not work."
    } | sed 's/$/\r/' > "$PKG/SOURCES.txt"
fi

rm -f "$OUT"
(cd "$STAGE" && zip -qr -9 "$OUT" mod-hw-emulator)
echo "$OUT"
