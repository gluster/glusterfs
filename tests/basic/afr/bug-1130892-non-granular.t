#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

# Create a 1X2 replica
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1}
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 cluster.granular-entry-heal off

# Disable self-heal daemon
TEST gluster volume set $V0 self-heal-daemon off

# Enable Client side heal
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off

# Disable all perf-xlators
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off

# Volume start
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

# FUSE Mount
TEST ${GFS} -s $H0 --volfile-id $V0 $M0

# Create files and dirs
TEST mkdir -p $M0/one/two/
TEST `echo "Carpe diem" > $M0/one/two/three`

# Simulate disk-replacement
TEST kill_brick $V0 $H0 $B0/${V0}-1
EXPECT_WITHIN ${PROCESS_DOWN_TIMEOUT} "^0$" afr_child_up_status $V0 1
TEST rm -rf $B0/${V0}-1/one
TEST rm -rf $B0/${V0}-1/.glusterfs

#Ideally, disk replacement is done using reset-brick or replace-brick gluster CLI
#which will create .glusterfs folder.
mkdir $B0/${V0}-1/.glusterfs && chmod 600 $B0/${V0}-1/.glusterfs

# Start force
TEST $CLI volume start $V0 force

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

TEST stat $M0/one

sleep 1

# Check pending xattrs
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}-0/one trusted.afr.$V0-client-1 data
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}-0/one trusted.afr.$V0-client-1 entry
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}-0/one trusted.afr.$V0-client-1 metadata

TEST gluster volume set $V0 self-heal-daemon on

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_dir_heal_done $B0/${V0}-0 $B0/${V0}-1 one
EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_dir_heal_done $B0/${V0}-0 $B0/${V0}-1 one/two
EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_file_heal_done $B0/${V0}-0 $B0/${V0}-1 one/two/three

cleanup;
