#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;

>/var/log/glusterfs/nfs.log;

function check_event_thread_log ()
{
        local nlines

        nlines=$(grep "$1" /var/log/glusterfs/nfs.log | grep -v G_LOG | wc -l)
        if [ "$nlines" != "0" ]; then
                echo "Y" 
        else
                echo "N"
        fi;
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

# Make sure its not enabled without the option
EXPECT_WITHIN 10 "N" check_event_thread_log "Started thread with index 3";

# Turn it on and check that we spawned the threads
TEST $CLI volume set $V0 nfs.disable on
TEST $CLI volume set $V0 nfs.event-threads 4
TEST $CLI volume set $V0 nfs.disable off
EXPECT_WITHIN 10 "Y" check_event_thread_log "Started thread with index 4";

cleanup;
