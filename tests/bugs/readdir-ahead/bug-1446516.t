#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{1..4}
TEST $CLI volume start $V0

TEST $CLI volume set $V0 parallel-readdir on

TEST $CLI volume set $V0 rda-cache-limit 4GB

TEST $CLI volume set $V0 parallel-readdir off

TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

cleanup;
