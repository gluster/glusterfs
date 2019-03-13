#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function check_callstack_log {
    local text=$1
    statedump_file=$(generate_mount_statedump $V0);
    grep $text $statedump_file 2>/dev/null 1>/dev/null;
    if [ $? -eq 0 ]; then
        echo "1";
    else
        echo "0";
    fi;
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume start $V0
TEST glusterfs -s $H0 --volfile-id $V0 $M0

mount_pid=$(get_mount_process_pid $V0);
# enable dumping of call stack creation and frame creation times in statedump
# monitoring is enabled by default

# We want to make sure that there is a pending frame in gluster stack.
# For that we are creating a blocking lock scenario.

TEST touch $M0/lockfile;
# Open two fd's on the same file
exec 8>$M0/lockfile;
exec 9>$M0/lockfile;

# First flock will succeed and the second one will block, hence the background run.
flock -x 8 ;
flock -x 9 &

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" check_callstack_log "callstack-creation-time";
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" check_callstack_log "frame-creation-time";

flock -u 8
flock -u 9;

# Closing the fd's
exec 8>&-
exec 9>&-

TEST rm -f $M0/lockfile;
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

rm -f $statedumpdir/glusterdump.$mount_pid.*;
cleanup
