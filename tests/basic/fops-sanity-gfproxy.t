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

#gfproxy server
TEST glusterfs --volfile-id=gfproxy/$V0 --volfile-server=$H0 -l /var/log/glusterfs/${V0}-gfproxy.log

#mount on a random dir
TEST glusterfs --entry-timeout=3600 --attribute-timeout=3600 -s $H0 --volfile-id=gfproxy-client/$V0 $M0 --direct-io-mode=yes
TEST grep gfproxy-client /proc/mounts

build_tester $(dirname $0)/fops-sanity.c

TEST cp $(dirname $0)/fops-sanity $M0
cd $M0
TEST ./fops-sanity $V0
cd -
rm -f $(dirname $0)/fops-sanity

cleanup;
