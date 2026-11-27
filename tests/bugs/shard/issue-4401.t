#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# GH-4401: With features.shard enabled, the size of a file being copied
# into the volume could turn out to be wrong (larger or smaller than the
# source) when an application reads the target directory (readdirp) while
# the copy is in progress and performance.write-behind is on.
#
# The reason is that readdirp can mark the file's shard inode-ctx stale
# (ctx->refresh), and the refresh lookup issued by the next write then
# overwrites the inode-ctx size with a stale value while the previous
# write's size-update xattrop is still in flight, resulting in the delta
# being accounted twice (or lost) in trusted.glusterfs.shard.file-size.

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}-{0,1,2}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume set $V0 features.shard-block-size 1024MB
TEST $CLI volume start $V0

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

# The file from the issue: 1441792 = 11 * 128k. Keep the source file
# outside the bricks; a running brick must not share its directory with
# files created without glusterfs gfids.
src_dir=$(mktemp -d /var/tmp/issue-4401.XXXXXX)
TEST dd if=/dev/zero of=$src_dir/data6.img bs=128k count=11
src_size=$(stat -c %s $src_dir/data6.img)
src_md5=$(md5sum $src_dir/data6.img | awk '{print $1}')

# A couple of other files so that the readdirp hammer has something to
# chew on.
TEST dd if=/dev/zero of=$M0/pad0 bs=128k count=1
TEST dd if=/dev/zero of=$M0/pad1 bs=128k count=2

bad_runs=0
for i in $(seq 1 30); do
    # Simulate the application from the issue which frequently reads the
    # directory that the file is being copied into. This triggers readdirp
    # and lookups on the file's inode while writes are in flight.
    ( while true; do ls -l $M0 >/dev/null 2>&1; done ) &
    hammer1=$!
    ( while true; do ls -f $M0 >/dev/null 2>&1; done ) &
    hammer2=$!

    cp $src_dir/data6.img $M0/data6.img

    kill -9 $hammer1 $hammer2 2>/dev/null
    wait $hammer1 2>/dev/null
    wait $hammer2 2>/dev/null

    size=$(stat -c %s $M0/data6.img 2>/dev/null)
    md5=$(md5sum $M0/data6.img 2>/dev/null | awk '{print $1}')
    if [ "$size" != "$src_size" ] || [ "$md5" != "$src_md5" ]; then
        bad_runs=$((bad_runs + 1))
        echo "run $i: size=$size (expected $src_size) md5=$md5 (expected $src_md5)" >&2
    fi

    rm -f $M0/data6.img
done

EXPECT 0 echo $bad_runs

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

rm -rf $src_dir
cleanup
