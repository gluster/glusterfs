#!/bin/bash
#Self-heal tests

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume attach-tier $V0 replica 2 $H0:$B0/brick{2,3}
TEST $CLI volume bitrot $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count
TEST $CLI volume bitrot $V0 scrub-frequency hourly
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST dd if=/dev/urandom of=$M0/FILE bs=1024 count=1

#Corrupt file from back-end
TEST stat $B0/brick3/FILE
echo "Corrupted data" >> $B0/brick3/FILE
#Manually set bad-file xattr since we can't wait for an hour.
TEST setfattr -n trusted.bit-rot.bad-file -v 0x3100 $B0/brick3/FILE

TEST $CLI volume stop $V0
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick3
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 3
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count
#Trigger lookup so that bitrot xlator marks file as bad in its inode context.
stat $M0/FILE
# Remove hot-tier
TEST $CLI volume tier $V0  detach start
sleep 1
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" detach_tier_status_field_complete $V0
TEST $CLI volume tier $V0  detach commit
#Test that file has migrated to cold tier.
EXPECT "1024" stat -c "%s" $B0/brick0/FILE
EXPECT "1024" stat -c "%s" $B0/brick1/FILE
TEST umount $M0
cleanup
