#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

# Setup a cluster with 3 replicas, and fav child by majority on
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3};
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 cluster.quorum-type none
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 nfs.disable off
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

dd if=/dev/urandom of=$M0/splitfile bs=128k count=5 2>/dev/null

MD5=$(md5sum $M0/splitfile | cut -d\  -f1)

# Create a split-brain by downing a brick, and flipping the
# gfid on the down brick, then bring the brick back up.
TEST kill_brick $V0 $H0 $B0/${V0}1
GFID_DIR_B1="$B0/${V0}1/.glusterfs/$(getfattr -n trusted.gfid -e hex $B0/${V0}1/splitfile 2>/dev/null | grep ^trusted | cut -d= -f2 | awk '{print substr($0,3,2)}')"
rm -rf $GFID_DIR_B1
mkdir -p $B0/${V0}1/.glusterfs/fd/55
ln $B0/${V0}1/splitfile $B0/${V0}1/.glusterfs/fd/55/fd551a5c-fddd-4c1a-a4d0-96ef09ef5c08
TEST setfattr -n "trusted.gfid" -v "0xfd551a5cfddd4c1aa4d096ef09ef5c08" $B0/${V0}1/splitfile

GFID_DIR_B3="$B0/${V0}3/.glusterfs/$(getfattr -n trusted.gfid -e hex $B0/${V0}3/splitfile 2>/dev/null | grep ^trusted | cut -d= -f2 | awk '{print substr($0,3,2)}')"
#EST rm -f $B0/${V0}3/splitfile
#m -rf $GFID_DIR_B3

touch $M0/newfile

# Synthetically force a conservative merge of the directory.  We want
# to ensure that conservative merges happen in-spite of GFID mis-matches,
# since we can handle them there's no sense in not doing these.  In fact,
# if we stop them it will block GFID split-brain resolution.
setfattr -n trusted.afr.patchy-client-1 -v 0x000000000000000000000002 $B0/${V0}1
setfattr -n trusted.afr.patchy-client-2 -v 0x000000000000000000000002 $B0/${V0}1

# Restart the down brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
sleep 5

# Tickle the file to trigger the gfid unsplit
TEST stat $M0/splitfile
sleep 1

# Verify the file is readable
TEST dd if=$M0/splitfile of=/dev/null 2>/dev/null

# Verify entry healing happened on the back-end regardless of the
# gfid-splitbrain state of the directory.
TEST stat $B0/${V0}1/splitfile

# Verify the MD5 signature of the file
HEALED_MD5=$(md5sum $M0/splitfile | cut -d\  -f1)
TEST [ "$MD5" == "$HEALED_MD5" ]

# Verify the file can be removed
TEST rm -f $M0/splitfile

# Part II: NFS test
TEST mount_nfs $H0:/$V0 $N0 nolock
#EST mount -t nfs -o nolock,noatime,noacl,soft,intr $H0:/$V0 $N0;

dd if=/dev/urandom of=$N0/splitfile bs=128k count=5 2>/dev/null

MD5=$(md5sum $N0/splitfile | cut -d\  -f1)

# Create a split-brain by downing a brick, and flipping the
# gfid on the down brick, then bring the brick back up.
TEST kill_brick $V0 $H0 $B0/${V0}1
GFID_DIR_B1="$B0/${V0}1/.glusterfs/$(getfattr -n trusted.gfid -e hex $B0/${V0}1/splitfile 2>/dev/null | grep ^trusted | cut -d= -f2 | awk '{print substr($0,3,2)}')"
rm -rf $GFID_DIR_B1
TEST setfattr -n "trusted.gfid" -v "0xfd551a5cfddd4c1aa4d096ef09ef5c08" $B0/${V0}1/splitfile

GFID_DIR_B3="$B0/${V0}3/.glusterfs/$(getfattr -n trusted.gfid -e hex $B0/${V0}3/splitfile 2>/dev/null | grep ^trusted | cut -d= -f2 | awk '{print substr($0,3,2)}')"
#EST rm -f $B0/${V0}3/splitfile
#m -rf $GFID_DIR_B3

# Restart the down brick
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
sleep 5

# Tickle the file to trigger the gfid unsplit
TEST stat $N0/splitfile
sleep 1

# Verify the file is readable
TEST dd if=$N0/splitfile of=/dev/null 2>/dev/null

# Verify the MD5 signature of the file
HEALED_MD5=$(md5sum $N0/splitfile | cut -d\  -f1)
TEST [ "$MD5" == "$HEALED_MD5" ]

# Verify the file can be removed
TEST rm -f $N0/splitfile

cleanup
