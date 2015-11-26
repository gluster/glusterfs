#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup
function num_entries {
        ls -l $1 | wc -l
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

fd1=`fd_available`
TEST fd_open $fd1 'w' $M0/file1
TEST fd_write $fd1 testing
TEST cat  $M0/file1
TEST rm -rf $M0/file1
TEST fd_write $fd1 testing
TEST fd_write $fd1 testing
TEST fd_write $fd1 testing

EXPECT "^2$" num_entries $B0/${V0}0/.glusterfs/unlink/
EXPECT "^2$" num_entries $B0/${V0}1/.glusterfs/unlink/
EXPECT "^2$" num_entries $B0/${V0}2/.glusterfs/unlink/
EXPECT "^2$" num_entries $B0/${V0}3/.glusterfs/unlink/
EXPECT "^2$" num_entries $B0/${V0}4/.glusterfs/unlink/
EXPECT "^2$" num_entries $B0/${V0}5/.glusterfs/unlink/
TEST fd_close $fd1;
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}0/.glusterfs/unlink/
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}1/.glusterfs/unlink/
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}2/.glusterfs/unlink/
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}3/.glusterfs/unlink/
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}4/.glusterfs/unlink/
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}5/.glusterfs/unlink/

cleanup;
