#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

function get_split_brain_status {
        local path=$1
        echo `getfattr -n replica.split-brain-status $path` | cut -f2 -d"=" | sed -e 's/^"//'  -e 's/"$//'
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0

#Disable self-heal-daemon
TEST $CLI volume set $V0 cluster.self-heal-daemon off

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

TEST `echo "some-data" > $M0/metadata-split-brain.txt`

#Create metadata split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST chmod 666 $M0/metadata-split-brain.txt

TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

TEST chmod 757 $M0/metadata-split-brain.txt

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

EXPECT 2 get_pending_heal_count $V0

#Inspect the file in metadata-split-brain
EXPECT "data-split-brain:no metadata-split-brain:yes Choices:patchy-client-0,patchy-client-1" get_split_brain_status $M0/metadata-split-brain.txt
TEST setfattr -n replica.split-brain-choice -v $V0-client-0 $M0/metadata-split-brain.txt

EXPECT "757" stat -c %a $M0/metadata-split-brain.txt

TEST setfattr -n replica.split-brain-choice -v $V0-client-1 $M0/metadata-split-brain.txt
EXPECT "666" stat -c %a $M0/metadata-split-brain.txt

cleanup;
