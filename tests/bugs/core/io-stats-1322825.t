#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0} $H0:$B1/${V0}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST "echo hello > $M0/file1"
TEST setfattr -n trusted.io-stats-dump -v /tmp/io-stats.log $M0
TEST ! getfattr -n trusted.io-stats-dump $B0/${V0}

cleanup;
