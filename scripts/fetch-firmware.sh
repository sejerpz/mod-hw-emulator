#!/bin/sh
# Fetches the firmware sources the emulator builds, into external/ (not tracked).
#
#   scripts/fetch-firmware.sh [DWARF_REPO [BRANCH [PROTO_BRANCH]]]
#   scripts/fetch-firmware.sh duox [BRANCH]
#
# Defaults: the aLabs Dwarf controller with HMI widgets, and the protocol branch that matches it; the
# mod-audio Duo X controller (make DEVICE=duox), whose protocol submodule is published as pinned.

set -e

cd "$(dirname "$0")/.."

if [ "$1" = duox ]; then
    DUOX=external/mod-duox-controller
    mkdir -p external
    if [ ! -d "$DUOX/.git" ]; then
        git clone --branch "${2:-master}" https://github.com/mod-audio/mod-duox-controller.git "$DUOX"
    else
        git -C "$DUOX" fetch origin
        git -C "$DUOX" checkout "${2:-master}"
    fi
    git -C "$DUOX" submodule update --init
    echo "firmware: $(git -C "$DUOX" log --oneline -1)"
    exit 0
fi

DWARF_REPO=${1:-https://github.com/sejerpz/mod-dwarf-controller.git}
DWARF_BRANCH=${2:-feature/hmi-widgets}
PROTO_REPO=https://github.com/sejerpz/mod-controller-proto.git
PROTO_BRANCH=${3:-feature/hmi-widgets}

mkdir -p external

if [ ! -d external/mod-dwarf-controller/.git ]; then
    git clone --branch "$DWARF_BRANCH" "$DWARF_REPO" external/mod-dwarf-controller
else
    git -C external/mod-dwarf-controller fetch origin
    git -C external/mod-dwarf-controller checkout "$DWARF_BRANCH"
fi

# the protocol submodule: the commit pinned by the firmware is not published; it is the widgets branch
# merged with the ui-config one, rebuilt here
PROTO=external/mod-dwarf-controller/mod-controller-proto
if [ ! -d "$PROTO/.git" ] && [ ! -f "$PROTO/.git" ]; then
    rm -rf "$PROTO"
    git clone "$PROTO_REPO" "$PROTO"
else
    git -C "$PROTO" fetch origin
fi
git -C "$PROTO" checkout --detach "origin/$PROTO_BRANCH"
git -C "$PROTO" -c user.name=mhe -c user.email=mhe@localhost merge --no-edit origin/feature/ui-config

echo "firmware: $(git -C external/mod-dwarf-controller log --oneline -1)"
echo "protocol: $(git -C "$PROTO" log --oneline -1)"
