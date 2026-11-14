#!/bin/sh
# restore.sh NAME [DEST] - pull store-NAME from deon81997/priavte and unpack it.
#   NAME: 86rig, qemu, win98 (= win98-a + win98-b), personal, or any store-<name> branch.
#   DEST: default from the store README table (qemu -> /tmp, everything else -> /home/claude).
# Skips the download if DEST already has a marker from an earlier restore (use -f to force).
# Needs: git, zstd, tar.
set -e
FORCE=0
[ "$1" = "-f" ] && { FORCE=1; shift; }
NAME=$1
[ -n "$NAME" ] || { echo "usage: $0 [-f] NAME [DEST]   (86rig qemu win98 personal ...)" >&2; exit 2; }
REPO=${STORE_REPO:-https://github.com/deon81997/priavte}
case $NAME in
  qemu) DEF=/tmp ;;
  *)    DEF=/home/claude ;;
esac
DEST=${2:-$DEF}
MARK=$DEST/.store-$NAME.restored
if [ $FORCE = 0 ] && [ -f "$MARK" ]; then echo "store-$NAME already in $DEST ($MARK)"; exit 0; fi
case $NAME in
  win98) BRANCHES="store-win98-a store-win98-b" ;;
  *)     BRANCHES="store-$NAME" ;;
esac
TMP=$(mktemp -d /tmp/restore.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
PARTS=""
for B in $BRANCHES; do
  git clone -q --depth 1 -b "$B" "$REPO" "$TMP/$B" || { echo "no branch $B in $REPO" >&2; exit 1; }
  PARTS="$PARTS $(ls "$TMP/$B"/store/* | sort | tr '\n' ' ')"
done
mkdir -p "$DEST"
# shellcheck disable=SC2086
cat $PARTS | zstd -dc | tar -x -C "$DEST"
date > "$MARK"
echo "store-$NAME -> $DEST"
