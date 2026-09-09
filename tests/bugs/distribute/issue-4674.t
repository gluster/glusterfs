#!/bin/bash

# Test for issue #4674: removal of the "cluster.dht-xattr-name" option
# (dht "xattr-name"), a fossil of the removed tiering feature.
#
# Post-removal contract:
#   1. DHT's on-disk names are fixed at the defaults:
#      trusted.glusterfs.dht (layout) and trusted.glusterfs.dht.linkto
#      (linkfile pointer).
#   2. "volume set/get cluster.dht-xattr-name" is rejected.
#   3. A stale client volfile that still carries "option xattr-name"
#      (e.g. one generated before an upgrade) still mounts and serves;
#      the option is ignored and the on-disk names stay at the defaults.
#
# Supersedes tests/bugs/distribute/bug-924265.t, which asserted the
# opposite of (2)+(3): that the option exists and a non-default value
# is honored.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# We only care about the exit code, so keep it quiet.
function silent_getfattr {
    getfattr $* &> /dev/null
}

# The linkfile lands on whichever subvol the new name hashes to.
function linkto_present {
    silent_getfattr -n trusted.glusterfs.dht.linkto $B0/${V0}0/FILE-2 ||
        silent_getfattr -n trusted.glusterfs.dht.linkto $B0/${V0}1/FILE-2
}

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
# On a nearly-full host the min-free-disk watermark (default 10%) makes DHT
# place new files off their hashed subvol (forging linkfiles at create),
# which breaks the deterministic rename-linkfile check below. Disable it.
TEST $CLI volume set $V0 cluster.min-free-disk 0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'

## Part 1 — defaults hold (regression guard, must pass before and after)

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

TEST mkdir $M0/dir0
TEST silent_getfattr -n trusted.glusterfs.dht $B0/${V0}0/dir0
TEST silent_getfattr -n trusted.glusterfs.dht $B0/${V0}1/dir0

# Rename creates a linkfile carrying the derived linkto name
# (FILE-1 and FILE-2 hash to different subvols on a 2-brick layout,
# same technique as non-root-unlink-stale-linkto.t).
TEST dd if=/dev/urandom of=$M0/FILE-1 count=1 bs=16k
TEST mv $M0/FILE-1 $M0/FILE-2
TEST linkto_present

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

## Part 2 — a stale volfile with "option xattr-name" is tolerated
##          but no longer honored

VOLF=$GLUSTERD_WORKDIR/vols/$V0/$V0.tcp-fuse.vol
STALE=$B0/stale-$V0.vol
TEST grep -q "cluster/distribute" $VOLF
sed "/type cluster\/distribute/a\    option xattr-name trusted.foo.bar" $VOLF > $STALE
# guard against the injection going vacuous if the volfile layout changes
TEST grep -q "xattr-name" $STALE

# tolerated: the mount and FOPs must work
TEST $GFS -f $STALE $M1
TEST mkdir $M1/dir1

# not honored: the layout xattr stays at the default name on both bricks
TEST silent_getfattr -n trusted.glusterfs.dht $B0/${V0}0/dir1
TEST silent_getfattr -n trusted.glusterfs.dht $B0/${V0}1/dir1
TEST ! silent_getfattr -n trusted.foo.bar $B0/${V0}0/dir1
TEST ! silent_getfattr -n trusted.foo.bar $B0/${V0}1/dir1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

## Part 3 — the option is gone from the volume-set table
##          (last: on pre-removal builds the set succeeds and mutates
##           the volume, so nothing may run after it)

TEST ! $CLI volume set $V0 cluster.dht-xattr-name trusted.foo.bar
TEST ! $CLI volume get $V0 cluster.dht-xattr-name
TEST ! grep -q "xattr-name" $VOLF

cleanup
