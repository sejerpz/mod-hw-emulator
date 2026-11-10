#!/bin/sh
# Exports one commit of a firmware checkout, with its protocol submodule at the commit it pins, to a folder
# the Makefile builds from (make FW_REF=...). The checkout is only read: no checkout, no worktree.
#
#   scripts/export-firmware.sh REPO REF DEST
#
# The submodule commit is taken from the submodule clone inside REPO: the commits the aLabs firmware pins are
# not all published, a fresh clone could not find them.

set -e

REPO=$1
REF=$2
DEST=$3

if [ -z "$DEST" ]; then
    echo "usage: $0 REPO REF DEST" >&2
    exit 2
fi

COMMIT=$(git -C "$REPO" rev-parse --verify "$REF^{commit}")

rm -rf "$DEST"
mkdir -p "$DEST"
git -C "$REPO" archive "$COMMIT" | tar -x -C "$DEST"

# submodules are gitlinks: archive leaves an empty folder
git -C "$REPO" ls-tree -r "$COMMIT" | awk '$2 == "commit" { print $3, $4 }' | while read -r sub path; do
    if ! git -C "$REPO/$path" cat-file -e "$sub^{commit}" 2>/dev/null; then
        echo "$REF pins $path at $sub, which $REPO/$path does not have: fetch it there" >&2
        exit 1
    fi
    mkdir -p "$DEST/$path"
    git -C "$REPO/$path" archive "$sub" | tar -x -C "$DEST/$path"
done

echo "$COMMIT" > "$DEST/.exported"
echo "exported $REF ($COMMIT) of $REPO to $DEST" >&2
