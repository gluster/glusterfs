#!/bin/bash
#This test checks if all the locks on
#ta file are being held and released properly

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../thin-arbiter.rc

function get_lock_count_on_ta()
{
    tapid=`cat $B0/ta.pid`
    local sfile=$(generate_statedump $tapid)
    count=$(grep "inodelk-count" $sfile | cut -f2 -d'=' | tail -1)
    ncount=$(grep "inodelk.inodelk" $sfile | grep "len=1" | wc -l)
    echo "count = $count : ncount = $ncount"
    if [ "$count" = "" ]
    then
        count=0
    fi

    if [ "$count" -eq "$ncount" ]
    then
        echo "$count"
    else
        echo "-1"
    fi
}

cleanup;
TEST ta_create_brick_and_volfile brick0
TEST ta_create_brick_and_volfile brick1
TEST ta_create_ta_and_volfile ta
TEST ta_start_brick_process brick0
TEST ta_start_brick_process brick1
TEST ta_start_ta_process ta

TEST ta_create_mount_volfile brick0 brick1 ta
TEST ta_start_mount_process $M0
TEST ta_start_mount_process $M1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" ta_up_status $V0 $M1 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "trusted.afr.patchy-ta-2" ls $B0/ta

TEST ta_create_shd_volfile brick0 brick1 ta
TEST ta_start_shd_process glustershd
shd_pid=$(cat $B0/glustershd.pid)

TEST touch $M0/a.txt
echo "Hello" >> $M0/a.txt
EXPECT_WITHIN $IO_WAIT_TIMEOUT "0" get_lock_count_on_ta

TEST ta_kill_brick brick0
echo "Hello" >> $M0/a.txt
EXPECT_WITHIN $IO_WAIT_TIMEOUT "1" get_lock_count_on_ta

echo "Hello" >> $M1/a.txt
EXPECT_WITHIN $IO_WAIT_TIMEOUT "2" get_lock_count_on_ta

echo "xyz" >> $M0/a.txt
EXPECT_WITHIN $IO_WAIT_TIMEOUT "2" get_lock_count_on_ta

chmod 0666 $M0/a.txt
EXPECT_WITHIN $IO_WAIT_TIMEOUT "2" get_lock_count_on_ta

TEST ta_start_brick_process brick0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_lock_count_on_ta

cleanup;
