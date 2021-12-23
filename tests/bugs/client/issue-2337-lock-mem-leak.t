#!/bin/bash

#Test that lock fop is not leaking any memory for overlapping regions
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

LOCK_TEST=$(dirname $0)/issue-2337-lock-mem-leak
build_tester $(dirname $0)/issue-2337-lock-mem-leak.c -o ${LOCK_TEST}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
#Guard against flush-behind
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST touch $M0/a
TEST fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/a
TEST flock -x $fd1
statedump=$(generate_mount_statedump $V0 $M0)
EXPECT_NOT "^nostatedump$" echo $statedump
#Making sure no one changes this mem-tracker name
TEST grep gf_client_mt_clnt_lock_t $statedump
TEST fd_close $fd1

statedump=$(generate_mount_statedump $V0 $M0)
EXPECT_NOT "^nostatedump$" echo $statedump
TEST ! grep gf_client_mt_clnt_lock_t $statedump

TEST ${LOCK_TEST} $M0/a

statedump=$(generate_mount_statedump $V0 $M0)
EXPECT_NOT "^nostatedump$" echo $statedump
TEST ! grep gf_client_mt_clnt_lock_t $statedump
TEST cleanup_mount_statedump $V0
TEST rm ${LOCK_TEST}
cleanup
