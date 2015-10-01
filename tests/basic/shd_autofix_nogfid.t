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
TEST $CLI volume set $V0 cluster.quorum-type auto
TEST $CLI volume set $V0 cluster.favorite-child-policy majority
#EST $CLI volume set $V0 cluster.favorite-child-by-majority on
#EST $CLI volume set $V0 cluster.favorite-child-by-mtime on
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0
sleep 5

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0

# Kill the SHD while we setup the test
pkill -f gluster/glustershd
TEST kill_brick $V0 $H0 $B0/${V0}1

mkdir $M0/foo
dd if=/dev/urandom of=$M0/foo/testfile bs=128k count=5 2>/dev/null
MD5=$(md5sum $M0/foo/testfile | cut -d\  -f1)

mkdir $B0/${V0}1/foo

# Kick off the SHD and wait 30 seconds for healing to take place
TEST gluster vol start $V0 force
EXPECT_WITHIN 30 "0" get_pending_heal_count $V0

# Verify the file was healed back to brick 1
TEST stat $B0/${V0}1/foo/testfile

# Part II: Test recovery for a file without a GFID
# Kill the SHD while we setup the test
pkill -f gluster/glustershd
TEST kill_brick $V0 $H0 $B0/${V0}1
rm -f $GFID_LINK_B1
rm -f $B0/${V0}1/foo/testfile
touch $B0/${V0}1/foo/testfile

# Queue the directories for healing, don't bother the queue the file
# as this shouldn't be required.
touch $B0/${V0}3/.glusterfs/indices/xattrop/00000000-0000-0000-0000-000000000001
touch $B0/${V0}3/.glusterfs/indices/xattrop/$GFID_PARENT_FORMATTED

TEST gluster vol start $V0 force
EXPECT_WITHIN 30 "0" get_pending_heal_count $V0
TEST stat $B0/${V0}1/foo/testfile

# Prove the directory and file are removable
TEST rm -f $B0/${V0}1/foo/testfile
TEST rmdir $B0/${V0}1/foo

cleanup
