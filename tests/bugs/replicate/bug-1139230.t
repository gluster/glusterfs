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

# Volume start
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

# FUSE Mount
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST mkdir -p $M0/one

# Kill a brick
TEST kill_brick $V0 $H0 $B0/${V0}-1

TEST `echo "A long" > $M0/one/two`

# Start force
TEST $CLI volume start $V0 force

EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1

EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_dir_heal_done $B0/${V0}-0 $B0/${V0}-1 one
EXPECT_WITHIN $HEAL_TIMEOUT "Y" is_file_heal_done $B0/${V0}-0 $B0/${V0}-1 one/two

# Pending xattrs should be set for all the bricks once self-heal is done
# Check pending xattrs
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-0/one trusted.afr.$V0-client-0
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-0/one trusted.afr.$V0-client-1
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-1/one trusted.afr.$V0-client-0
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-1/one trusted.afr.$V0-client-1
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-0/one trusted.afr.dirty
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-1/one trusted.afr.dirty

TEST `echo "time ago" > $M0/one/three`

# Pending xattrs should be set for all the bricks once transaction is done
# Check pending xattrs
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-0/one/three trusted.afr.$V0-client-0
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-0/one/three trusted.afr.$V0-client-1
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-1/one/three trusted.afr.$V0-client-0
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-1/one/three trusted.afr.$V0-client-1
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-0/one/three trusted.afr.dirty
EXPECT "0x000000000000000000000000" afr_get_changelog_xattr $B0/${V0}-1/one/three trusted.afr.dirty

cleanup;
