#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function write_file()
{
	path="$1"; shift
	echo "$*" > "$path"
}

function check_xattrs()
{
	result=""

	for observer in 0 1 2; do
		obs_path=${B0}/${V0}-$observer/a_file
		for target in 0 1 2; do
			tgt_xattr="trusted.afr.${V0}-client-$target"
			actual=$(afr_get_changelog_xattr $obs_path $tgt_xattr)
			if [ $observer -ne 2 -a $target -eq 2 ]; then
				expected=0x000000020000000000000000
			else 
				expected=0x000000000000000000000000
			fi
			if [ "$actual" = "$expected" ]; then
				result="${result}y"
			else
				result="${result}n"
			fi
		done
	done

	echo $result
}
	
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
TEST $CLI volume set $V0 cluster.background-self-heal-count 0

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

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
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 0
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 1
EXPECT_WITHIN 20 "1" afr_child_up_status $V0 2
TEST kill_brick ${V0} ${H0} ${B0}/${V0}-2
TEST ls -l ${M0}/a_file

EXPECT "yyyyyyyyy" check_xattrs

if [ "$EXIT_EARLY" = "1" ]; then
	exit 0;
fi

## Finish up
TEST umount $M0;
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
