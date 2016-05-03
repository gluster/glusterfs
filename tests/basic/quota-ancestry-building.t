#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fileio.rc

cleanup;
# This tests quota enforcing on an inode without any path information.
# This should cover anon-fd type of workload as well.

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/quota.c -o $QDD

TESTS_EXPECTED_IN_LOOP=8
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/brick1 $H0:$B0/brick2;

TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0;
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 1B
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

deep=/0/1/2/3/4/5/6/7/8/9
TEST mkdir -p $M0/$deep

TEST touch $M0/$deep/file1 $M0/$deep/file2 $M0/$deep/file3 $M0/$deep/file4

TEST fd_open 3 'w' "$M0/$deep/file1"
TEST fd_open 4 'w' "$M0/$deep/file2"
TEST fd_open 5 'w' "$M0/$deep/file3"
TEST fd_open 6 'w' "$M0/$deep/file4"

# consume all quota
TEST ! $QDD $M0/$deep/file 256 4

# simulate name-less lookups for re-open where the parent information is lost.
# Stopping and starting the bricks will trigger client re-open which happens on
# a gfid without any parent information. Since no operations are performed on
# the fds {3..6} every-xl will be under the impression that they are good fds

TEST $CLI volume stop $V0
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

for i in $(seq 3 6); do
# failing writes indicate that we are enforcing quota set on /
TEST_IN_LOOP ! fd_write $i "content"
TEST_IN_LOOP sync
done

exec 3>&-
exec 4>&-
exec 5>&-
exec 6>&-

TEST $CLI volume stop $V0
EXPECT "1" get_aux

rm -f $QDD
cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1332020
