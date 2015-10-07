#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

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
#EST $CLI volume set $V0 cluster.favorite-child-by-majority on
#EST $CLI volume set $V0 cluster.favorite-child-by-mtime on
TEST $CLI volume set $V0 cluster.favorite-child-policy majority
TEST $CLI volume set $V0 storage.build-pgfid on
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0
sleep 5

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0

cd $M0
mkdir -p a/b/c
dd if=/dev/urandom of=a/b/c/testfile bs=128k count=5 2>/dev/null
MD5=$(md5sum a/b/c/testfile | cut -d\  -f1)

# Kill the SHD while we setup the test
pkill -f gluster/glustershd
# Kill the brick as well such that 
TEST kill_brick $V0 $H0 $B0/${V0}1

# Grab the GFID of the file and parent dir
GFID_PARENT_B_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/a/b 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_PARENT_B_FORMATTED=$(echo "$GFID_PARENT_B_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_PARENT_B_LINK_B1="$B0/${V0}1/.glusterfs/$(echo $GFID_PARENT_B_RAW | awk '{print substr($0,3,2)"/"substr($0,5,2)"/"substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')"
GFID_PARENT_C_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/a/b/c 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_PARENT_C_FORMATTED=$(echo "$GFID_PARENT_C_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_PARENT_C_LINK_B1="$B0/${V0}1/.glusterfs/$(echo $GFID_PARENT_C_RAW | awk '{print substr($0,3,2)"/"substr($0,5,2)"/"substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')"
GFID_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/a/b/c/testfile 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_FORMATTED=$(echo "$GFID_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_LINK_B1="$B0/${V0}1/.glusterfs/$(echo $GFID_RAW | awk '{print substr($0,3,2)"/"substr($0,5,2)"/"substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')"

#
# Here we are going to create a situation such that a file 3
# levels deep into the FS requires healing, along with 2 levels
# of parent directories.  The only signal SHD has is that the
# file itself needs healing.  The directory (entry) heals are
# missing; simulating a crash or some sort of bug that we need
# to be able to recover from.
#

# Nuke the file from brick 1, along with the parent directories
# and all backend hard/symbolic links
rm -f $B0/${V0}1/a/b/c/testfile
rm -f $GFID_LINK_B1
rmdir $B0/${V0}1/a/b/c
rm -f $GFID_PARENT_C_LINK_B1
rmdir $B0/${V0}1/a/b
rm -f $GFID_PARENT_B_LINK_B1

# Now manually queue up the parent directory for healing
touch $B0/${V0}3/.glusterfs/indices/xattrop/$GFID_FORMATTED

# Kick off the SHD and wait 30 seconds for healing to take place
TEST gluster vol start patchy force
EXPECT_WITHIN 30 "0" get_pending_heal_count $V0
sleep 5

# Verify the file was healed back to brick 1
TEST stat $B0/${V0}1/a/b/c/testfile

cleanup
