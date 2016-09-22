#!/bin/bash
. $(dirname $0)/../../../include.rc
. $(dirname $0)/../../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume heal $V0 granular-entry-heal enable

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;

# Create files
for i in {1..5}
do
        echo $i > $M0/file$i.txt
done

# Metadata changes
TEST setfattr -n user.test -v qwerty $M0/file5.txt

# Replace brick1
TEST $CLI volume replace-brick $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}1_new commit force

# Replaced-brick should accuse the non-replaced-brick (Simulating case for data-loss)
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/${V0}1_new/

# Check if data, metadata and entry segments of changelog are set for replaced-brick
EXPECT "000000010000000100000001" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0

# Also ensure we don't mistakenly tamper with the new brick's changelog xattrs
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1_new

# Ensure the dirty xattr is set on the new brick.
EXPECT "000000000000000000000001" get_hex_xattr trusted.afr.dirty $B0/${V0}1_new

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

# Check if entry-heal has happened
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}1_new | sort)

# To make sure that files were not lost from brick0
TEST diff <(ls $B0/${V0}0 | sort) <(ls $B0/${V0}1 | sort)
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0

# Test if data was healed
TEST diff $B0/${V0}0/file1.txt $B0/${V0}1_new/file1.txt
# To make sure that data was not lost from brick0
TEST diff $B0/${V0}0/file1.txt $B0/${V0}1/file1.txt

# Test if metadata was healed and exists on both the bricks
EXPECT "qwerty" get_text_xattr user.test $B0/${V0}1_new/file5.txt
EXPECT "qwerty" get_text_xattr user.test $B0/${V0}0/file5.txt

EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1_new
EXPECT "000000000000000000000000" get_hex_xattr trusted.afr.dirty $B0/${V0}1_new

cleanup
