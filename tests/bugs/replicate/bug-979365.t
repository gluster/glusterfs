#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This script checks that ensure-durability option enables/disables afr
#sending fsyncs
cleanup;

function num_fsyncs {
        $CLI volume profile $V0 info | grep -w FSYNC | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 ensure-durability on
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd of=$M0/a if=/dev/zero bs=1024k count=10
#fsyncs take a while to complete.
sleep 5

# There can be zero or more fsyncs, depending on the order
# in which the writes reached the server, in turn deciding
# whether they were treated as "appending" writes or not.

TEST [[ $(num_fsyncs) -ge 0 ]]
#Stop the volume to erase the profile info of old operations
TEST $CLI volume profile $V0 stop
TEST $CLI volume stop $V0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
#Disable ensure-durability now to disable fsyncs in afr.
TEST $CLI volume set $V0 ensure-durability off
TEST $CLI volume start $V0
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $CLI volume profile $V0 start
TEST dd of=$M0/a if=/dev/zero bs=1024k count=10
#fsyncs take a while to complete.
sleep 5
TEST [[ $(num_fsyncs) -eq 0 ]]

cleanup
