#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../tier.rc


# Creates a tiered volume with pure distribute hot and cold tiers
# Both hot and cold tiers will have an equal number of bricks.

function create_dist_tier_vol () {
        mkdir $B0/cold
        mkdir $B0/hot
        TEST $CLI volume create $V0 $H0:$B0/cold/${V0}{1..3}
        TEST $CLI volume set $V0 performance.quick-read off
        TEST $CLI volume set $V0 performance.io-cache off
        TEST $CLI volume start $V0
        TEST $CLI volume attach-tier $V0 $H0:$B0/hot/${V0}{1..2}
        TEST $CLI volume set $V0 cluster.tier-mode test
}

function non_zero_check () {
        if [ "$1" -ne 0 ]
        then
                echo "0"
        else
                echo "1"
        fi
}

function num_bricks_up {
        local b
        local n_up=0

        for b in $B0/hot/${V0}{1..2} $B0/cold/${V0}{1..3}; do
                if [ x"$(brick_up_status $V0 $H0 $b)" = x"1" ]; then
                        n_up=$((n_up+1))
                fi
        done

        echo $n_up
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume status


#Create and start a tiered volume
create_dist_tier_vol
# Wait for the bricks to come up, *then* the tier daemon.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 5 num_bricks_up
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 0 tier_daemon_check
sleep 5   #wait for some time to run tier daemon
time_before_restarting=$(rebalance_run_time $V0);

#checking for elapsed time after sleeping for two seconds.
EXPECT "0" non_zero_check $time_before_restarting;

#Difference of elapsed time should be positive

kill -9 $(pidof glusterd);
TEST glusterd;
sleep 2;
# Wait for the bricks to come up, *then* the tier daemon.
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 5 num_bricks_up
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" tier_daemon_check;
time1=$(rebalance_run_time $V0);
EXPECT "0" non_zero_check $time1;
sleep 4;
time2=$(rebalance_run_time $V0);
EXPECT "0" non_zero_check $time2;
diff=`expr $time2 - $time1`
EXPECT "0" non_zero_check $diff;
