#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

SEEK=$(dirname $0)/seek
build_tester $(dirname $0)/../seek.c -o ${SEEK}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST mkdir -p $B0/${V0}{0..2}
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..2}

TEST $CLI volume start $V0

TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST ${SEEK} create ${M0}/test 0 1 1048576 1
# Determine underlying filesystem allocation block size
BSIZE="$(($(${SEEK} scan ${M0}/test hole 0) * 2))"

TEST ${SEEK} create ${M0}/test 0 ${BSIZE} $((${BSIZE} * 4 + 512)) ${BSIZE}

EXPECT "^0$" ${SEEK} scan ${M0}/test data 0
EXPECT "^$((${BSIZE} / 2))$" ${SEEK} scan ${M0}/test data $((${BSIZE} / 2))
EXPECT "^$((${BSIZE} - 1))$" ${SEEK} scan ${M0}/test data $((${BSIZE} - 1))
EXPECT "^$((${BSIZE} * 4))$" ${SEEK} scan ${M0}/test data ${BSIZE}
EXPECT "^$((${BSIZE} * 4))$" ${SEEK} scan ${M0}/test data $((${BSIZE} * 4))
EXPECT "^$((${BSIZE} * 5))$" ${SEEK} scan ${M0}/test data $((${BSIZE} * 5))
EXPECT "^$((${BSIZE} * 5 + 511))$" ${SEEK} scan ${M0}/test data $((${BSIZE} * 5 + 511))
EXPECT "^ENXIO$" ${SEEK} scan ${M0}/test data $((${BSIZE} * 5 + 512))
EXPECT "^ENXIO$" ${SEEK} scan ${M0}/test data $((${BSIZE} * 6))

EXPECT "^${BSIZE}$" ${SEEK} scan ${M0}/test hole 0
EXPECT "^${BSIZE}$" ${SEEK} scan ${M0}/test hole $((${BSIZE} / 2))
EXPECT "^${BSIZE}$" ${SEEK} scan ${M0}/test hole $((${BSIZE} - 1))
EXPECT "^${BSIZE}$" ${SEEK} scan ${M0}/test hole ${BSIZE}
EXPECT "^$((${BSIZE} * 5 + 512))$" ${SEEK} scan ${M0}/test hole $((${BSIZE} * 4))
EXPECT "^$((${BSIZE} * 5 + 512))$" ${SEEK} scan ${M0}/test hole $((${BSIZE} * 5))
EXPECT "^$((${BSIZE} * 5 + 512))$" ${SEEK} scan ${M0}/test hole $((${BSIZE} * 5 + 511))
EXPECT "^ENXIO$" ${SEEK} scan ${M0}/test hole $((${BSIZE} * 5 + 512))
EXPECT "^ENXIO$" ${SEEK} scan ${M0}/test hole $((${BSIZE} * 6))

rm -f ${SEEK}
cleanup

# Centos6 regression machines seem to not support SEEK_DATA/SEEK_HOLE
#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=000000
