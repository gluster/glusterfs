#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../common-utils.rc
. $(dirname $0)/../../dht.rc

# Test 1 overview:
# ----------------
# Test whether lookups are sent after a brick comes up again
#
# 1. Create a 3 brick pure distribute volume
# 2. Fuse mount the volume so the layout is set on the root
# 3. Kill one brick and try to create a directory which hashes to that brick.
#    It should fail with EIO.
# 4. Restart the brick that was killed.
# 5. Do not remount the volume. Try to create the same directory as in step 3.

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0-{1..3}
TEST $CLI volume start $V0

# We want the lookup to reach DHT
TEST $CLI volume set $V0 performance.stat-prefetch off

# Mount using FUSE and lookup the mount so a layout is set on the brick root
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0

ls $M0/

TEST mkdir $M0/level1

# Find a dirname that will hash to the brick we are going to kill
hashed=$V0-client-1
TEST dht_first_filename_with_hashsubvol "$hashed" $M0 "dir-"
roottestdir=$fn_return_val

hashed=$V0-client-1
TEST dht_first_filename_with_hashsubvol "$hashed" $M0/level1 "dir-"
level1testdir=$fn_return_val


TEST kill_brick $V0 $H0 $B0/$V0-2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status $V0 $H0 $B0/$V0-2

TEST $CLI volume status $V0


# Unmount and mount the volume again so dht has an incomplete in memory layout

umount -f $M0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0


mkdir $M0/$roottestdir
TEST [ $? -ne 0 ]

mkdir $M0/level1/$level1testdir
TEST [ $? -ne 0 ]

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/$V0-2

#$CLI volume status

# It takes a while for the client to reconnect to the brick
sleep 5


mkdir $M0/$roottestdir
TEST [ $? -eq 0 ]

mkdir $M0/$level1/level1testdir
TEST [ $? -eq 0 ]

# Cleanup
cleanup


