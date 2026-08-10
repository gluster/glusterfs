#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

cleanup

TEST glusterd
TEST pidof glusterd

# Give each brick independent capacity so one hashed destination can cross
# min-free-disk while another remains a valid redirected destination.
TEST truncate -s 512M $B0/brick-device-{0,1,2}

TEST L0=$(SETUP_LOOP $B0/brick-device-0)
TEST MKFS_LOOP $L0
TEST L1=$(SETUP_LOOP $B0/brick-device-1)
TEST MKFS_LOOP $L1
TEST L2=$(SETUP_LOOP $B0/brick-device-2)
TEST MKFS_LOOP $L2

TEST mkdir -p $B0/${V0}{0,1,2}
TEST MOUNT_LOOP $L0 $B0/${V0}0
TEST MOUNT_LOOP $L1 $B0/${V0}1
TEST MOUNT_LOOP $L2 $B0/${V0}2
TEST mkdir -p $B0/${V0}{0,1,2}/brick

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1,2}/brick
TEST $CLI volume start $V0
EXPECT "Started" volinfo_field $V0 'Status'

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

# Create on client-0, then rename to a name hashed on client-1.  This leaves
# the data on client-0 and a link file on client-1, which makes the file
# eligible for an explicit migration.
TEST dht_first_filename_with_hashsubvol "$V0-client-0" "$M0" source
source_name=$fn_return_val
TEST dht_first_filename_with_hashsubvol "$V0-client-1" "$M0" redirected
redirected_name=$fn_return_val
TEST dd if=/dev/zero of=$M0/$source_name bs=4096 count=1 status=none
TEST mv $M0/$source_name $M0/$redirected_name
TEST test -f $B0/${V0}0/brick/$redirected_name
EXPECT "$V0-client-0" dht_get_linkto_target \
    $B0/${V0}1/brick/$redirected_name

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Consume capacity outside the brick directory.  The file remains invisible
# to Gluster while statfs reports client-1 below the configured threshold.
TEST fallocate -l 350M $B0/${V0}1/reserved-space
TEST $CLI volume set $V0 cluster.min-free-disk 40%

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1

# Prime DHT's asynchronous disk-usage cache before requesting migration.
TEST touch $M0/refresh-disk-usage
TEST rm $M0/refresh-disk-usage
sleep 2

# Migration first locks client-1, then redirects the data to client-2 because
# client-1 is below min-free-disk.  Cleanup must unlock the original client-1
# target, not the reassigned data target.
TEST setfattr -n trusted.distribute.migrate-data -v force \
    $M0/$redirected_name
EXPECT "$V0-client-2" dht_get_linkto_target \
    $B0/${V0}1/brick/$redirected_name
TEST test -f $B0/${V0}2/brick/$redirected_name

# A leaked entry lock blocks this same-name operation from another client.
TEST timeout --kill-after=2 10 mv \
    $M1/$redirected_name $M1/$redirected_name-renamed

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1
TEST $CLI volume stop $V0
UMOUNT_LOOP $B0/${V0}{0,1,2}
cleanup
