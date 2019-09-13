#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=6

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

TEST $CLI volume add-brick $V0 replica 3 $H0:$B0/${V0}{6,7,8};
#Check the thread count become to number of volumes*number of afr subvolume plus 3 additional threads from newly added bricks (3*6+3=21)

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^21$" number_healer_threads_shd $V0 "afr_shd_index_healer"

#Remove the brick and check the detach is successful
$CLI volume remove-brick $V0 $H0:$B0/${V0}{6,7,8} force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd $V0 "afr_shd_index_healer"

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" number_healer_threads_shd $V0 "glusterfs_graph_cleanup"
TEST $CLI volume add-brick ${V0}_ec1 $H0:$B0/${V0}_ec1_add{0,1,2,3,4,5};
#Check the thread count become to number of volumes*number of ec subvolume plus 2 additional threads from newly added bricks (2*6+6=18)

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^18$" number_healer_threads_shd $V0 "ec_shd_index_healer"

#Remove the brick and check the detach is successful
$CLI volume remove-brick ${V0}_ec1 $H0:$B0/${V0}_ec1_add{0,1,2,3,4,5} force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^12$" number_healer_threads_shd $V0 "ec_shd_index_healer"


for i in $(seq 1 2); do
   TEST $CLI volume stop ${V0}_afr$i
   TEST $CLI volume stop ${V0}_ec$i
done

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}4

TEST touch $M0/foo{1..100}

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^204$" get_pending_heal_count $V0

TEST $CLI volume start ${V0} force

EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

TEST rm -rf $M0/*
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
shd_pid=$(get_shd_mux_pid $V0)
TEST $CLI volume create ${V0}_distribute1 $H0:$B0/${V0}_distribute10
TEST $CLI volume start ${V0}_distribute1

#Creating a non-replicate/non-ec volume should not have any effect in shd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"
EXPECT "^${shd_pid}$" get_shd_mux_pid $V0

TEST mkdir $B0/add/
#Now convert the distributed volume to replicate
TEST $CLI volume add-brick ${V0}_distribute1 replica 3 $H0:$B0/add/{2..3}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^9$" number_healer_threads_shd $V0 "afr_shd_index_healer"

#scale down the volume
TEST $CLI volume remove-brick ${V0}_distribute1 replica 1 $H0:$B0/add/{2..3} force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

#Before stopping the process, make sure there is no pending clenup threads hanging
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" number_healer_threads_shd $V0 "glusterfs_graph_cleanup"

TEST $CLI volume stop ${V0}
TEST $CLI volume delete ${V0}
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" shd_count

TEST rm -rf $B0/add/2 $B0/add/3

#Now convert the distributed volume back to replicate and make sure that a new shd is spawned
TEST $CLI volume add-brick ${V0}_distribute1 replica 3 $H0:$B0/add/{2..3};
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count
EXPECT_WITHIN $HEAL_TIMEOUT "^3$" number_healer_threads_shd ${V0}_distribute1 "afr_shd_index_healer"

#Now convert the replica volume to distribute again and make sure the shd is now stopped
TEST $CLI volume remove-brick ${V0}_distribute1 replica 1 $H0:$B0/add/{2..3} force
TEST rm -rf $B0/add/

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" shd_count

cleanup

#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1708929
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=1708929
