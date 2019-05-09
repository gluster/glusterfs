#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=16

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

shd_pid=$(get_shd_mux_pid $V0)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "__afr_shd_healer_wait"

#Create a one more volume
TEST $CLI volume create ${V0}_1 replica 3 $H0:$B0/${V0}_1{0,1,2,3,4,5}
TEST $CLI volume start ${V0}_1

#Check whether the shd has multiplexed or not
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid ${V0}_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid ${V0}

TEST $CLI volume set ${V0}_1 cluster.background-self-heal-count 0
TEST $CLI volume set ${V0}_1 cluster.eager-lock off
TEST $CLI volume set ${V0}_1 performance.flush-behind off
TEST $GFS --volfile-id=/${V0}_1 --volfile-server=$H0 $M1

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}4
TEST kill_brick ${V0}_1 $H0 $B0/${V0}_10
TEST kill_brick ${V0}_1 $H0 $B0/${V0}_14

TEST touch $M0/foo{1..100}
TEST touch $M1/foo{1..100}

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^204$" get_pending_heal_count $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^204$" get_pending_heal_count ${V0}_1

TEST $CLI volume start ${V0} force
TEST $CLI volume start ${V0}_1 force

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count ${V0}_1

TEST rm -rf $M0/*
TEST rm -rf $M1/*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

#Stop the volume
TEST $CLI volume stop ${V0}_1
TEST $CLI volume delete ${V0}_1

#Check the stop succeeded and detached the volume with out restarting it
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid $V0

#Check the thread count become to earlier number after stopping
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "__afr_shd_healer_wait"


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

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "__ec_shd_healer_wait"

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
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" number_healer_threads_shd $V0 "__ec_shd_healer_wait"
#Check the thread count become to earlier number after stopping
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "__afr_shd_healer_wait"

for i in $(seq 1 3); do
   TEST $CLI volume create ${V0}_afr$i replica 3 $H0:$B0/${V0}_afr${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_afr$i
   TEST $CLI volume create ${V0}_ec$i disperse 6 redundancy 2 $H0:$B0/${V0}_ec${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_ec$i
done

#Check the thread count become to number of volumes*number of ec subvolume (3*6=18)
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^18$" number_healer_threads_shd $V0 "__ec_shd_healer_wait"
#Check the thread count become to number of volumes*number of afr subvolume (4*6=24)
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^24$" number_healer_threads_shd $V0 "__afr_shd_healer_wait"
#Delete the volumes
for i in $(seq 1 3); do
   TEST $CLI volume stop ${V0}_afr$i
   TEST $CLI volume stop ${V0}_ec$i
   TEST $CLI volume delete ${V0}_afr$i
   TEST $CLI volume delete ${V0}_ec$i
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "__afr_shd_healer_wait"

TEST $CLI volume stop ${V0}
TEST $CLI volume delete ${V0}
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" shd_count

cleanup
