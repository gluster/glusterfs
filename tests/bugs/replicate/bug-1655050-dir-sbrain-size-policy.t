#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd

#Create replica 2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

cd $M0
TEST mkdir dir

#Create metadata split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST chmod 757 dir
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST chmod 747 dir
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

#Use size as fav-child policy.
TEST $CLI volume set $V0 cluster.favorite-child-policy size

#Enable shd and heal the file.
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

EXPECT_WITHIN $HEAL_TIMEOUT "2" get_pending_heal_count $V0

b1c1dir=$(afr_get_specific_changelog_xattr $B0/${V0}0/dir \
          trusted.afr.$V0-client-1 "metadata")
b2c0dir=$(afr_get_specific_changelog_xattr $B0/${V0}1/dir \
          trusted.afr.$V0-client-0 "metadata")

EXPECT "00000001" echo $b1c1dir
EXPECT "00000001" echo $b2c0dir

#Finish up
TEST force_umount $M0
cleanup;
