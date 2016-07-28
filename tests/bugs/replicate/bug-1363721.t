#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

FILE_UPDATE_TIMEOUT=20
cleanup

function size_increased {
        local file=$1
        local size=$2
        local new_size=$(stat -c%s $file)
        if [ $new_size -gt $size ];
        then
                echo "Y"
        else
                echo "N"
        fi
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0 --direct-io-mode=enable

cd $M0

# Start writing to a file.
(dd if=/dev/urandom of=$M0/file1 bs=1k 2>/dev/null 1>/dev/null)&
dd_pid=$!

# Let IO happen
EXPECT_WITHIN $FILE_UPDATE_TIMEOUT "Y" size_increased file1 0

# Now kill the zeroth brick
kill_brick $V0 $H0 $B0/${V0}0

# Let IO continue
EXPECT_WITHIN $FILE_UPDATE_TIMEOUT "Y" size_increased file1 $(stat -c%s file1)

# Now bring the brick back up
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

# Let IO continue
EXPECT_WITHIN $FILE_UPDATE_TIMEOUT "Y" size_increased file1 $(stat -c%s file1)

# Now kill the first brick
kill_brick $V0 $H0 $B0/${V0}1

# Let IO continue
EXPECT_WITHIN $FILE_UPDATE_TIMEOUT "Y" size_increased file1 $(stat -c%s file1)

# Now bring the brick back up
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

# Let IO continue for 3 seconds
sleep 3

# Now kill the second brick
kill_brick $V0 $H0 $B0/${V0}2

# At this point the write should have been failed. But make sure that the second
# brick is never an accused.

md5sum_2=$(md5sum $B0/${V0}2/file1 | awk '{print $1}')

EXPECT_NOT "$md5sum_2" echo `md5sum $B0/${V0}0/file1 | awk '{print $1}'`
EXPECT_NOT "$md5sum_2" echo `md5sum $B0/${V0}1/file1 | awk '{print $1}'`

EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}0/file1 trusted.afr.dirty data
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/file1 trusted.afr.dirty data

EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}0/file1 trusted.afr.$V0-client-2 data
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/file1 trusted.afr.$V0-client-2 data
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}2/file1 trusted.afr.$V0-client-2 data
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}0/file1 trusted.afr.$V0-client-2 metadata
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/file1 trusted.afr.$V0-client-2 metadata
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}2/file1 trusted.afr.$V0-client-2 metadata

# Now bring the brick back up
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2

# Enable shd
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2

TEST $CLI volume heal $V0

# Wait for heal to complete
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0

EXPECT "$md5sum_2" echo `md5sum $B0/${V0}0/file1 | awk '{print $1}'`
EXPECT "$md5sum_2" echo `md5sum $B0/${V0}1/file1 | awk '{print $1}'`
EXPECT "$md5sum_2" echo `md5sum $B0/${V0}2/file1 | awk '{print $1}'`

cd ~

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup
