#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TESTS_EXPECTED_IN_LOOP=126

cleanup

TEST glusterd
TEST ${CLI} volume create ${V0} replica 3 ${H0}:/$B0/${V0}_{0..2}
TEST ${CLI} volume start ${V0}

TEST ${GFS} --volfile-server ${H0} --volfile-id ${V0} ${M0}

# Create several files to make sure that at least some of them should be
# migrated by rebalance.
for i in {0..63}; do
    TEST dd if=/dev/urandom of=${M0}/file.${i} bs=4k count=1
    TEST dd if=/dev/urandom of=${M0}/file.${i} bs=4k count=1 seek=128
done

TEST ${CLI} volume add-brick ${V0} ${H0}:${B0}/${V0}_{3..5}
TEST ${CLI} volume rebalance ${V0} start force
EXPECT_WITHIN ${REBALANCE_TIMEOUT} "completed" rebalance_status_field "${V0}"

EXPECT "^0$" rebalance_failed_field "${V0}"

cleanup
