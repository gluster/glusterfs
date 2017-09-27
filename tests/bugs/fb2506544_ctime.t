#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info 2> /dev/null;

# Setup a cluster with 3 replicas, and fav child by majority on
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3};
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 1
TEST $CLI volume start $V0
sleep 5

# Mount the volume
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0

# Write some random data into a file
dd if=/dev/urandom of=$M0/splitfile bs=128k count=5 2>/dev/null

# Create a split-brain by downing a brick, writing some data
# then downing the other two, write some more data and bring
# everything back up.
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/splitfile bs=128k count=5 oflag=append 2>/dev/null

TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0

TEST dd if=/dev/urandom of=$M0/splitfile bs=128k count=5 oflag=append 2>/dev/null
CTIME_MD5=$(md5sum $M0/splitfile | cut -d\  -f1)

# Bring all bricks back up.
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 1
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 2

# First do a test to prove the file is splitbrained without
# favorite-child support.
umount $M0
# Mount the volume
TEST glusterfs --log-level DEBUG --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0
sleep 1

#EST ! md5sum $M0/splitfile

# Ok now turn the favorite-child option and we should be able to read it.
# The MD5 should be of the file which was created first.
umount $M0
TEST $CLI volume set $V0 cluster.favorite-child-by-ctime on
sleep 1
# Mount the volume
TEST glusterfs --log-level DEBUG --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0
sleep 2

HEALED_MD5=$(md5sum $M0/splitfile | cut -d\  -f1)
TEST [ "$CTIME_MD5" == "$HEALED_MD5" ]

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info 2> /dev/null;

# Setup a cluster with 3 replicas, and fav child by majority on
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..3};
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal on
TEST $CLI volume set $V0 cluster.metadata-self-heal on
TEST $CLI volume set $V0 cluster.entry-self-heal on
TEST $CLI volume set $V0 cluster.quorum-type fixed
TEST $CLI volume set $V0 cluster.quorum-count 1
TEST $CLI volume start $V0
sleep 5

# Mount the volume
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0

mkdir $M0/d
# Write some random data into a file
dd if=/dev/urandom of=$M0/d/splitfile bs=128k count=5 2>/dev/null

# Create a split-brain by downing a brick, writing some data
# then downing the other two, write some more data and bring
# everything back up.
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/d/splitfile bs=128k count=5 oflag=append 2>/dev/null

TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}3
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0

TEST dd if=/dev/urandom of=$M0/d/splitfile bs=128k count=5 oflag=append 2>/dev/null
CTIME_MD5=$(md5sum $M0/d/splitfile | cut -d\  -f1)

# Bring all bricks back up.
TEST $CLI volume start $V0 force
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 1
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 2

# First do a test to prove the file is splitbrained without
# favorite-child support.
umount $M0
# Mount the volume
TEST glusterfs --log-level DEBUG --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0
sleep 1

#EST ! md5sum $M0/d/splitfile

# Ok now turn the favorite-child option and we should be able to read it.
# The MD5 should be of the file which was created first.
umount $M0
TEST $CLI volume set $V0 cluster.favorite-child-by-ctime on
TEST $CLI volume set $V0 cluster.self-heal-daemon on
sleep 1
/etc/init.d/glusterd restart_shd
EXPECT_WITHIN 60 "0" get_pending_heal_count $V0
sleep 1

# Mount the volume
TEST glusterfs --log-level DEBUG --volfile-id=/$V0 --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0
sleep 2

HEALED_MD5=$(md5sum $M0/d/splitfile | cut -d\  -f1)
TEST [ "$CTIME_MD5" == "$HEALED_MD5" ]

cleanup
