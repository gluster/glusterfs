#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind on
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=enable
touch $M0/a

#Open file with fd as 5
exec 5>$M0/a
realpath=$(gf_get_gfid_backend_file_path $B0/${V0}0 "a")

kill_brick $V0 $H0 $B0/${V0}0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

EXPECT "Y" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 "$realpath"

kill_brick $V0 $H0 $B0/${V0}0
TEST gf_rm_file_and_gfid_link $B0/${V0}0 "a"
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
ls -l $M0/a > /dev/null 2>&1  #Make sure the file is re-created
EXPECT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 "$realpath"
EXPECT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 $B0/${V0}0/a

for i in {1..1024}; do
        echo "open sesame" >&5
done

EXPECT_WITHIN $REOPEN_TIMEOUT "Y" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 "$realpath"
#close the fd
exec 5>&-

#Check that anon-fd based file is not leaking.
EXPECT_WITHIN $REOPEN_TIMEOUT "N" gf_check_file_opened_in_brick $V0 $H0 $B0/${V0}0 "$realpath"
cleanup;
