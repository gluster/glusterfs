#!/bin/bash

## The script tests the logging of the quota in the bricks after reaching soft
## limit of the configured limit.
##
##  Steps:
##  1. Create and mount the volume
##  2. Enable quota and set the limit on 2 directories
##  3. Write some data to cross the limit
##  4. Grep the string expected in brick logs
##  5. Wait for 10 seconds (alert timeout is set to 10s)
##  6. Repeat 3 and 4.
##  7. Cleanup

. $(dirname $0)/../include.rc
. $(dirname $0)/../fileio.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;

#1
## Step 1
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick{1..4};
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 noac,nolock


QUOTA_LIMIT_DIR="quota_limit_dir"
BRICK_LOG_DIR="`gluster --print-logdir`/bricks"

#9
TEST mkdir $N0/$QUOTA_LIMIT_DIR

#10
## Step 2
TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 alert-time 10
TEST $CLI volume quota $V0 hard-timeout 0
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 limit-usage / 200KB
TEST $CLI volume quota $V0 limit-usage /$QUOTA_LIMIT_DIR 100KB

#16
## Step 3 and 4
TEST dd if=/dev/urandom of=$N0/$QUOTA_LIMIT_DIR/95KB_file bs=1k count=95
TEST grep -e "\"Usage crossed soft limit:.*used by /$QUOTA_LIMIT_DIR\"" -- $BRICK_LOG_DIR/*

TEST dd if=/dev/urandom of=$N0/100KB_file bs=1k count=100
TEST grep -e "\"Usage crossed soft limit:.*used by /\"" -- $BRICK_LOG_DIR/*

#20
## Step 5
TEST sleep 10

## Step 6
TEST dd if=/dev/urandom of=$N0/$QUOTA_LIMIT_DIR/1KB_file bs=1k count=1
TEST grep -e "\"Usage is above soft limit:.*used by /$QUOTA_LIMIT_DIR\"" -- $BRICK_LOG_DIR/*

#23
TEST dd if=/dev/urandom of=$N0/1KB_file bs=1k count=1
TEST grep -e "\"Usage is above soft limit:.*used by /\"" -- $BRICK_LOG_DIR/*

#25
## Step 7
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;
