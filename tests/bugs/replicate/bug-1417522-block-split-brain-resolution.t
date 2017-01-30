#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST touch $M0/file

TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=10
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
TEST kill_brick $V0 $H0 $B0/${V0}2
TEST dd if=/dev/urandom of=$M0/file bs=1024 count=20
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST ! dd if=$M0/file of=/dev/null
SOURCE_BRICK_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)

# Various fav-child policies must not heal the file when some bricks are down.
TEST $CLI volume set $V0 favorite-child-policy size
TEST ! dd if=$M0/file of=/dev/null
TEST $CLI volume set $V0 favorite-child-policy ctime
TEST ! dd if=$M0/file of=/dev/null
TEST $CLI volume set $V0 favorite-child-policy mtime
TEST ! dd if=$M0/file of=/dev/null
TEST $CLI volume set $V0 favorite-child-policy majority
TEST ! dd if=$M0/file of=/dev/null

# CLI/mount based split-brain resolution must also not work.
TEST ! $CLI volume heal $V0 split-brain bigger-file /file
TEST ! $CLI volume heal $V0 split-brain mtime /file
TEST ! $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}2 /file1

TEST ! getfattr -n replica.split-brain-status $M0/file
TEST ! setfattr -n replica.split-brain-choice -v $V0-client-1 $M0/file

# Bring all bricks back up and launch heal.
TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT 0 get_pending_heal_count $V0
B1_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
B2_MD5=$(md5sum $B0/${V0}2/file | cut -d\  -f1)
TEST [ "$SOURCE_BRICK_MD5" == "$B1_MD5" ]
TEST [ "$SOURCE_BRICK_MD5" == "$B2_MD5" ]

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
cleanup;
