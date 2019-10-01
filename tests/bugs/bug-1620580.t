#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

## Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs -s $H0 --volfile-id=$V0 $M0

#do some operation on mount, so that kill_brick is guaranteed to be
#done _after_ first lookup on root

TEST ls $M0
TEST touch $M0/file

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

# Case of Same volume name, but different bricks
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{3,4};
TEST $CLI volume start $V0;

# Give time for 'reconnect' to happen
sleep 4

TEST ! ls $M0
TEST ! touch $M0/file1

# Case of Same brick, but different volume (ie, recreated).
TEST $CLI volume create $V1 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V1;

# Give time for 'reconnect' to happen
sleep 4
TEST ! ls $M0
TEST ! touch $M0/file2

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
TEST $CLI volume stop $V1
TEST $CLI volume delete $V1

# Case of Same brick, but different volume (but same volume name)
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}
TEST $CLI volume start $V0;

# Give time for 'reconnect' to happen
sleep 4
TEST ! ls $M0
TEST ! touch $M0/file3


cleanup
