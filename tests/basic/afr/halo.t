#!/bin/bash
#Tests that halo basic functionality works as expected

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

function get_up_child()
{
    if [ "1" == $(afr_private_key_value $V0 $M0 0 "child_up\[0\]") ];
    then
        echo 0
    elif [ "1" == $(afr_private_key_value $V0 $M0 0 "child_up\[1\]") ]
    then
        echo 1
    fi
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.halo-enabled yes
TEST $CLI volume set $V0 cluster.halo-max-replicas 1
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
EXPECT "^1$" afr_private_key_value $V0 $M0 0 "halo_child_up\[0\]"
EXPECT "^1$" afr_private_key_value $V0 $M0 0 "halo_child_up\[1\]"
EXPECT_NOT "^-1$" afr_private_key_value $V0 $M0 0 "child_latency\[0\]"
EXPECT_NOT "^-1$" afr_private_key_value $V0 $M0 0 "child_latency\[1\]"

up_id=$(get_up_child)
TEST [[ ! -z "$up_id" ]]

down_id=$((1-up_id))

TEST kill_brick $V0 $H0 $B0/${V0}${up_id}
#As max-replicas is configured to be 1, down_child should be up now
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "halo_child_up\[${down_id}\]"
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "child_up\[${down_id}\]"
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_private_key_value $V0 $M0 0 "halo_child_up\[${up_id}\]"
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_private_key_value $V0 $M0 0 "child_up\[${up_id}\]"
EXPECT "^-1$" afr_private_key_value $V0 $M0 0 "child_latency\[${up_id}\]"
EXPECT_NOT "^-1$" afr_private_key_value $V0 $M0 0 "child_latency\[${down_id}\]"

#Bring the brick back up and the state should be restored
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "halo_child_up\[${up_id}\]"

up_id=$(get_up_child)
TEST [[ ! -z "$up_id" ]]
down_id=$((1-up_id))
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "halo_child_up\[${down_id}\]"
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" afr_private_key_value $V0 $M0 0 "child_up\[${down_id}\]"
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "halo_child_up\[${up_id}\]"
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^1$" afr_private_key_value $V0 $M0 0 "child_up\[${up_id}\]"
EXPECT_NOT "^-1$" afr_private_key_value $V0 $M0 0 "child_latency\[0\]"
EXPECT_NOT "^-1$" afr_private_key_value $V0 $M0 0 "child_latency\[1\]"

cleanup;
