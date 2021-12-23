#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=20

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 performance.flush-behind off
TEST $CLI volume start $V0

for i in $(seq 1 3); do
   TEST $CLI volume create ${V0}_afr$i replica 3 $H0:$B0/${V0}_afr${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_afr$i
   TEST $CLI volume create ${V0}_ec$i disperse 6 redundancy 2 $H0:$B0/${V0}_ec${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_ec$i
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count

#Stop the glusterd
TEST pkill glusterd
#Only stopping glusterd, so there will be one shd
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^1$" shd_count
TEST glusterd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
#Check the thread count become to number of volumes*number of ec subvolume (3*6=18)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd $V0 "ec_shd_index_healer"
#Check the thread count become to number of volumes*number of afr subvolume (4*6=24)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^24$" number_healer_threads_shd $V0 "afr_shd_index_healer"

shd_pid=$(get_shd_mux_pid $V0)
for i in $(seq 1 3); do
    afr_path="/var/run/gluster/shd/${V0}_afr$i/${V0}_afr$i-shd.pid"
    EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" cat $afr_path
    ec_path="/var/run/gluster/shd/${V0}_ec$i/${V0}_ec${i}-shd.pid"
    EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" cat $ec_path
done

#Reboot a node scenario
TEST pkill gluster
#Only stopped glusterd, so there will be one shd
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" shd_count

TEST glusterd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count

#Check the thread count become to number of volumes*number of ec subvolume (3*6=18)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd $V0 "ec_shd_index_healer"
#Check the thread count become to number of volumes*number of afr subvolume (4*6=24)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^24$" number_healer_threads_shd $V0 "afr_shd_index_healer"

shd_pid=$(get_shd_mux_pid $V0)
for i in $(seq 1 3); do
    afr_path="/var/run/gluster/shd/${V0}_afr$i/${V0}_afr$i-shd.pid"
    EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" cat $afr_path
    ec_path="/var/run/gluster/shd/${V0}_ec$i/${V0}_ec${i}-shd.pid"
    EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" cat $ec_path
done

for i in $(seq 1 3); do
   TEST $CLI volume stop ${V0}_afr$i
   TEST $CLI volume stop ${V0}_ec$i
done

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}3

TEST touch $M0/foo{1..100}

EXPECT_WITHIN $HEAL_TIMEOUT "^204$" get_pending_heal_count $V0

TEST $CLI volume start ${V0} force

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST rm -rf $M0/*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0


TEST $CLI volume stop ${V0}
TEST $CLI volume delete ${V0}

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^0$" shd_count

cleanup
