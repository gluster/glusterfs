#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

#### patchy.dev.d-backends-patchy1.vol
brick=${B0//\//-}
SERVER_VOLFILE="/var/lib/glusterd/vols/${V0}/${V0}.${HOSTNAME}.${brick:1}-${V0}1.vol"

cleanup;

TEST mkdir -p $B0/single-brick
TEST mkdir -p ${GLUSTER_XLATOR_DIR}/user

## deploy dummy user xlator
TEST cp ${GLUSTER_XLATOR_DIR}/playground/template.so ${GLUSTER_XLATOR_DIR}/user/hoge.so

TEST glusterd
TEST $CLI volume create $V0 replica 3  $H0:$B0/${V0}{1,2,3,4,5,6};

TEST $CLI volume set $V0 user.xlator.hoge posix
TEST grep -q 'user/hoge' ${SERVER_VOLFILE}

TEST $CLI volume set $V0 user.xlator.hoge.opt1 10
TEST grep -q '"option opt1 10"' ${SERVER_VOLFILE}
TEST $CLI volume set $V0 user.xlator.hoge.opt2 hogehoge
TEST grep -q '"option opt2 hogehoge"' ${SERVER_VOLFILE}
TEST $CLI volume set $V0 user.xlator.hoge.opt3 true
TEST grep -q '"option opt3 true"' ${SERVER_VOLFILE}

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}5
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}6

TEST $CLI volume set $V0 user.xlator.hoge trash
TEST grep -q 'user/hoge' ${SERVER_VOLFILE}

TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}5
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}6

TEST ! $CLI volume set $V0 user.xlator.hoge unknown
TEST grep -q 'user/hoge' ${SERVER_VOLFILE} # When the CLI fails, the volfile is not modified.

TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}4
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}5
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}6

#### teardown

TEST rm -f ${GLUSTER_XLATOR_DIR}/user/hoge.so
cleanup;
