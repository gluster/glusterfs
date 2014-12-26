#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

#simulate a split-brain of a file and do truncate. This should not crash the mount point
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0
TEST touch a
#simulate no-changelog data split-brain
echo "abc" > $B0/${V0}1/a
echo "abcd" > $B0/${V0}0/a
TEST truncate -s 0 a
TEST ls
cd

cleanup
