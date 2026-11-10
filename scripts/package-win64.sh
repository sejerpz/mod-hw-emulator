#!/bin/bash
# Builds the Windows package of mod-hw-emulator: the emulator with its user interface, and the files mhe-desktop.cmd
# lays over a MOD Desktop installation.
#
#   scripts/package-win64.sh MOD_DESKTOP MOD_HOST [OUT]
#
# MOD_DESKTOP is a mod-desktop checkout at the release the package is for (git checkout 0.0.13, say) whose PawPaw
# dependencies are built for win64, as mod-desktop's CI does it (ubuntu:22.04):
#
#   utils/apply-patches.sh
#   src/PawPaw/.github/workflows/bootstrap-deps.sh win64
#   src/PawPaw/bootstrap-mod.sh win64
#
# so mod-host links the same jack and lilv as the MOD Desktop it goes in. MOD_HOST is a mod-host checkout on
# branch feature/mod-hw-emulator (sys_serial_tcp.h); mod-ui's hmi.py is the one of MOD_DESKTOP with
# patches/mod-ui/0001-hmi-tcp-transport.patch. OUT is the zip, mod-hw-emulator-win64.zip by default.

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
MOD_DESKTOP=$(cd "$1" && pwd)
MOD_HOST=$(cd "$2" && pwd)
OUT=$(realpath -m "${3:-mod-hw-emulator-win64.zip}")
RUN="$MOD_DESKTOP/utils/run.sh win64"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
PKG=$STAGE/mod-hw-emulator
OVL=$PKG/mod-desktop-overlay
mkdir -p "$OVL" "$PKG/skins" "$PKG/devices/dwarf" "$PKG/platforms"

# mod-host, built as MOD Desktop builds its own plus the HMI control
cp -a "$MOD_HOST" "$STAGE/mod-host"
# the fixes MOD Desktop applies to its own mod-host, when they apply to this one
if [ -f "$MOD_DESKTOP/patches/mod-host.patch" ] && patch -s -p1 --dry-run -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch" > /dev/null 2>&1; then
    patch -s -p1 -d "$STAGE/mod-host" < "$MOD_DESKTOP/patches/mod-host.patch"
fi
$RUN make -C "$STAGE/mod-host" clean > /dev/null
$RUN make -C "$STAGE/mod-host" MOD_DESKTOP=1 MOD_HMI_EMULATOR=1 SKIP_READLINE=1 SKIP_FFTW335=1 WINDOWS=true mod-host.so
cp "$STAGE/mod-host/mod-host.so" "$OVL/mod-host.dll"

# mod-ui's HMI line over TCP, on the hmi.py of this MOD Desktop
cp "$MOD_DESKTOP/src/mod-ui/mod/hmi.py" "$STAGE/hmi.py"
sha256sum "$STAGE/hmi.py" | cut -d' ' -f1 | tr a-f A-F > "$OVL/hmi.py.base-sha256"
patch -s "$STAGE/hmi.py" < "$EMU/patches/mod-ui/0001-hmi-tcp-transport.patch"
cp "$STAGE/hmi.py" "$OVL/hmi.py"

# the emulator, with the Qt of MOD Desktop
$RUN make -C "$EMU" BUILD=build-win64 all gui
cp "$EMU"/build-win64/{mod-hw-emulator.exe,mhe-run.exe,fw-dwarf.dll} "$PKG/"

PREFIX=$($RUN sh -c 'echo $PAWPAW_PREFIX')
for dll in Qt5Core Qt5Gui Qt5Widgets; do
    cp "$PREFIX/bin/$dll.dll" "$PKG/" 2> /dev/null || cp "$MOD_DESKTOP/build/$dll.dll" "$PKG/"
done
cp "$PREFIX"/plugins/platforms/qwindows.dll "$PKG/platforms/" 2> /dev/null \
    || cp "$MOD_DESKTOP"/build/platforms/qwindows.dll "$PKG/platforms/"
# whatever else the programs need from the toolchain (winpthread, the GCC runtimes when not static)
for dll in $(x86_64-w64-mingw32-objdump -p "$PKG"/*.exe "$PKG"/*.dll "$PKG"/platforms/*.dll "$OVL"/*.dll \
             | awk '/DLL Name/ { print $3 }' | sort -u); do
    [ -e "$PKG/$dll" ] && continue
    for dir in "$PREFIX/bin" "$PREFIX/lib" /usr/x86_64-w64-mingw32/lib /usr/lib/gcc/x86_64-w64-mingw32/*-posix; do
        if [ -e "$dir/$dll" ]; then
            cp "$dir/$dll" "$PKG/"
            break
        fi
    done
done

cp -r "$EMU/devices/dwarf/skin" "$PKG/skins/dwarf"
cp "$EMU/devices/dwarf/mod-hardware-descriptor.json" "$PKG/devices/dwarf/"
cp "$EMU/scripts/mhe-desktop.ps1" "$PKG/"
sed 's/$/\r/' "$EMU/scripts/mhe-desktop.cmd" > "$PKG/mhe-desktop.cmd"
cp "$EMU/LICENSE" "$PKG/"

rm -f "$OUT"
(cd "$STAGE" && zip -qr -9 "$OUT" mod-hw-emulator)
echo "$OUT"
