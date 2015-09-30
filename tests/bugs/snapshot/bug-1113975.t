#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;

TEST $CLI volume start $V0;

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..10} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap1 $V0 no-timestamp;

for i in {11..20} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap2 $V0 no-timestamp;

TEST $CLI volume stop $V0

TEST $CLI snapshot restore snap1;

TEST $CLI snapshot restore snap2;

TEST $CLI volume start $V0

cleanup  ;
