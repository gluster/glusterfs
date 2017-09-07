#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../dht.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3  $H0:$B0/${V0}{1,2,3,4,5,6};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '6' brick_count $V0

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 storage.max-hardlinks 3
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST dd if=/dev/zero of=$M0/testfile count=1

# max-hardlinks is 3, should be able to create 2 links.
TEST link $M0/testfile $M0/testfile.link1
TEST link $M0/testfile $M0/testfile.link2

# But not 3.
TEST ! link $M0/testfile $M0/testfile.link3
# If we remove one...
TEST rm $M0/testfile.link1
# Now we can add one.
TEST link $M0/testfile $M0/testfile.link3

# But not another
TEST ! link $M0/testfile $M0/testfile.link4

# Unless we disable the limit...
TEST $CLI volume set $V0 storage.max-hardlinks 0
TEST link $M0/testfile $M0/testfile.link4

cleanup;
