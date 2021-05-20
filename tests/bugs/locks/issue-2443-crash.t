#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

build_tester $(dirname $0)/issue-2443-crash.c
TEST mv $(dirname $0)/issue-2443-crash $M0
cd $M0
TEST ./issue-2443-crash a

cd -
cleanup;
