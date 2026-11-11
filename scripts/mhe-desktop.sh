#!/bin/bash
# mod-hw-emulator - runs MOD Desktop with an emulated controller, the Dwarf or the Duo X (Linux and macOS packages)
#
#   ./mhe-desktop.sh [--device dwarf|duox] [--no-emulator] [--shared-data]
#
# The package (scripts/package-linux.sh, package-macos.sh) holds a MOD Desktop built from mod-desktop main with
# mod-host, mod-ui's hmi.py and addressing dialog and the launcher of mod-hw-emulator in it, and the emulator. This
# writes the descriptor of the device for mod-ui, starts the emulator (unless --no-emulator: one already running)
# and then MOD Desktop's launcher; they talk over 127.0.0.1: 9900 for mod-ui's HMI line, 9901 for mod-host's
# /sys_msgs. The data (pedalboards, user files, the launcher log) is in data/ next to this script, or in
# ~/Documents/MOD Desktop, the folder of an installed MOD Desktop, with --shared-data.

set -e

here=$(cd "$(dirname "$0")" && pwd)
device=dwarf
emulator=true
shared=false
hmi_port=9900
sys_port=9901

while [ $# -gt 0 ]; do
    case "$1" in
        --device) device=$2; shift ;;
        --device=*) device=${1#--device=} ;;
        --no-emulator) emulator=false ;;
        --shared-data) shared=true ;;
        -h|--help) sed -n '2,11p' "$0" | cut -c3-; exit 0 ;;
        *) echo "unknown option $1 (--help)" >&2; exit 1 ;;
    esac
    shift
done

# where the package keeps each part: on macOS MOD Desktop is an app bundle, with the emulator in it; the
# descriptor in the bundle is a link to devices/current.json, written here, so the bundle stays as signed
if [ "$(uname -s)" = Darwin ]; then
    programs="$here/MOD Desktop.app/Contents/MacOS"
    launcher_dir=$programs
    emulator_bin="$programs/mod-hw-emulator"
    desktop_descriptor="$here/devices/current.json"
    firmware="$here/fw-$device.dylib"
else
    launcher_dir="$here/mod-desktop"
    emulator_bin="$here/mod-hw-emulator"
    desktop_descriptor="$here/mod-desktop/mod-hardware-descriptor.json"
    firmware="$here/fw-$device.so"
fi

descriptor="$here/devices/$device/mod-desktop-descriptor.json"
if [ ! -f "$descriptor" ] || [ ! -f "$firmware" ]; then
    echo "no device '$device' here: dwarf or duox" >&2
    exit 1
fi
# the emulator's EEPROM and settings, one per device
state="$here/mhe-state/$device"

# with a timeout where there is one: where nothing refuses a connection to a closed port (WSL's mirrored
# network) it would hang; macOS has no timeout, and refuses at once
listening() {
    if command -v timeout > /dev/null; then
        timeout 1 bash -c "exec 3<> /dev/tcp/127.0.0.1/$1" 2> /dev/null
    else
        (exec 3<> "/dev/tcp/127.0.0.1/$1") 2> /dev/null
    fi
}

# MOD Desktop's descriptor with the actuators and HMI options of the device (merged when the package was built)
cp "$descriptor" "$desktop_descriptor"

# both sides of /sys_msgs over TCP, as on Windows: the emulator listens, mod-host connects
export MOD_HMI_SERIAL_PORT="tcp://127.0.0.1:$hmi_port"
export MOD_SYS_SERIAL_PORT="tcp://127.0.0.1:$sys_port"
export MOD_DEV_HMI=0

if $emulator; then
    if listening $sys_port; then
        echo "something is already on port $sys_port: an emulator running? (--no-emulator to use it)" >&2
        exit 1
    fi
    (cd "$here" && exec "$emulator_bin" --state "$state" --tcp webgui=$hmi_port "$firmware" > /dev/null 2>&1 &)
fi
for _ in $(seq 30); do
    listening $sys_port && break
    sleep 0.5
done
if ! listening $sys_port; then
    echo "the emulator is not listening on port $sys_port" >&2
    exit 1
fi

# the launcher's "Open Emulator" button runs this, and starts MOD Desktop again if it is running
export MOD_HW_EMULATOR="\"$emulator_bin\" --state \"$state\" --tcp webgui=$hmi_port \"$firmware\""
if ! $shared; then
    export MOD_DATA_DIR="$here/data"
fi

echo "MOD Desktop ($launcher_dir) started with the emulated $device"
cd "$launcher_dir"
exec ./mod-desktop
