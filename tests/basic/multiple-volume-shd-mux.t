#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=16

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2,3,4,5}
TEST $CLI volume start $V0

shd_pid=$(get_shd_mux_pid $V0)
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

for i in $(seq 1 3); do
   TEST $CLI volume create ${V0}_afr$i replica 3 $H0:$B0/${V0}_afr${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_afr$i
   TEST $CLI volume create ${V0}_ec$i disperse 6 redundancy 2 $H0:$B0/${V0}_ec${i}{0,1,2,3,4,5}
   TEST $CLI volume start ${V0}_ec$i
done

#Check the thread count become to number of volumes*number of ec subvolume (3*6=18)
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^18$" number_healer_threads_shd $V0 "ec_shd_index_healer"
#Check the thread count become to number of volumes*number of afr subvolume (4*6=24)
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^24$" number_healer_threads_shd $V0 "afr_shd_index_healer"
#Delete the volumes
for i in $(seq 1 3); do
   TEST $CLI volume stop ${V0}_afr$i
   TEST $CLI volume stop ${V0}_ec$i
   TEST $CLI volume delete ${V0}_afr$i
   TEST $CLI volume delete ${V0}_ec$i
done

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^${shd_pid}$" get_shd_mux_pid $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "^1$" shd_count

EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^6$" number_healer_threads_shd $V0 "afr_shd_index_healer"

TEST $CLI volume stop ${V0}
TEST $CLI volume delete ${V0}
EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT "^0$" shd_count

cleanup
