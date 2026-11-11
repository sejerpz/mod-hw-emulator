#!/bin/bash
# Runs a mod-ui checkout against the emulated controller, through its debug-server.sh.
#
#   scripts/run-mod-ui.sh [--device dwarf|duox] MOD_UI_DIR [PORT]
#
# Start the emulator first: build/mhe-run --tcp webgui=PORT build/fw-<device>.so (PORT 9900 by default).
# The device (dwarf by default) picks the hardware descriptor mod-ui reads, devices/<device>/.
# mod-ui needs a tcp:// HMI port: branch feature/mod-hw-emulator, or patches/mod-ui/0001-hmi-tcp-transport.patch.
#
# Without mod-host (default): MOD_DEV_HOST=1, no jackd, no plugin widgets.
# With mod-host (scripts/run-mod-host.sh): MOD_DEV_HOST=0 and MOD_DEVICE_HOST_PORT=<its port>; server.py
# turns the development environment on by itself, so MOD_DEV_HOST=0 must be set explicitly.

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=dwarf

while [ $# -gt 0 ]; do
    case "$1" in
        --device) DEVICE=$2; shift ;;
        --device=*) DEVICE=${1#--device=} ;;
        -*) echo "unknown option $1" >&2; exit 1 ;;
        *) break ;;
    esac
    shift
done

UI=$(cd "${1:?usage: $0 [--device dwarf|duox] MOD_UI_DIR [PORT]}" && pwd)
PORT=${2:-9900}

if [ ! -f "$EMU/devices/$DEVICE/mod-hardware-descriptor.json" ]; then
    echo "no device '$DEVICE': dwarf or duox" >&2
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

cd "$UI"
exec sh ./debug-server.sh
