#!/bin/bash
#
# Bug 853680
#
# Test that io-threads least-rate-limit throttling functions as expected. Set
# a limit, perform a few operations with a least-priority mount and verify
# said operations take a minimum amount of time according to the limit.

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume start $V0

# set rate limit to 1 operation/sec
TEST $CLI volume set $V0 performance.least-rate-limit 1

# use client-pid=-1 for least priority mount
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --client-pid=-1

# create a few files and verify this takes more than a few seconds
date1=`date +%s`
TEST touch $M0/file{0..2}
date2=`date +%s`

optime=$(($date2 - $date1))
TEST [ $optime -ge 3 ]

TEST umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
