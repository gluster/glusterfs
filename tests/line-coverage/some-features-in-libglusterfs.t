#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function grep_string {
    local f=$1
    local string=$2
    # The output of test script also shows up in log. Ignore them.
    echo $(grep ${string} ${f} | grep -v "++++++" | wc -l)
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume set $V0 client-log-level TRACE
TEST $CLI volume start $V0;

log_file="$(gluster --print-logdir)/gluster.log"
## Mount FUSE
TEST $GFS -s $H0 --log-file $log_file --volfile-id $V0 $M1

## Cover 'monitoring.c' here
pgrep 'glusterfs' | xargs kill -USR2

EXPECT_WITHIN 2 1 grep_string $log_file 'sig:USR2'

## Also cover statedump
pgrep 'glusterfs' | xargs kill -USR1

EXPECT_WITHIN 2 1 grep_string $log_file 'sig:USR1'

## Also cover SIGHUP
pgrep 'glusterfs' | xargs kill -HUP

EXPECT_WITHIN 2 1 grep_string $log_file 'sig:HUP'

## Also cover SIGTERM
pgrep 'glusterfs' | xargs kill -TERM

EXPECT_WITHIN 2 1 grep_string $log_file 'cleanup_and_exit'

# Previous call should make umount of the process.
# force_umount $M1

# TODO: below section is commented out, mainly as our regression treats the test
# as failure because sending ABRT signal will cause the process to dump core.
# Our regression treats the test as failure, if there is a core.
# FIXME: figure out a way to run this test, because this part of the code gets
# executed only when there is coredump, and it is critical for debugging, to
# keep it working always.

# # Restart client
# TEST $GFS -s $H0 --log-file $log_file --volfile-id $V0 $M1
#
# ## Also cover SIGABRT
# pgrep 'glusterfs ' | xargs kill -ABRT
#
# TEST [ 1 -eq $(grep 'pending frames' $log_file | wc -l) ]

TEST rm $log_file

cleanup;
