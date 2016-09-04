#!/bin/bash

. $(dirname $0)/../../include.rc
#. $(dirname $0)/../../volume.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..2};

TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 600
TEST $CLI volume set $V0 performance.cache-invalidation on
TEST $CLI volume set $V0 performance.md-cache-timeout 600
TEST $CLI volume set $V0 performance.cache-samba-metadata on
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 read-subvolume $V0-client-0
TEST $CLI volume set $V0 performance.quick-read off

TEST $CLI volume start $V0

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

#Write some data from M0 and read it from M1,
#so that M1 selects a read subvol, and caches the lookup
TEST `echo "one" > $M0/file1`
EXPECT "one" cat $M1/file1

#Fail few writes from M0 on brick-0, as a result of this failure
#upcall in brick-0 will invalidate the read subvolume of M1.
TEST chattr +i $B0/${V0}1/file1
TEST `echo "two" > $M0/file1`
TEST `echo "three" > $M0/file1`
TEST `echo "four" > $M0/file1`
TEST `echo "five" > $M0/file1`

EXPECT_WITHIN $MDC_TIMEOUT "five" cat $M1/file1
TEST chattr -i $B0/${V0}1/file1
cleanup;
