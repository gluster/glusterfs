#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

shd_pid=$(get_shd_mux_pid $V0)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

#Now create a  ec volume and check mux works
TEST $CLI volume create ${V0}_2 disperse 6 redundancy 2 $H0:$B0/${V0}_2{0,1,2,3,4,5}
TEST $CLI volume start ${V0}_2

#Check whether the shd has multiplexed or not
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid ${V0}_2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid ${V0}

TEST $CLI volume set ${V0}_2 cluster.background-self-heal-count 0
TEST $CLI volume set ${V0}_2 cluster.eager-lock off
TEST $CLI volume set ${V0}_2 performance.flush-behind off
TEST $GFS --volfile-id=/${V0}_2 --volfile-server=$H0 $M1

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST kill_brick ${V0}_2 $H0 $B0/${V0}_20
TEST kill_brick ${V0}_2 $H0 $B0/${V0}_22

TEST touch $M0/foo{1..100}
TEST touch $M1/foo{1..100}

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^204$" get_pending_heal_count $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^404$" get_pending_heal_count ${V0}_2

TEST $CLI volume start ${V0} force
TEST $CLI volume start ${V0}_2 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "ec_shd_index_healer"

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count ${V0}_2

TEST rm -rf $M0/*
TEST rm -rf $M1/*


#Stop the volume
TEST $CLI volume stop ${V0}_2
TEST $CLI volume delete ${V0}_2

#Check the stop succeeded and detached the volume with out restarting it
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid $V0

#Check the thread count become to zero for ec related threads
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" number_healer_threads_shd $V0 "ec_shd_index_healer"
#Check the thread count become to earlier number after stopping
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

TEST $CLI volume stop ${V0}
TEST $CLI volume delete ${V0}
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" shd_count

cleanup
