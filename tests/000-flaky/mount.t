#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

#G_TESTDEF_TEST_STATUS_CENTOS6=NFS_TEST

cleanup;

## Start and create a volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6,7,8,9};
TEST $CLI volume set $V0 nfs.disable false

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';


## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';


## Make volume tightly consistent for metdata
TEST $CLI volume set $V0 performance.stat-prefetch off;

## Mount FUSE with caching disabled (read-write)
TEST $GFS -s $H0 --volfile-id $V0 $M0;

## Check consistent "rw" option
TEST 'mount -t $MOUNT_TYPE_FUSE | grep -E "^$H0:$V0 "|$GREP_MOUNT_OPT_RW';
TEST 'grep -E "^$H0:$V0 .+ ,?rw,?" /proc/mounts';

## Mount FUSE with caching disabled (read-only)
TEST $GFS --read-only -s $H0 --volfile-id $V0 $M1;

## Check consistent "ro" option
TEST 'mount -t $MOUNT_TYPE_FUSE | grep -E "^$H0:$V0 "|$GREP_MOUNT_OPT_RO';
TEST 'grep -E "^$H0:$V0 .+ ,?ro(,.+)?" /proc/mounts';

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

## Mount NFS
TEST mount_nfs $H0:/$V0 $N0 nolock;

## Test for consistent views between NFS and FUSE mounts
## write access to $M1 should fail
TEST ! stat $M0/newfile;
TEST ! touch $M1/newfile;
TEST touch $M0/newfile;
TEST stat $M1/newfile;
TEST stat $N0/newfile;
TEST ! rm -f $M1/newfile;
TEST rm -f $N0/newfile;
TEST ! stat $M0/newfile;
TEST ! stat $M1/newfile;

# No need to check for status here right now
$(dirname $0)/rpc-coverage.sh $N0 >/dev/null

## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
