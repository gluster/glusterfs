#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id $V0 $M0

mount_pid=$(get_mount_process_pid $V0);
# enable dumping of call stack creation and frame creation times in statedump
kill -USR2 $mount_pid;

TEST touch $M0/touchfile;
(dd if=/dev/urandom of=$M0/file bs=5k 2>/dev/null 1>/dev/null)&
back_pid=$!;
statedump_file=$(generate_mount_statedump $V0);
grep "callstack-creation-time" $statedump_file 2>/dev/null 1>/dev/null;
TEST [ $? -eq 0 ];
grep "frame-creation-time" $statedump_file 2>/dev/null 1>/dev/null;
TEST [ $? -eq 0 ];

kill -SIGTERM $back_pid;
wait >/dev/null 2>&1;

TEST rm -f $M0/touchfile $M0/file;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

rm -f $statedumpdir/glusterdump.$mount_pid.*;
cleanup
