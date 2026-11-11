#!/bin/sh
# Lists the files of a PawPaw downloads folder that are not the archives their name says, such as the page a
# site answers with instead of the file (.github/workflows/release.yml removes them so they are downloaded
# again). Only the start of each archive is read.
#
#   scripts/ci-bad-downloads.sh DIR

cd "$1" || exit 1
for f in *; do
    [ -f "$f" ] || continue
    case "$f" in
        *.tar.*|*.tgz|*.tbz2|*.txz)
            tar -tf "$f" 2> /dev/null | head -n 1 | grep -q . || echo "$f" ;;
        *.zip)
            unzip -qql "$f" > /dev/null 2>&1 || echo "$f" ;;
    esac
done
