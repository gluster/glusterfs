#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;


TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume start $V0

TEST "$CLI volume geo-replication $V0 $B0/slave start --xml | xmllint --format -"

TEST "$CLI volume geo-replication $V0 $B0/slave stop --xml | xmllint --format -"

TEST $CLI volume stop $V0

cleanup;

