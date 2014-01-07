#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

cleanup;

#1
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
EXPECT_WITHIN 20 1 is_nfs_export_available
TEST mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0 $N0
TEST umount $N0

# based on ip addresses (1-4)
# case 1: allow only localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-allow 127.0.0.1
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 2: allow only non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-allow 192.168.1.1
EXPECT_WITHIN 20 1 is_nfs_export_available
#11
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST $CLI volume reset --mode=script $V0
# case 3: reject only localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-reject 127.0.0.1
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 4: reject only non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.168.1.1
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0



# NEED TO CHECK BOTH IP AND NAME BASED AUTH.
# CASES WITH NFS.ADDR-NAMELOOKUP ON (5-12)
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
EXPECT_WITHIN 20 1 is_nfs_export_available
#20
TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 5: allow only localhost
TEST $CLI volume set $V0 nfs.rpc-auth-allow localhost
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# case 6: allow only somehost
TEST $CLI volume set $V0 nfs.rpc-auth-allow somehost
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 7: reject only localhost
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-reject localhost
EXPECT_WITHIN 20 1 is_nfs_export_available
#30
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 8: reject only somehost
TEST $CLI volume set $V0 nfs.rpc-auth-reject somehost
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

# based on ip addresses: repeat of cases 1-4
# case 9: allow only localhost ip
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-allow 127.0.0.1
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST mkdir -p $N0/subdir
TEST umount $N0

# case 10: allow a non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-allow 192.168.1.1
EXPECT_WITHIN 20 1 is_nfs_export_available
#41
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0

# case 11: reject only localhost ip
TEST $CLI volume reset --mode=script $V0
TEST $CLI volume set $V0 nfs.addr-namelookup on
TEST $CLI volume set $V0 nfs.rpc-auth-reject 127.0.0.1
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST ! mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0/subdir $N0

# case 12: reject only non-localhost ip
TEST $CLI volume set $V0 nfs.rpc-auth-reject 192.168.1.1
EXPECT_WITHIN 20 1 is_nfs_export_available

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0 $N0
TEST umount $N0

TEST mount -t nfs -o vers=3,nolock,soft,intr localhost:/$V0/subdir $N0
TEST umount $N0

TEST $CLI volume stop --mode=script $V0
#52
TEST $CLI volume delete --mode=script $V0
cleanup
