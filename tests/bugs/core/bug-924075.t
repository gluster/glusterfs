#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#FIXME: there is another patch which moves the following function into
#include.rc
function process_leak_count ()
{
    local pid=$1;
    return $(ls -lh /proc/$pid/fd | grep "(deleted)" | wc -l)
}

TEST glusterd;
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
TEST $CLI volume start $V0;
TEST glusterfs -s $H0 --volfile-id $V0 $M0;
mount_pid=$(get_mount_process_pid $V0);
TEST process_leak_count $mount_pid;

cleanup;
