#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

create_files () {
        for i in {1..10}; do
                orig=$(printf %s/file%04d $1 $i)
                echo "This is file $i" > $orig
        done
        for i in {1..10}; do
                mkdir $(printf %s/dir%04d $1 $i)
        done
        sync
}

create_dirs () {
        for i in {1..10}; do
                mkdir $(printf %s/dir%04d $1 $i)
                create_files $(printf %s/dir%04d $1 $i)
        done
        sync
}

stat_files () {
        for i in {1..10}; do
                orig=$(printf %s/file%04d $1 $i)
                stat $orig
        done
        for i in {1..10}; do
                stat $(printf %s/dir%04d $1 $i)
        done
        sync
}

stat_dirs () {
        for i in {1..10}; do
                stat $(printf %s/dir%04d $1 $i)
                stat_files $(printf %s/dir%04d $1 $i)
        done
        sync
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '4' brick_count $V0

TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0

# Create and poulate the NFS inode tables
TEST create_dirs $N0
TEST stat_dirs $N0

# add-bricks changing the state of the volume where some bricks
# would have some directories and others would not
TEST $CLI volume add-brick $V0 replica 2 $H0:$B0/${V0}{5,6,7,8}

# Post this dht_access was creating a mess for directories which is fixed
# with this commit. The issues could range from getting ENOENT or
# ESTALE or entries missing to directories not having complete
# layouts.
TEST cd $N0
TEST ls -lR

TEST $CLI volume rebalance $V0 start force
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

# tests to check post rebalance if layouts and entires are fine and
# accessible by NFS to clear the volume
TEST ls -lR
rm -rf ./*
# There are additional bugs where NFS+DHT does not delete all entries
# on an rm -rf, so we do an additional rm -rf to ensure all is done
# and we are facing this transient issue, rather than a bad directory
# layout that is cached in memory
TEST rm -rf ./*

# Get out of the mount, so that umount can work
TEST cd /

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
