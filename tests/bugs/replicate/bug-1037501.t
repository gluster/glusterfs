#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function write_file()
{
	path="$1"; shift
	echo "$*" > "$path"
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
mkdir -p ${B0}/${V0}-2
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}-{0,1,2}

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

TEST `echo "TEST-FILE" > $M0/File`
TEST `mkdir $M0/Dir`
TEST `ln  $M0/File $M0/Link`
TEST `mknod $M0/FIFO p`

TEST $CLI volume add-brick $V0 replica 4 $H0:$B0/$V0-3 force
TEST $CLI volume add-brick $V0 replica 5 $H0:$B0/$V0-4 force
TEST $CLI volume add-brick $V0 replica 6 $H0:$B0/$V0-5 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 4
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 5
TEST gluster volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-0/File
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-1/File
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-2/File
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-3/File
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-4/File
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-5/File

EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-0/Link
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-1/Link
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-2/Link
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-3/Link
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-4/Link
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-5/Link

EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-0/Dir
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-1/Dir
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-2/Dir
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-3/Dir
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-4/Dir
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-5/Dir

EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-0/FIFO
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-1/FIFO
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-2/FIFO
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-3/FIFO
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-4/FIFO
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/$V0-5/FIFO

EXPECT 10 stat -c '%s' $B0/$V0-0/File
EXPECT 10 stat -c '%s' $B0/$V0-1/File
EXPECT 10 stat -c '%s' $B0/$V0-2/File
EXPECT 10 stat -c '%s' $B0/$V0-3/File
EXPECT 10 stat -c '%s' $B0/$V0-4/File
EXPECT 10 stat -c '%s' $B0/$V0-5/File

EXPECT 3 stat -c '%h' $B0/$V0-0/Link
EXPECT 3 stat -c '%h' $B0/$V0-1/Link
EXPECT 3 stat -c '%h' $B0/$V0-2/Link
EXPECT 3 stat -c '%h' $B0/$V0-3/Link
EXPECT 3 stat -c '%h' $B0/$V0-4/Link
EXPECT 3 stat -c '%h' $B0/$V0-5/Link

EXPECT 'directory' stat -c '%F' $B0/$V0-0/Dir
EXPECT 'directory' stat -c '%F' $B0/$V0-1/Dir
EXPECT 'directory' stat -c '%F' $B0/$V0-2/Dir
EXPECT 'directory' stat -c '%F' $B0/$V0-3/Dir
EXPECT 'directory' stat -c '%F' $B0/$V0-4/Dir
EXPECT 'directory' stat -c '%F' $B0/$V0-5/Dir

EXPECT 'fifo' stat -c '%F' $B0/$V0-0/FIFO
EXPECT 'fifo' stat -c '%F' $B0/$V0-1/FIFO
EXPECT 'fifo' stat -c '%F' $B0/$V0-2/FIFO
EXPECT 'fifo' stat -c '%F' $B0/$V0-3/FIFO
EXPECT 'fifo' stat -c '%F' $B0/$V0-4/FIFO
EXPECT 'fifo' stat -c '%F' $B0/$V0-5/FIFO

cleanup;
