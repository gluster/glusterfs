#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=4

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0

for i in $(seq 1 2); do
   TEST $CLI volume create ${V0}_afr$i replica 3 $H0:$B0/${V0}_afr${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_afr$i
   TEST $CLI volume create ${V0}_ec$i disperse 6 redundancy 2 $H0:$B0/${V0}_ec${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_ec$i
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
#Check the thread count become to number of volumes*number of ec subvolume (2*6=12)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^12$" number_healer_threads_shd $V0 "ec_shd_index_healer"
#Check the thread count become to number of volumes*number of afr subvolume (3*6=18)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd $V0 "afr_shd_index_healer"

#kill one brick and test cleanup
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST $CLI volume stop $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^12$" number_healer_threads_shd ${V0}_afr1 "afr_shd_index_healer"
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd ${V0}_afr1 "afr_shd_index_healer"

#kill an entire subvol and test cleanup
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
#wait for some time to create a race sceanrio
sleep 1
TEST $CLI volume stop $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^12$" number_healer_threads_shd ${V0}_afr1 "afr_shd_index_healer"
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd ${V0}_afr1 "afr_shd_index_healer"

#kill all bricks and test cleanup
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST kill_brick $V0 $H0 $B0/${V0}5
#wait for some time to create a race sceanrio
sleep 2

TEST $CLI volume stop $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^12$" number_healer_threads_shd ${V0}_afr1 "afr_shd_index_healer"
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd ${V0}_afr1 "afr_shd_index_healer"

cleanup
