#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 performance.quick-read off

#mount on a random dir
TEST glusterfs --entry-timeout=3600 --attribute-timeout=3600 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=yes

build_tester $(dirname $0)/bug-858242.c

TEST $(dirname $0)/bug-858242 $M0/testfile $V0

TEST rm -rf $(dirname $0)/858242
cleanup;

