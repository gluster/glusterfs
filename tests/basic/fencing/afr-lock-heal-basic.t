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

function fill_lock_info()
{
    local info
    local brick=$1
    pattern="ACTIVE.*client-${brick: -1}"

    brick_sdump=$(generate_brick_statedump $V0 $H0 $brick)
    info="$(egrep "$inode" $brick_sdump -A3| egrep "$pattern" | uniq | awk '{print $1,$2,$3,S4,$5,$6,$7,$8}'|tr -d '(,), ,')"

    echo "${info}"

    if [ -n "$info" ]
    then
        return 0
    else
        return 1
    fi
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
TEST build_tester $(dirname $0)/afr-lock-heal-basic.c -lgfapi -ggdb

$(dirname $0)/afr-lock-heal-basic $H0 $V0 "/FILE" $logdir C1&
client1_pid=$!
TEST [ $client1_pid ]

$(dirname $0)/afr-lock-heal-basic $H0 $V0 "/FILE" $logdir C2&
client2_pid=$!
TEST [ $client2_pid ]

TEST sleep 5 # By now, the 2 clients would  have opened an fd on FILE and waiting for a SIGUSR1.
EXPECT "Y" is_gfapi_program_alive $client1_pid
EXPECT "Y" is_gfapi_program_alive $client2_pid

gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/FILE))
inode="FILE|gfid:$gfid_str"

# Kill brick-3 and let client-1 take lock on the file.
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill -SIGUSR1 $client1_pid
# If program is still alive, glfs_file_lock() was a success.
EXPECT "Y" is_gfapi_program_alive $client1_pid

# Check lock is present on brick-1 and brick-2
TEST_WITHIN $PROCESS_UP_TIMEOUT fill_lock_info $B0/${V0}0
c1_lock_on_b1="${TEST_OUTPUT}"
TEST_WITHIN $PROCESS_UP_TIMEOUT fill_lock_info $B0/${V0}1
c1_lock_on_b2="${TEST_OUTPUT}"
TEST [ "$c1_lock_on_b1" == "$c1_lock_on_b2" ]

# Restart brick-3 and check that the lock has healed on it.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2

# Note: We need to wait for client to re-open the fd. Otherwise client_pre_lk_v2() fails with EBADFD for remote-fd. Also wait for lock heal.
# So we may need to check the statedump for locks multiple times.
TEST_WITHIN $PROCESS_UP_TIMEOUT fill_lock_info $B0/${V0}2
c1_lock_on_b3="${TEST_OUTPUT}"
TEST [ "$c1_lock_on_b1" == "$c1_lock_on_b3" ]

# Kill brick-1 and let client-2 preempt the lock on bricks 2 and 3.
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill -SIGUSR1 $client2_pid
# If program is still alive, glfs_file_lock() was a success.
EXPECT "Y" is_gfapi_program_alive $client2_pid

# Restart brick-1 and let lock healing complete.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0

# Check that all bricks now have locks from client 2 only.
# Note: We need to wait for client to re-open the fd. Otherwise client_pre_lk_v2() fails with EBADFD for remote-fd. Also wait for lock heal.
# So we may need to check the statedump for locks multiple times.
TEST_WITHIN $PROCESS_UP_TIMEOUT fill_lock_info $B0/${V0}0
c2_lock_on_b1="${TEST_OUTPUT}"
TEST_WITHIN $PROCESS_UP_TIMEOUT fill_lock_info $B0/${V0}1
c2_lock_on_b2="${TEST_OUTPUT}"
TEST_WITHIN $PROCESS_UP_TIMEOUT fill_lock_info $B0/${V0}2
c2_lock_on_b3="${TEST_OUTPUT}"
TEST [ "$c2_lock_on_b1" == "$c2_lock_on_b2" ]
TEST [ "$c2_lock_on_b1" == "$c2_lock_on_b3" ]
TEST [ "$c2_lock_on_b1" != "$c1_lock_on_b1" ]

#Let the client programs run and exit.
TEST kill -SIGUSR1 $client1_pid
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" is_gfapi_program_alive $client1_pid
TEST kill -SIGUSR1 $client2_pid
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "N" is_gfapi_program_alive $client2_pid

cleanup_tester $(dirname $0)/afr-lock-heal-basic
cleanup;
