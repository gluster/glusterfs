#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../common-utils.rc

# Test 1 overview:
# ----------------
#
# 1. Kill one brick of the volume.
# 2. Create directories and change directory properties.
# 3. Bring up the brick and access the directory
# 4. Check the permissions and xattrs on the backend

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0-{1..3}
TEST $CLI volume start $V0

# We want the lookup to reach DHT
TEST $CLI volume set $V0 performance.stat-prefetch off

# Mount using FUSE , kill a brick and create directories
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0

ls $M0/
cd $M0

TEST kill_brick $V0 $H0 $B0/$V0-1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status $V0 $H0 $B0/$V0-1

TEST mkdir dir{1..4}

# No change for dir1
# Change permissions for dir2
# Set xattr on dir3
# Change permissions and set xattr on dir4

TEST chmod 777 $M0/dir2

TEST setfattr -n "user.test" -v "test" $M0/dir3

TEST chmod 777 $M0/dir4
TEST setfattr -n "user.test" -v "test" $M0/dir4


# Start all bricks

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/$V0-1

#$CLI volume status

# It takes a while for the client to reconnect to the brick
sleep 5

stat $M0/dir* > /dev/null

# Check that directories have been created on the brick that was killed

TEST ls $B0/$V0-1/dir1

TEST ls $B0/$V0-1/dir2
EXPECT "777" stat -c "%a" $B0/$V0-1/dir2

TEST ls $B0/$V0-1/dir3
EXPECT "test" getfattr -n "user.test" --absolute-names --only-values $B0/$V0-1/dir3


TEST ls $B0/$V0-1/dir4
EXPECT "777" stat -c "%a" $B0/$V0-1/dir4
EXPECT "test" getfattr -n "user.test" --absolute-names --only-values $B0/$V0-1/dir4


TEST rm -rf $M0/*

cd

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0


# Test 2 overview:
# ----------------
# 1. Create directories with all bricks up.
# 2. Kill a brick and change directory properties and set user xattr.
# 2. Bring up the brick and access the directory
# 3. Check the permissions and xattrs on the backend


TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0

ls $M0/
cd $M0
TEST mkdir dir{1..4}

TEST kill_brick $V0 $H0 $B0/$V0-1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" brick_up_status $V0 $H0 $B0/$V0-1

# No change for dir1
# Change permissions for dir2
# Set xattr on dir3
# Change permissions and set xattr on dir4

TEST chmod 777 $M0/dir2

TEST setfattr -n "user.test" -v "test" $M0/dir3

TEST chmod 777 $M0/dir4
TEST setfattr -n "user.test" -v "test" $M0/dir4


# Start all bricks

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/$V0-1

#$CLI volume status

# It takes a while for the client to reconnect to the brick
sleep 5

stat $M0/dir* > /dev/null

# Check directories on the brick that was killed

TEST ls $B0/$V0-1/dir2
EXPECT "777" stat -c "%a" $B0/$V0-1/dir2

TEST ls $B0/$V0-1/dir3
EXPECT "test" getfattr -n "user.test" --absolute-names --only-values $B0/$V0-1/dir3


TEST ls $B0/$V0-1/dir4
EXPECT "777" stat -c "%a" $B0/$V0-1/dir4
EXPECT "test" getfattr -n "user.test" --absolute-names --only-values $B0/$V0-1/dir4
cd


# Cleanup
cleanup

