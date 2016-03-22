#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function write_file()
{
	path="$1"; shift
	echo "$*" > "$path"
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
mkdir -p ${B0}/${V0}-2
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}-{0,1,2}

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Make sure io-cache and write-behind don't interfere.
TEST $CLI volume set $V0 performance.io-cache off;
TEST $CLI volume set $V0 performance.write-behind off;
TEST $CLI volume set $V0 performance.stat-prefetch off

## Make sure automatic self-heal doesn't perturb our results.
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal on

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 cluster.quorum-type none
## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Create a file with some recognizably stale data.
TEST write_file $M0/a_file "old_data"

## Kill two of the bricks and write some newer data.
TEST kill_brick ${V0} ${H0} ${B0}/${V0}-1
TEST kill_brick ${V0} ${H0} ${B0}/${V0}-2
TEST write_file $M0/a_file "new_data"

## Bring all the bricks up and kill one so we do a partial self-heal.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 2
TEST kill_brick ${V0} ${H0} ${B0}/${V0}-2
TEST dd if=${M0}/a_file of=/dev/null


obs_path_0=${B0}/${V0}-0/a_file
obs_path_1=${B0}/${V0}-1/a_file
obs_path_2=${B0}/${V0}-2/a_file

tgt_xattr_0="trusted.afr.${V0}-client-0"
tgt_xattr_1="trusted.afr.${V0}-client-1"
tgt_xattr_2="trusted.afr.${V0}-client-2"

actual=$(afr_get_changelog_xattr $obs_path_0 $tgt_xattr_0)
EXPECT "0x000000000000000000000000|^\$" echo $actual

EXPECT_WITHIN $HEAL_TIMEOUT "0x000000000000000000000000" \
afr_get_changelog_xattr $obs_path_0 $tgt_xattr_1

actual=$(afr_get_changelog_xattr $obs_path_0 $tgt_xattr_2)
EXPECT "0x000000030000000000000000" echo $actual

actual=$(afr_get_changelog_xattr $obs_path_1 $tgt_xattr_0)
EXPECT "0x000000000000000000000000|^\$" echo $actual

actual=$(afr_get_changelog_xattr $obs_path_1 $tgt_xattr_1)
EXPECT "0x000000000000000000000000|^\$" echo $actual

actual=$(afr_get_changelog_xattr $obs_path_1 $tgt_xattr_2)
EXPECT "0x000000010000000000000000" echo $actual

actual=$(afr_get_changelog_xattr $obs_path_2 $tgt_xattr_0)
EXPECT "0x000000000000000000000000|^\$" echo $actual

actual=$(afr_get_changelog_xattr $obs_path_2 $tgt_xattr_1)
EXPECT "0x000000000000000000000000|^\$" echo $actual

actual=$(afr_get_changelog_xattr $obs_path_2 $tgt_xattr_2)
EXPECT "0x000000000000000000000000|^\$" echo $actual

if [ "$EXIT_EARLY" = "1" ]; then
	exit 0;
fi

## Finish up
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
