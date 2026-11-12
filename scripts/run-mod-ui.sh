#!/bin/bash
# Runs a mod-ui checkout against the emulated controller, through its debug-server.sh.
#
#   scripts/run-mod-ui.sh [--device dwarf|duox|duo] MOD_UI_DIR [PORT]
#
# Start the emulator first: build/mhe-run --tcp webgui=PORT build/fw-<device>.so (PORT 9900 by default).
# The device (dwarf by default) picks the hardware descriptor mod-ui reads, devices/<device>/.
# mod-ui needs a tcp:// HMI port: branch feature/mod-hw-emulator, or patches/mod-ui/0001-hmi-tcp-transport.patch.
#
# Without mod-host (default): MOD_DEV_HOST=1, no jackd, no plugin widgets.
# With mod-host (scripts/run-mod-host.sh): MOD_DEV_HOST=0 and MOD_DEVICE_HOST_PORT=<its port>; server.py
# turns the development environment on by itself, so MOD_DEV_HOST=0 must be set explicitly.
#
# The data are those debug-server.sh takes next to a mod-ui checkout beside the emulator, whatever mod-ui is
# run (deps/mod-desktop/src/mod-ui too): MOD_DATA_DIR (default ../mod-home), MOD_USER_FILES_DIR (default
# ../user-files), LV2_PATH (default ../plugins/lv2, as scripts/run-mod-host.sh).

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=dwarf

# options before or after the arguments
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --device) DEVICE=${2:?--device needs dwarf, duox or duo}; shift ;;
        --device=*) DEVICE=${1#--device=} ;;
        -*) echo "unknown option $1" >&2; exit 1 ;;
        *) ARGS+=("$1") ;;
    esac
    shift
done

UI=$(cd "${ARGS[0]:?usage: $0 [--device dwarf|duox|duo] MOD_UI_DIR [PORT]}" && pwd)
PORT=${ARGS[1]:-9900}
case "$PORT" in
    ''|*[!0-9]*) echo "PORT is a number, not '$PORT'" >&2; exit 1 ;;
esac

if [ ! -f "$EMU/devices/$DEVICE/mod-hardware-descriptor.json" ]; then
    echo "no device '$DEVICE': dwarf, duox or duo" >&2
    exit 1
fi

if ! grep -q "class SocketSerial" "$UI/mod/hmi.py"; then
    echo "mod-ui has no tcp:// HMI port: use its feature/mod-hw-emulator branch or apply" >&2
    echo "  $EMU/patches/mod-ui/0001-hmi-tcp-transport.patch" >&2
    exit 1
fi

[ -d "$UI/modui-env/bin" ] && export PATH="$UI/modui-env/bin:$PATH"

export MOD_DEV_HMI=0
export MOD_DEV_HOST=${MOD_DEV_HOST:-1}
export MOD_DEVICE_HOST_PORT=${MOD_DEVICE_HOST_PORT:-5555}
export MOD_HMI_SERIAL_PORT=tcp://127.0.0.1:$PORT
export MOD_HARDWARE_DESC_FILE=$EMU/devices/$DEVICE/mod-hardware-descriptor.json
export MOD_DATA_DIR=${MOD_DATA_DIR:-$EMU/../mod-home/}
export MOD_USER_FILES_DIR=${MOD_USER_FILES_DIR:-$EMU/../user-files/}
export LV2_PATH=${LV2_PATH:-$EMU/../plugins/lv2/}

# debug-server.sh with the data above
cd "$UI"
exec python server.py
