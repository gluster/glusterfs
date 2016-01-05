#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc
cleanup;

#Allow readdirs to proceed on directories that are in split-brain

TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1};
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0;
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST mkdir $M0/dir
TEST touch $M0/dir/file{1..5}

#Create entry split-brain
TEST kill_brick $V0 $H0 $B0/$V0"1"
TEST touch $M0/dir/FILE
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT '1' afr_child_up_status_meta $M0 $V0-replicate-0 1
TEST kill_brick $V0 $H0 $B0/$V0"0"
TEST touch $M0/dir/FILE
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT '1' afr_child_up_status_meta $M0 $V0-replicate-0 0

cd $M0/dir
EXPECT "6" echo $(ls | wc -l)
TEST ! cat FILE
TEST `echo hello>hello.txt`
cd -
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup
