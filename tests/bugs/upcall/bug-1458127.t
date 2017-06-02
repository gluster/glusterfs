#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{0}
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume set $V0 performance.nl-cache on
TEST $CLI volume set $V0 nl-cache-positive-entry on
TEST $CLI volume set $V0 nl-cache-timeout 2
TEST $CLI volume set $V0 features.cache-invalidation on
TEST $CLI volume set $V0 features.cache-invalidation-timeout 2
TEST $CLI volume set $V0 md-cache-timeout 20

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M1

TEST mkdir $M0/dir
TEST touch $M0/dir/xyz
#Wait until upcall clears the fact that M0 had accessed dir
sleep 4
TEST mv $M0/dir/xyz $M0/dir/xyz1
TEST ! ls $M0/dir/file1
TEST touch $M1/dir/file1
TEST ls $M0/dir/file1
TEST ls $M0/dir/file1

cleanup;
