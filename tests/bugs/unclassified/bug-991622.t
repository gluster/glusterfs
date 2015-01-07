#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

#This tests that no fd leaks are observed in unlink/rename in open-behind
function leaked_fds {
        ls -l /proc/$(get_brick_pid $V0 $H0 $B0/$V0)/fd | grep deleted
}

cleanup;
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume set $V0 open-behind on
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=enable

TEST fd1=`fd_available`
TEST fd_open $fd1 'w' "$M0/testfile1"
TEST fd_write $fd1 "content"

TEST fd2=`fd_available`
TEST fd_open $fd2 'w' "$M0/testfile2"
TEST fd_write $fd2 "content"

TEST touch $M0/a
TEST rm $M0/testfile1
TEST mv $M0/a $M0/testfile2
TEST fd_close $fd1
TEST fd_close $fd2
TEST ! leaked_fds
cleanup;
