#!/bin/sh

#
# Copyright (c) 2021 Pavilion Data Systems, Inc. <http://www.pavilion.io>
#

# Usage:
# BRICK_PATH=<brick-path>
# find $BRICK_PATH -path "$BRICK_PATH/.glusterfs" -prune -o \
#        -type f -amin +60 \
#        -exec /usr/share/glusterfs/scripts/migrate-to-cold.sh <volume-mount> <cold-tier-mount> <brick-path> {} \; 2>/dev/null
# Example:
# BRICK_PATH=/bricks/gvol/brick1/brick
# find $BRICK_PATH -path "$BRICK_PATH/.glusterfs" -prune -o \
#        -type f -amin +60 \
#        -exec /usr/share/glusterfs/scripts/migrate-to-cold.sh /mnt/gvol /mnt/tier $BRICK_PATH {} \; 2>/dev/null


if [ $# -ne 4 ] ; then
    echo "Usage: $0 <volume-mount> <cold-tier-mount> <brick-path> <full-path-to-file>"
    exit 1;
fi

volume_mnt=$(echo $1 | sed -e "s#/\$##")
cold_tier_mnt=$(echo $2 | sed -e "s#/\$##")
brick_path=$(echo $3 | sed -e "s#/\$##")
source=$(echo $4 | sed -e "s#${brick_path}/##")

# Take mtime from mount with nanosecond granularity
mtime=$(stat "$volume_mnt/$source" -c '%.Y')

gfid=$(attr -R -g gfid -q "$brick_path/$source" | hexdump -ve '1/1 "%.2x"')
is_remote=$(attr -R -g glusterfs.cs.remote -q "$brick_path/$source" 2>/dev/null| wc -w)
dest="$cold_tier_mnt/$source.$gfid"

if [ $is_remote -ne 0 ]; then
    >&2 echo "[NOT OK] File is already remote [{source=$source}]"
    exit 1;
fi

mkdir -p $(dirname "$dest")
cp -a "$volume_mnt/$source" "$dest"
# Ideally, the application calling copy should make sure the data is
# safe before calling below setfattr
ret=$?

if [ $ret -eq 0 ] ; then
    setfattr -n tier.mark-file-as-remote -v "$mtime:$source.$gfid" "$volume_mnt/$source"
    ret=$?
    if [ $ret -ne 0 ] ; then
        >&2 echo "[NOT OK] Failed to set 'tier.mark-file-as-remote' xattr after copy. [{source=$source}]"
        exit $ret
    fi
else
    >&2 echo "[NOT OK] Copy failed. [{source=$source}, {cold_tier=$cold_tier_mnt}]"
    exit $ret
fi

echo "[    OK] Finished migration. [{source=$source}, {dest=$dest}]"
