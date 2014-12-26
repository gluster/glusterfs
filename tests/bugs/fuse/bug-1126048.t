#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

function grep_for_ebadf {
        $M0/bug-1126048 "gluster --mode=script --wignore volume add-brick $V0 $H0:$B0/brick2" | grep -i "Bad file descriptor"
}
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs -s $H0 --volfile-id=$V0 $M0 --direct-io-mode=yes

build_tester $(dirname $0)/bug-1126048.c

TEST cp $(dirname $0)/bug-1126048 $M0
cd $M0
TEST grep_for_ebadf
TEST ls -l $M0
cd -
TEST rm -f $(dirname $0)/bug-1126048
cleanup;
