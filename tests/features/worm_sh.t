#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off

TEST $CLI volume set $V0 self-heal-daemon off
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

## Enable file level WORM
TEST $CLI volume set $V0 features.worm-file-level 1
TEST $CLI volume set $V0 features.default-retention-period 100
TEST $CLI volume set $V0 features.auto-commit-period 5

## Tests for manual transition to WORM/Retained state
TEST `echo "worm1" > $M0/file1`
TEST chmod 0444 $M0/file1
sleep 5
TEST `echo "worm2" > $M0/file2`
TEST chmod 0444 $M0/file2
sleep 5
TEST `echo "worm3" > $M0/file3`
TEST chmod 0444 $M0/file3
sleep 5

## Stopp one of the bricks
TEST kill_brick $V0 $H0 $B0/${V0}1

## Manipulate the WORMed-Files
TEST $CLI volume set $V0 features.worm-file-level 0
sleep 5

TEST chmod 0777 $M0/file1
TEST `echo "test" >> $M0/file1`
TEST `echo "test" >> $M0/file3`
TEST `rm -rf $M0/file2`

## Metadata changes
TEST setfattr -n user.test -v qwerty $M0/file3
sleep 5

## Enable file level WORM again
TEST $CLI volume set $V0 features.worm-file-level 1

## Restart volume and trigger self-heal
TEST $CLI volume stop $V0 force
TEST $CLI volume start $V0 force
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

# Check if entry-heal has happened
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}1 | sort)

# Test if data was healed
TEST diff $B0/${V0}0/file1 $B0/${V0}1/file1
TEST diff $B0/${V0}0/file3 $B0/${V0}1/file3

# Test if metadata was healed and exists on both the bricks
EXPECT "qwerty" get_text_xattr user.test $B0/${V0}1/file3
EXPECT "qwerty" get_text_xattr user.test $B0/${V0}0/file3

cleanup;
