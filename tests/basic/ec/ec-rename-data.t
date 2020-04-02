#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

function num_entries {
	ls $1 | wc -l
}

function check_hidden ()
{
    if [[ -d "$B0/$1/$hidden_dir" ]]
    then
        echo "Y"
    else
        echo "N"
    fi
}

function is_hidden_empty ()
{
    local ret;

    ret=$(ls -a "$B0/$1/$hidden_dir/" | wc -l)
    if [[ $ret -eq 2 ]]
    then
        echo "Y"
    else
        echo "N"
    fi
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 write-behind off
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
TEST $CLI volume heal $V0 disable
TEST $CLI volume set $V0 disperse.use-anonymous-inode on
#TEST $CLI volume set $V0 cluster.heal-timeout 60
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

cd $M0
TEST touch file1
TEST touch file2
TEST 'echo "line1" >> file1'
TEST mkdir -p dir1/dira/dirb
TEST 'echo "line1">>dir1/dira/dirb/file1'
TEST mkdir delete_me
TEST 'echo "line1" >> delete_me/file1'

TEST 'echo "line2" >> file1'
TEST 'echo "line2" >> dir1/dira/dirb/file1'
TEST 'echo "line2" >> delete_me/file1'

#Kill brick1
TEST kill_brick $V0 $H0 $B0/${V0}1

mv file1 file2
mv dir1/dira dir2

#Delete a dir when brick1 is down.
rm -rf delete_me
cd -

#Bring everything up and trigger heal
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status

TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^2$" get_pending_heal_count $V0

#Test that heal worked as expected by forcing read from brick0
#remount to make sure data is not served from any cache
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
EXPECT "line2" tail -1 $M0/file2
EXPECT "line2" tail -1 $M0/dir2/dirb/file1
TEST ! stat $M0/delete_me/file1
TEST ! stat $M0/delete_me

hidden_dir=$(ls -a $B0/${V0}0 | grep "glusterfs-anonymous-inode")

EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_hidden "${V0}0"
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_hidden "${V0}1"
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_hidden "${V0}2"

EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_hidden_empty "${V0}0"
EXPECT_WITHIN $HEAL_TIMEOUT "N" is_hidden_empty "${V0}1"
EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_hidden_empty "${V0}2"

cleanup
