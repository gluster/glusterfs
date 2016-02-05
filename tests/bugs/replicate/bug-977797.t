#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 open-behind off
TEST $CLI volume set $V0 quick-read off
TEST $CLI volume set $V0 read-ahead off
TEST $CLI volume set $V0 write-behind off
TEST $CLI volume set $V0 io-cache off

TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0


TEST mkdir -p $M0/a
TEST `echo "GLUSTERFS" > $M0/a/file`

TEST kill_brick $V0 $H0 $B0/$V0"1"

TEST chown root $M0/a
TEST chown root $M0/a/file
TEST `echo "GLUSTER-FILE-SYSTEM" > $M0/a/file`
TEST mkdir $M0/a/b

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0;



TEST kill_brick $V0 $H0 $B0/$V0"2"

TEST chmod 757 $M0/a
TEST chmod 757 $M0/a/file

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1;

dd if=$M0/a/file of=/dev/null bs=1024k
#read fails, but heal is triggered.
TEST [ $? -ne 0 ]

EXPECT_WITHIN $HEAL_TIMEOUT "00000000"  \
afr_get_specific_changelog_xattr $B0/$V0"1"/a/file trusted.afr.$V0-client-0 "data"

EXPECT_WITHIN $HEAL_TIMEOUT "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"1"/a/file trusted.afr.$V0-client-1 "data"

EXPECT_WITHIN $HEAL_TIMEOUT "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"2"/a/file trusted.afr.$V0-client-0 "data"

EXPECT_WITHIN $HEAL_TIMEOUT  "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"2"/a/file trusted.afr.$V0-client-1 "data"

EXPECT_WITHIN $HEAL_TIMEOUT "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"1"/a trusted.afr.$V0-client-0 "entry"

EXPECT_WITHIN HEAL_TIMEOUT "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"1"/a trusted.afr.$V0-client-1 "entry"

EXPECT_WITHIN $HEAL_TIMEOUT "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"2"/a trusted.afr.$V0-client-0 "entry"

EXPECT_WITHIN $HEAL_TIMEOUT  "00000000" \
afr_get_specific_changelog_xattr $B0/$V0"2"/a trusted.afr.$V0-client-1 "entry"

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
