#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 granular-entry-heal on
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0

TEST mkdir -p $M0/a/b/c -p
cd $M0/a/b/c

TEST kill_brick $V0 $H0 $B0/${V0}2
rm -rf $B0/${V0}2/*
rm -rf $B0/${V0}2/.glusterfs
TEST $CLI volume start $V0 force

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST touch file

GFID_C=$(get_gfid_string $M0/a/b/c)
TEST stat $B0/${V0}0/.glusterfs/indices/entry-changes/$GFID_C/file
TEST stat $B0/${V0}1/.glusterfs/indices/entry-changes/$GFID_C/file

EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}0/a/b/c trusted.afr.$V0-client-2 entry
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/a/b/c trusted.afr.$V0-client-2 entry

cd ~

cleanup
