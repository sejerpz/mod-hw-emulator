#!/bin/sh
# Clones the firmware sources the emulator builds next to it, where the Makefile looks for them
# (../mod-dwarf-controller, ../mod-duox-controller), with their protocol submodule at the commit they pin.
#
#   scripts/fetch-firmware.sh [dwarf|duox [BRANCH]]
#
# Defaults: the Dwarf controller of sejerpz on feature/mod-hw-emulator (the aLabs HMI widgets plus the fixes
# found with the emulator), the Duo X controller on master of the sejerpz fork of mod-audio's. Both are
# published with their submodules, so a plain clone builds.

set -e

cd "$(dirname "$0")/../.."

DEVICE=${1:-dwarf}
case "$DEVICE" in
    dwarf) BRANCH=${2:-feature/mod-hw-emulator} ;;
    duox) BRANCH=${2:-master} ;;
    *) echo "usage: $0 [dwarf|duox [BRANCH]]" >&2; exit 2 ;;
esac
DIR=mod-$DEVICE-controller

if [ ! -e "$DIR/.git" ]; then
    git clone --branch "$BRANCH" "https://github.com/sejerpz/$DIR.git" "$DIR"
else
    git -C "$DIR" fetch origin
    git -C "$DIR" checkout "$BRANCH"
fi
git -C "$DIR" submodule update --init

echo "firmware: $(git -C "$DIR" log --oneline -1)"
echo "protocol: $(git -C "$DIR/mod-controller-proto" log --oneline -1)"
