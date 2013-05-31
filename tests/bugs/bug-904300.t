#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

cleanup;

# 1-8
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0;
TEST $CLI volume start $V0
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0 $N0
TEST mkdir $N0/dir1
TEST umount $N0

#
# Case 1: Allow "dir1" to be mounted only from 127.0.0.1
# 9-12
TEST $CLI volume set $V0 export-dir \""/dir1(127.0.0.1)"\"
EXPECT_WITHIN 20 2 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0/dir1 $N0
TEST umount $N0

#
# Case 2: Allow "dir1" to be mounted only from 8.8.8.8. This is
#         a negative test case therefore the mount should fail.
# 13-16
TEST $CLI volume set $V0 export-dir \""/dir1(8.8.8.8)"\"
EXPECT_WITHIN 20 2 is_nfs_export_available

TEST ! mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0/dir1 $N0
TEST ! umount $N0


# Case 3: Variation of test case1. Here we are checking with hostname
#         instead of ip address.
# 17-20
TEST $CLI volume set $V0 export-dir \""/dir1($H0)"\"
EXPECT_WITHIN 20 2 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0/dir1 $N0
TEST umount $N0

# Case 4: Variation of test case1. Here we are checking with IP range
# 21-24
TEST $CLI volume set $V0 export-dir \""/dir1(127.0.0.0/24)"\"
EXPECT_WITHIN 20 2 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0/dir1 $N0
TEST umount $N0

## Finish up
TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
