#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST useradd tmpuser

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.favorite-child-policy mtime
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal on
TEST $CLI volume set $V0 cluster.data-self-heal off

TEST $CLI volume start $V0
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "^2$" online_brick_count

TEST glusterfs --volfile-id=$V0 --acl --volfile-server=$H0 --entry-timeout=0 $M0;

TEST kill_brick $V0 $H0 $B0/${V0}0
EXPECT_WITHIN ${PROCESS_DOWN_TIMEOUT} "^1$" online_brick_count

TEST mkdir -p $M0/tmp1/new
TEST chown tmpuser:tmpuser $M0/tmp1/
TEST chown tmpuser:tmpuser $M0/tmp1/new
TEST chmod o-rx $M0/tmp1/
TEST chmod o-rx $M0/tmp1/new

TEST $CLI volume start $V0 force
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "^2$" online_brick_count
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST glusterfs --volfile-id=$V0 --acl --volfile-server=$H0 --entry-timeout=0 $M0;
TEST ls $M0/tmp1

#At this point, we have completed entry-heal but not meta-data heal
OrigUserid=$(id -u tmpuser)
FileUserid=$(id -u `stat -c '%U' $B0/${V0}1/tmp1`)
EXPECT "^$OrigUserid$" echo $FileUserid

OrigUserid=$(id -u tmpuser)
FileUserid=$(id -u `stat -c '%U' $B0/${V0}0/tmp1`)
EXPECT_NOT "^$OrigUserid$" echo $FileUserid


TEST kill_brick $V0 $H0 $B0/${V0}1
EXPECT_WITHIN ${PROCESS_DOWN_TIMEOUT} "^1$" online_brick_count

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;
TEST glusterfs --volfile-id=$V0 --acl --volfile-server=$H0 --entry-timeout=0 $M0;

#With this lookup from mount-point, the dht will trigger a setxattr which will force afr to do an xattrop
#So there will be a split-brain now,
TEST ls $M0/tmp1
TEST $CLI volume heal $V0 disable
TEST $CLI volume start $V0 force
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "^2$" online_brick_count
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "^2$" afr_get_split_brain_count $V0
TEST $CLI volume set $V0 cluster.self-heal-daemon on
TEST $CLI volume heal $V0 enable
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

OrigUserid=$(id -u tmpuser)
FileUserid=$(id -u `stat -c '%U' $B0/${V0}0/tmp1`)
EXPECT "^$OrigUserid$" echo $FileUserid

userdel --force tmpuser
cleanup
