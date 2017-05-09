#!/bin/bash

. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc
. $(dirname $0)/../../../snapshot.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume set $V0 nfs.disable false


TEST $CLI volume start $V0;

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..10} ; do echo "file" > $M0/file$i ; done

# Create file and hard-links
TEST touch $M0/f1
TEST mkdir $M0/dir
TEST ln $M0/f1 $M0/f2
TEST ln $M0/f1 $M0/dir/f3

TEST $CLI snapshot config activate-on-create enable
TEST $CLI volume set $V0 features.uss enable;

TEST $CLI snapshot create snap1 $V0 no-timestamp;

for i in {11..20} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap2 $V0 no-timestamp;
TEST build_tester $(dirname $0)/bug-1447266.c -lgfapi

#Testing strts from here-->

TEST $(dirname $0)/bug-1447266 $V0 $H0 "/.."
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/."
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/../."
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/../.."
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/dir/../."
#Since dir1 is not present, this test should fail
TEST ! $(dirname $0)/bug-1447266 $V0 $H0 "/dir/../dir1"
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/dir/.."
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/.snaps"
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/.snaps/."
#Since snap3 is not present, this test should fail
TEST ! $(dirname $0)/bug-1447266 $V0 $H0 "/.snaps/.././snap3"
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/.snaps/../."
TEST $(dirname $0)/bug-1447266 $V0 $H0 "/.snaps/./snap1/./../snap1/dir/."

cleanup_tester $(dirname $0)/bug-1319374
cleanup;
