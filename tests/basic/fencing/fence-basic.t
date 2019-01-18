#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 diagnostics.client-log-flush-timeout 30
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 locks.mandatory-locking forced
TEST $CLI volume set $V0 enforce-mandatory-lock on


logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/fence-basic.c -lgfapi -ggdb
TEST $(dirname $0)/fence-basic $H0 $V0 $logdir

cleanup_tester $(dirname $0)/fence-basic

cleanup;