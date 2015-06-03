#!/bin/bash

. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../include.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;
TEST pidof glusterd;

#TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume create $V0 $H0:$L2 $H0:$L3;
TEST $CLI volume start $V0;

TEST $CLI snapshot create snap1 $V0 no-timestamp;
TEST $CLI volume stop $V0
TEST $CLI snapshot restore snap1;
TEST $CLI volume start $V0
TEST $CLI volume attach-tier $V0 $H0:$L1 $H0:$L2

TEST pkill gluster
TEST glusterd
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'

TEST $CLI volume stop $V0

cleanup  ;

