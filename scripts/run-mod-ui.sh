#!/bin/bash
# Runs a mod-ui checkout against the emulated controller, through its debug-server.sh.
#
#   scripts/run-mod-ui.sh MOD_UI_DIR [PORT]
#
# Start the emulator first: build/mhe-run --tcp webgui=PORT build/fw-dwarf.so (PORT 9900 by default).
# mod-ui needs a tcp:// HMI port: branch feature/mod-hw-emulator, or patches/mod-ui/0001-hmi-tcp-transport.patch.
#
# Without mod-host (default): MOD_DEV_HOST=1, no jackd, no plugin widgets.
# With mod-host (scripts/run-mod-host.sh): MOD_DEV_HOST=0 and MOD_DEVICE_HOST_PORT=<its port>; server.py
# turns the development environment on by itself, so MOD_DEV_HOST=0 must be set explicitly.

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
UI=$(cd "${1:?usage: $0 MOD_UI_DIR [PORT]}" && pwd)
PORT=${2:-9900}

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
export MOD_HARDWARE_DESC_FILE=$EMU/devices/dwarf/mod-hardware-descriptor.json

cd "$UI"
exec sh ./debug-server.sh
