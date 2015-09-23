#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.read-subvolume-index 2
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.heal-timeout 30
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 nfs.write-size 524288
TEST $CLI volume set $V0 nfs.read-size 524288
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0
for i in {1..10}
do
        dd if=/dev/urandom of=testfile$i bs=1M count=1 2>/dev/null
done
cd ~
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST rm -rf $B0/${V0}2/testfile*
TEST rm -rf $B0/${V0}2/.glusterfs

TEST $CLI volume stop $V0
sleep 5
TEST $CLI volume start $V0
EXPECT_WITHIN 20 "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN 30 "0" get_pending_heal_count $V0

# Verify we see all ten files when ls'ing, without the patch this should
# return no files and fail.
how_many_files () {
        \ls $M0 2> /dev/null | wc -l
}
# Once it's triggered, entry self-heal happens asynchronously and might take
# a long-ish time before the missing entries appear, but it does complete.
# To accommodate that, retry for a little while.
EXPECT_WITHIN 20 "10" how_many_files

cleanup
