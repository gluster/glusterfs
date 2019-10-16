#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function is_gfapi_program_alive()
{
        pid=$1
        ps -p $pid
        if [ $? -eq 0 ]
        then
                echo "Y"
        else
                echo "N"
        fi
}

function get_active_lock_count {
    brick=$1
    sdump=$(generate_brick_statedump $V0 $H0 $brick)
    lock_count="$(grep ACTIVE $sdump| wc -l)"
    echo "$lock_count"
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 locks.mandatory-locking forced
TEST $CLI volume set $V0 enforce-mandatory-lock on
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`
TEST build_tester $(dirname $0)/afr-lock-heal-advanced.c -lgfapi -ggdb

#------------------------------------------------------------------------------
# Use more than 1 fd from same client so that list_for_each_* loops are executed more than once.
$(dirname $0)/afr-lock-heal-advanced $H0 $V0 "/FILE1" "/FILE2" $logdir C1&
client_pid=$!
TEST [ $client_pid ]

TEST sleep 5 # By now, the client would  have opened an fd on FILE1 and FILE2 and waiting for a SIGUSR1.
EXPECT "Y" is_gfapi_program_alive $client_pid

# Kill brick-3 and let client-1 take lock on both files.
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill -SIGUSR1 $client_pid
# If program is still alive, glfs_file_lock() was a success.
EXPECT "Y" is_gfapi_program_alive $client_pid

# Check lock is present on brick-1 and brick-2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_active_lock_count $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_active_lock_count $B0/${V0}1

# Restart brick-3 and check that the lock has healed on it.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
TEST sleep 10 #Needed for client to re-open fd? Otherwise client_pre_lk_v2() fails with EBADFD for remote-fd.

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_active_lock_count $B0/${V0}2

#------------------------------------------------------------------------------
# Kill same brick before heal completes the first time and check it completes the second time.
TEST $CLI volume set $V0 delay-gen locks
TEST $CLI volume set $V0 delay-gen.delay-duration 5000000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable finodelk

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST $CLI volume reset $V0 delay-gen
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_active_lock_count $B0/${V0}0

#------------------------------------------------------------------------------
# Kill 2 bricks and bring it back. The fds must be marked bad.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1

# TODO: `gluster v statedump $V0 client localhost:$client_pid` is not working,
# so sleep for 20 seconds for the client to connect to connect to the bricks.
TEST sleep $CHILD_UP_TIMEOUT

# Try to write to FILE1 from the .c; it must fail.
TEST kill -SIGUSR1 $client_pid
wait $client_pid
ret=$?
TEST [ $ret == 0 ]

cleanup_tester $(dirname $0)/afr-lock-heal-advanced
cleanup;
