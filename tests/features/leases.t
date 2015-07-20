#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume start $V0
TEST $CLI volume set $V0 leases on

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST mkdir $M0/test
TEST touch $M0/test/lease

TEST $CLI volume set $V0 leases off

cleanup;
