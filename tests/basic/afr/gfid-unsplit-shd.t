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
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 cluster.quorum-type none
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 cluster.favorite-child-policy majority
#EST $CLI volume set $V0 cluster.favorite-child-by-majority off
#EST $CLI volume set $V0 cluster.favorite-child-by-mtime on
#EST $CLI volume set $V0 cluster.favorite-child-by-size off
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
dd if=/dev/urandom of=foo/splitfile bs=128k count=5 2>/dev/null

MD5=$(md5sum foo/splitfile | cut -d\  -f1)

sleep 1
cd ~

GFID_PARENT_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/foo 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_PARENT_FORMATTED=$(echo "$GFID_PARENT_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_RAW=$(getfattr -n trusted.gfid -e hex $B0/${V0}1/foo/splitfile 2>/dev/null | grep trusted.gfid | cut -d= -f2)
GFID_FORMATTED=$(echo "$GFID_RAW" | awk '{print substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')
GFID_LINK_B1="$B0/${V0}1/.glusterfs/$(echo $GFID_RAW | awk '{print substr($0,3,2)"/"substr($0,5,2)"/"substr($1,3,8)"-"substr($1,11,4)"-"substr($1,15,4)"-"substr($1,19,4)"-"substr($1,23,12)}')"

# Create a split-brain by downing a brick, and flipping the
# gfid on the down brick, then bring the brick back up.

# For good measure kill the first brick so the inode cache is wiped, we don't
# want any funny business
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST $CLI volume start $V0 force
pkill -f gluster/glustershd

rm -f $GFID_LINK_B1
TEST setfattr -n "trusted.gfid" -v "0xfd551a5cfddd4c1aa4d096ef09ef5c08" $B0/${V0}1/foo/splitfile
sleep 1
TEST touch $B0/${V0}1/foo/splitfile

mkdir -p $B0/${V0}1/.glusterfs/fd/55
ln $B0/${V0}1/foo/splitfile $B0/${V0}1/.glusterfs/fd/55/fd551a5c-fddd-4c1a-a4d0-96ef09ef5c08
cd ~

touch $B0/${V0}3/.glusterfs/indices/xattrop/$GFID_FORMATTED
touch $B0/${V0}3/.glusterfs/indices/xattrop/$GFID_PARENT_FORMATTED

TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
sleep 5

EXPECT_WITHIN 60 "0" get_pending_heal_count $V0

TEST stat $B0/${V0}1/foo/splitfile

cd $M0

# Tickle the file to trigger the gfid unsplit
TEST stat foo/splitfile
sleep 1

# Verify the file is readable
TEST dd if=foo/splitfile of=/dev/null 2>/dev/null

# Verify entry healing happened on the back-end regardless of the
# gfid-splitbrain state of the directory.
TEST stat $B0/${V0}1/foo/splitfile

# Verify the MD5 signature of the file
HEALED_MD5=$(md5sum foo/splitfile | cut -d\  -f1)
TEST [ "$MD5" == "$HEALED_MD5" ]

# Verify the file can be removed
TEST rm -f foo/splitfile
cd ~

cleanup
