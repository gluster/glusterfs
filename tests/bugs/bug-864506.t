#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;


TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick0
TEST $CLI volume start $V0

TEST "mkdir $B0/slave"

TEST "$CLI volume geo-replication $V0 $B0/slave start --xml | xmllint --format -"

TEST "$CLI volume geo-replication $V0 $B0/slave2 start --xml | xmllint --format -"

TEST "$CLI volume geo-replication status --xml | xmllint --format -"

TEST "$CLI volume geo-replication $V0 $B0/slave status --xml | xmllint --format -"

TEST "$CLI volume geo-replication $V0 $B0/slave2 status --xml | xmllint --format -"

TEST "$CLI volume geo-replication $V0 $B0/slave stop --xml | xmllint --format -"

TEST "$CLI volume geo-replication $V0 $B0/slave2 stop --xml | xmllint --format -"

TEST $CLI volume stop $V0

TEST "rmdir $B0/slave"

cleanup;
