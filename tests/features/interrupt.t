#!/bin/bash

##Copy this file to tests/bugs before running run.sh (cp extras/test/bug-920583.t tests/bugs/)

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

TESTS_EXPECTED_IN_LOOP=4

cleanup;
logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/open_and_sleep.c

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3,4,5,6,7,8,9};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

function log-file-name()
{
    logfilename=$M0".log"
    echo ${logfilename:1} | tr / -
}

log_file=$logdir"/"`log-file-name`

function test_interrupt {
        local handlebool="$1"
        local logpattern="$2"

        TEST $GFS --volfile-id=$V0 --volfile-server=$H0 --fuse-flush-handle-interrupt=$handlebool --log-level=DEBUG $M0

        # If the test helper fails (which is considered a setup error, not failure of the test
        # case itself), kill will be invoked without argument, and that will be the actual
        # error which is caught.
        TEST "./$(dirname $0)/open_and_sleep $M0/testfile-$handlebool | { sleep 0.1; xargs -n1 kill -INT; }"

        TEST "grep -E '$logpattern' $log_file"
        # Basic sanity check, making sure filesystem has not crashed.
        TEST test -f $M0/testfile-$handlebool
}

# Theoretically FLUSH might finish before INTERRUPT is handled,
# in which case we'd get the "no handler found" message instead of
# "interrupt handler triggered" (but it's unlikely).
# If that's observed, the pattern can be changed to
# 'FLUSH.*interrupt handler triggered|[I]NTERRUPT.*no handler found'
# to fix the test.
test_interrupt yes '[F]LUSH.*interrupt handler triggered'
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
test_interrupt no '[I]NTERRUPT.*no handler found'

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup_tester $(dirname $0)/open_and_sleep;
cleanup;
