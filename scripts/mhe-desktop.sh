#!/bin/bash
# mod-hw-emulator - runs MOD Desktop with an emulated controller, the Dwarf or the Duo X (Linux, standalone package)
#
#   ./mhe-desktop.sh [--device dwarf|duox] [--no-emulator] [--shared-data]
#
# The package (scripts/package-linux.sh) holds a MOD Desktop built from mod-desktop main with mod-host, mod-ui's
# hmi.py and addressing dialog and the launcher of mod-hw-emulator in it, and the emulator next to it. This writes
# the descriptor of the device for mod-ui, starts the emulator (unless --no-emulator: one already running) and then
# MOD Desktop's launcher; they talk over 127.0.0.1: 9900 for mod-ui's HMI line, 9901 for mod-host's /sys_msgs.
# The data (pedalboards, user files, the launcher log) is in data/ next to this script, or in
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

descriptor="$here/devices/$device/mod-desktop-descriptor.json"
firmware="$here/fw-$device.so"
if [ ! -f "$descriptor" ] || [ ! -f "$firmware" ]; then
    echo "no device '$device' here: dwarf or duox" >&2
    exit 1
fi
# the emulator's EEPROM and settings, one per device
state="$here/mhe-state/$device"

# with a timeout: where nothing refuses a connection to a closed port (WSL's mirrored network) it would hang
listening() {
    timeout 1 bash -c "exec 3<> /dev/tcp/127.0.0.1/$1" 2> /dev/null
}

# MOD Desktop's descriptor with the actuators and HMI options of the device (merged when the package was built)
cp "$descriptor" "$here/mod-desktop/mod-hardware-descriptor.json"

# both sides of /sys_msgs over TCP, as on Windows: the emulator listens, mod-host connects
export MOD_HMI_SERIAL_PORT="tcp://127.0.0.1:$hmi_port"
export MOD_SYS_SERIAL_PORT="tcp://127.0.0.1:$sys_port"
export MOD_DEV_HMI=0

if $emulator; then
    if listening $sys_port; then
        echo "something is already on port $sys_port: an emulator running? (--no-emulator to use it)" >&2
        exit 1
    fi
    (cd "$here" && exec ./mod-hw-emulator --state "$state" --tcp webgui=$hmi_port "$firmware" > /dev/null 2>&1 &)
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
export MOD_HW_EMULATOR="\"$here/mod-hw-emulator\" --state \"$state\" --tcp webgui=$hmi_port \"$firmware\""
if ! $shared; then
    export MOD_DATA_DIR="$here/data"
fi

echo "MOD Desktop ($here/mod-desktop) started with the emulated $device"
cd "$here/mod-desktop"
exec ./mod-desktop
