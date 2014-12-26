#!/bin/bash

#This tests that mknod and create fops mark necessary pending changelog
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST kill_brick $V0 $H0 $B0/${V0}0
cd $M0
TEST mkfifo fifo
TEST mknod block b 0 0
TEST touch a
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/fifo trusted.afr.$V0-client-0 data
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/fifo trusted.afr.$V0-client-0 entry
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/fifo trusted.afr.$V0-client-0 metadata
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/block trusted.afr.$V0-client-0 data
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/block trusted.afr.$V0-client-0 entry
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/block trusted.afr.$V0-client-0 metadata
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/a trusted.afr.$V0-client-0 data
EXPECT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/a trusted.afr.$V0-client-0 entry
EXPECT_NOT "00000000" afr_get_specific_changelog_xattr $B0/${V0}1/a trusted.afr.$V0-client-0 metadata
cleanup
