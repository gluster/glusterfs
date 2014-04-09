#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

#mount on a random dir
TEST glusterfs --entry-timeout=3600 --attribute-timeout=3600 -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=yes

build_tester $(dirname $0)/fops-sanity.c

TEST cp $(dirname $0)/fops-sanity $M0
cd $M0
TEST ./fops-sanity $V0
cd -
TEST rm -f $(dirname $0)/fops-sanity
cleanup;
