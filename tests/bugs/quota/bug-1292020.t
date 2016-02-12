#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function write_sample_data () {
        dd if=/dev/zero of=$M0/f1 bs=256k count=400 2>&1 | grep -i exceeded
}

cleanup;

TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0;
TEST $CLI volume quota $V0 enable;
TEST $CLI volume quota $V0 limit-usage / 1

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;
EXPECT "exceeded" write_sample_data

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0
cleanup;
