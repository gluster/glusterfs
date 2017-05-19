#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B{0..1}/$V0
TEST $CLI volume start $V0

TEST ! $CLI volume set $V0 parallel-readdir sdf

TEST $CLI volume set $V0 parallel-readdir off
TEST $CLI volume set $V0 parallel-readdir on

TEST ! $CLI volume set $V0 rda-cache-limit 0
TEST ! $CLI volume set $V0 rda-cache-limit -634
TEST ! $CLI volume set $V0 rda-cache-limit 87adh
TEST ! $CLI volume set $V0 parallel-readdir sdf

TEST ! $CLI volume set $V0 rda-request-size 0
TEST ! $CLI volume set $V0 rda-request-size -634
TEST ! $CLI volume set $V0 rda-request-size 87adh

TEST $CLI volume set $V0 rda-cache-limit 10MB
TEST $CLI volume set $V0 rda-request-size 128KB

cleanup;
