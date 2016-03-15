#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

#This tests if cluster.eager-lock blocks metadata operations on nfs/fuse mounts.
#If it is not woken up, INODELK from the next command waits
#for post-op-delay secs.

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/r2_0 $H0:$B0/r2_1
TEST $CLI volume set $V0 ensure-durability off
TEST $CLI volume set $V0 cluster.eager-lock on
TEST $CLI volume set $V0 cluster.post-op-delay-secs 3
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock;
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0
echo 1 > $N0/1 && chmod +x $N0/1
echo 1 > $M0/1 && chmod +x $M0/1

#Check that INODELK MAX latency is not in the order of seconds
#Test if the MAX INODELK fop latency is of the order of seconds.
inodelk_max_latency=$($CLI volume profile $V0 info | grep INODELK | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{7,}")

TEST [ -z $inodelk_max_latency ]
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;
