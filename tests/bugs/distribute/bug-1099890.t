#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

## TO-DO: Fix the following once the dht du refresh interval issue is fixed:
## 1. Do away with sleep(1).
## 2. Do away with creation of empty files.

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/../../basic/quota.c -o $QDD

TEST   glusterd;
TEST   pidof glusterd;

# Create 2 loop devices, one per brick.
TEST   truncate -s 100M $B0/brick1
TEST   truncate -s 100M $B0/brick2

TEST   L1=`SETUP_LOOP $B0/brick1`
TEST   MKFS_LOOP $L1

TEST   L2=`SETUP_LOOP $B0/brick2`
TEST   MKFS_LOOP $L2

TEST   mkdir -p $B0/${V0}{1,2}

TEST   MOUNT_LOOP $L1 $B0/${V0}1
TEST   MOUNT_LOOP $L2 $B0/${V0}2

# Create a plain distribute volume with 2 subvols.
TEST   $CLI volume create $V0 $H0:$B0/${V0}{1,2};

TEST   $CLI volume start $V0;
EXPECT "Started" volinfo_field $V0 'Status';

TEST   $CLI volume quota $V0 enable;

TEST   $CLI volume set $V0 features.quota-deem-statfs on

TEST   $CLI volume quota $V0 limit-usage / 150MB;

TEST   $CLI volume set $V0 cluster.min-free-disk 50%

TEST   glusterfs -s $H0 --volfile-id=$V0 $M0

# Make sure quota-deem-statfs is working as expected
EXPECT "150M" echo `df -h $M0 -P | tail -1 | awk {'print $2'}`

# Create a new file 'foo' under the root of the volume, which hashes to subvol-0
# of DHT, that consumes 40M
TEST $QDD $M0/foo 256 160

TEST   stat $B0/${V0}1/foo
TEST ! stat $B0/${V0}2/foo

# Create a new file 'bar' under the root of the volume, which hashes to subvol-1
# of DHT, that consumes 40M
TEST $QDD $M0/bar 256 160

TEST ! stat $B0/${V0}1/bar
TEST   stat $B0/${V0}2/bar

# Touch a zero-byte file on the root of the volume to make sure the statfs data
# on DHT is refreshed
sleep 1;
TEST   touch $M0/empty1;

# At this point, the available space on each subvol {60M,60M} is greater than
# their min-free-disk {50M,50M}, but if this bug still exists, then
# the total available space on the volume as perceived by DHT should be less
# than min-free-disk, i.e.,
#
# consumed space returned per subvol by quota = (40M + 40M) = 80M
#
# Therefore, consumed space per subvol computed by DHT WITHOUT the fix would be:
# (80M/150M)*100 = 53%
#
# Available space per subvol as perceived by DHT with the bug = 47%
# which is less than min-free-disk

# Now I create a file that hashes to subvol-1 (counting from 0) of DHT.
# If this bug still exists,then DHT should be routing this creation to subvol-0.
# If this bug is fixed, then DHT should be routing the creation to subvol-1 only
# as it has more than min-free-disk space available.

TEST $QDD $M0/file 1 1
sleep 1;
TEST ! stat $B0/${V0}1/file
TEST   stat $B0/${V0}2/file

# Touch another zero-byte file on the root of the volume to refresh statfs
# values stored by DHT.

TEST touch $M0/empty2;

# Now I create a new file that hashes to subvol-0, at the end of which, there
# will be less than min-free-disk space available on it.
TEST $QDD $M0/fil 256 80
sleep 1;
TEST   stat $B0/${V0}1/fil
TEST ! stat $B0/${V0}2/fil

# Touch to refresh statfs info cached by DHT

TEST   touch $M0/empty3;

# Now I create a file that hashes to subvol-0 but since it has less than
# min-free-disk space available, its data will be cached on subvol-1.

TEST $QDD $M0/zz 256 20

TEST   stat $B0/${V0}1/zz
TEST   stat $B0/${V0}2/zz

EXPECT "$V0-client-1" dht_get_linkto_target "$B0/${V0}1/zz"

EXPECT "1" is_dht_linkfile "$B0/${V0}1/zz"

force_umount $M0
TEST $CLI volume stop $V0
EXPECT "1" get_aux
UMOUNT_LOOP ${B0}/${V0}{1,2}
rm -f ${B0}/brick{1,2}

rm -f $QDD
cleanup
