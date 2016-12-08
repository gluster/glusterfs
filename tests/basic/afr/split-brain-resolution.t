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

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

TEST `echo "some-data" > $M0/data-split-brain.txt`
TEST `echo "some-data" > $M0/metadata-split-brain.txt`

#Create data and metadata split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0

TEST `echo "brick1_alive" > $M0/data-split-brain.txt`
TEST setfattr -n user.test -v brick1 $M0/metadata-split-brain.txt

TEST $CLI volume start $V0 force
TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0

TEST `echo "brick0_alive" > $M0/data-split-brain.txt`
TEST setfattr -n user.test -v brick0 $M0/metadata-split-brain.txt

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

EXPECT 4 get_pending_heal_count $V0

TEST ! cat $M0/data-split-brain.txt
TEST ! getfattr -n user.test $M0/metadata-split-brain.txt

#Inspect file in data-split-brain
EXPECT "data-split-brain:yes metadata-split-brain:no Choices:patchy-client-0,patchy-client-1" get_split_brain_status $M0/data-split-brain.txt
TEST setfattr -n replica.split-brain-choice -v $V0-client-0 $M0/data-split-brain.txt

#Should now be able to read the contents of data-split-brain.txt
EXPECT "brick0_alive" cat $M0/data-split-brain.txt

TEST setfattr -n replica.split-brain-choice-timeout -v 10 $M0/
TEST setfattr -n replica.split-brain-choice -v $V0-client-1 $M0/data-split-brain.txt

#Should now be able to read the contents of data-split-brain.txt
EXPECT "brick1_alive" cat $M0/data-split-brain.txt

#Inspect the file in metadata-split-brain
EXPECT "data-split-brain:no metadata-split-brain:yes Choices:patchy-client-0,patchy-client-1" get_split_brain_status $M0/metadata-split-brain.txt
TEST setfattr -n replica.split-brain-choice -v $V0-client-0 $M0/metadata-split-brain.txt

EXPECT "brick0" get_text_xattr user.test $M0/metadata-split-brain.txt

TEST setfattr -n replica.split-brain-choice -v $V0-client-1 $M0/metadata-split-brain.txt
EXPECT "brick1" get_text_xattr user.test $M0/metadata-split-brain.txt

#Check that setting split-brain-choice to "none" results in EIO again
TEST setfattr -n replica.split-brain-choice -v none $M0/metadata-split-brain.txt
TEST setfattr -n replica.split-brain-choice -v none $M0/data-split-brain.txt
TEST ! getfattr -n user.test $M0/metadata-split-brain.txt
TEST ! cat $M0/data-split-brain.txt

#Negative test cases should fail
TEST ! setfattr -n replica.split-brain-choice -v $V0-client-4 $M0/data-split-brain.txt
TEST ! setfattr -n replica.split-brain-heal-finalize -v $V0-client-4 $M0/metadata-split-brain.txt

#Heal the files
TEST setfattr -n replica.split-brain-heal-finalize -v $V0-client-0 $M0/metadata-split-brain.txt
TEST setfattr -n replica.split-brain-heal-finalize -v $V0-client-1 $M0/data-split-brain.txt

EXPECT "brick0" get_text_xattr user.test $M0/metadata-split-brain.txt
EXPECT "brick1_alive" cat $M0/data-split-brain.txt

EXPECT 0 get_pending_heal_count $V0

cleanup;
