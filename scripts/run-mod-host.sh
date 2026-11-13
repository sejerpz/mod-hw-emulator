#!/bin/bash
# Starts jackd with mod-host inside it, as on the device and in mod-desktop.
#
#   scripts/run-mod-host.sh [PORT]
#
# mod-host opens /sys_msgs (plugin widgets, labels, leds) only as a JACK internal client, and only when built
# with `make MOD_HMI_EMULATOR=1` (branch devel). Start the emulator first: it creates
# /sys_msgs. PORT is mod-host's socket port (feedback on PORT+1), 5555 by default: mod-ui must use the same,
# MOD_DEVICE_HOST_PORT. Pick another one when something else has 5555 (the Android emulator's adb, say; with
# WSL mirrored networking the Windows ports count too).
#
# Environment: MOD_HOST (checkout, default ../mod-host, next to mod-hw-emulator), LV2_PATH (default ../plugins/lv2),
#              JACK_ARGS (default: dummy backend, 48 kHz, 256 frames).

set -e

EMU=$(cd "$(dirname "$0")/.." && pwd)
PORT=${1:-5555}
MOD_HOST=$(cd "${MOD_HOST:-$EMU/../mod-host}" && pwd)
export LV2_PATH=${LV2_PATH:-$(cd "$EMU/../plugins/lv2" && pwd)}
JACK_ARGS=${JACK_ARGS:--d dummy -r 48000 -p 256}

if [ ! -f "$MOD_HOST/mod-host.so" ]; then
    echo "no $MOD_HOST/mod-host.so: build it with make MOD_HMI_EMULATOR=1" >&2
    exit 1
fi
if [ ! -e /dev/shm/sys_msgs ]; then
    echo "warning: no /dev/shm/sys_msgs, start the emulator first or mod-host runs without the HMI" >&2
fi

# jackd looks for internal clients in JACK_INTERNAL_DIR
export JACK_INTERNAL_DIR=$MOD_HOST
# shellcheck disable=SC2086
jackd $JACK_ARGS &
JACKD=$!
trap 'kill $JACKD 2>/dev/null' EXIT INT TERM

for _ in $(seq 20); do
    jack_lsp > /dev/null 2>&1 && break
    sleep 0.5
done

jack_load -i "$PORT" mod-host mod-host
echo "mod-host on port $PORT (feedback $((PORT + 1))), inside jackd; Ctrl+C stops both"
wait $JACKD
