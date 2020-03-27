#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test tries to detect a race condition in write-behind. It's based on a
# reproducer written by Stefan Ring that is able to hit it sometimes. On my
# system, it happened around 10% of the runs. This means that if this bug
# appears again, this test will fail once every 10 runs. Most probably this
# failure will be hidden by the automatic test retry of the testing framework.
#
# Please, if this test fails, it needs to be analyzed in detail.

function run() {
    "${@}" >/dev/null
}

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
# This makes it easier to hit the issue
TEST $CLI volume set $V0 client-log-level TRACE
TEST $CLI volume start $V0

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0

build_tester $(dirname $0)/issue-884.c -lgfapi

TEST touch $M0/testfile

# This program generates a file of 535694336 bytes with a fixed pattern
TEST run $(dirname $0)/issue-884 $V0 testfile

# This is the md5sum of the expected pattern without corruption
EXPECT "ad105f9349345a70fc697632cbb5eec8" echo "$(md5sum $B0/$V0/testfile | awk '{ print $1; }')"

cleanup
