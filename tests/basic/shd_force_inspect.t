#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

# Setup a cluster with 3 replicas, and fav child by majority on
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3};
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 cluster.self-heal-daemon on
TEST $CLI volume set $V0 nfs.disable on
TEST $CLI volume set $V0 cluster.quorum-type none
TEST $CLI volume set $V0 cluster.favorite-child-by-majority on
TEST $CLI volume set $V0 cluster.favorite-child-by-mtime on
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0
sleep 5

# Part I: FUSE Test
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0

cd $M0
mkdir foo
dd if=/dev/urandom of=foo/testfile bs=128k count=5 2>/dev/null
MD5=$(md5sum foo/testfile | cut -d\  -f1)

# Kill the SHD while we setup the test
pkill -f gluster/glustershd

# Grab the GFID of the file and parent dir
GFID_PARENT_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/foo 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_PARENT_FORMATTED=$(echo "$GFID_PARENT_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/foo/testfile 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_FORMATTED=$(echo "$GFID_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_LINK_B1="$B0/${V0}1/.glusterfs/$(echo $GFID_RAW | awk '{print substr($0,3,2)"/"substr($0,5,2)"/"substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')"

# Nuke the file from brick 1
rm -f $GFID_LINK_B1
rm -f $B0/${V0}1/foo/testfile

# Now manually queue up the parent directory for healing
touch $B0/${V0}2/.glusterfs/indices/xattrop/$GFID_PARENT_FORMATTED
touch $B0/${V0}3/.glusterfs/indices/xattrop/$GFID_PARENT_FORMATTED

# Kick off the SHD and wait 30 seconds for healing to take place
TEST gluster vol start patchy force
EXPECT_WITHIN 30 "0" afr_get_pending_heal_count $V0

# Verify the file was healed back to brick 1
TEST stat $B0/${V0}1/foo/testfile

cleanup
