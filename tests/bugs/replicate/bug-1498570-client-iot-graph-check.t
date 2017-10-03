#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

TESTS_EXPECTED_IN_LOOP=21
function reset_cluster
{
        cleanup
        TEST glusterd
        TEST pidof glusterd

}
function check_iot_option
{
        local enabled=$1
        local is_loaded_in_graph=$2

        EXPECT "$enabled" volume_get_field $V0 client-io-threads
        IOT_STRING="volume\ $V0-io-threads"
        grep "$IOT_STRING" $GLUSTERD_WORKDIR/vols/$V0/trusted-$V0.tcp-fuse.vol
        TEST ret=$?
        EXPECT_NOT "$is_loaded_in_graph" echo $ret
}

reset_cluster
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
check_iot_option on 1

reset_cluster
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
check_iot_option off 0

reset_cluster
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume add-brick $V0 replica 2 $H0:$B0/${V0}1
check_iot_option off 0
TEST $CLI volume remove-brick $V0 replica 1 $H0:$B0/${V0}1 force
check_iot_option on 1

reset_cluster
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..5}
TEST $CLI volume set $V0 client-io-threads on
check_iot_option on 1
TEST $CLI volume remove-brick $V0 replica 2 $H0:$B0/${V0}2 $H0:$B0/${V0}5 force
check_iot_option on 1

cleanup
