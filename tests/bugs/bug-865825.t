#!/bin/bash

. $(dirname $0)/../include.rc

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
mkdir -p ${B0}/${V0}-0
mkdir -p ${B0}/${V0}-1
mkdir -p ${B0}/${V0}-2
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}-{0,1,2}

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Make sure io-cache and write-behind don't interfere.
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 performance.io-cache off;
TEST $CLI volume set $V0 performance.write-behind off;
TEST $CLI volume set $V0 performance.stat-prefetch off

## Make sure automatic self-heal doesn't perturb our results.
TEST $CLI volume set $V0 cluster.self-heal-daemon off

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount native
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0

## Create a file with some recognizable contents.
echo "test_data" > $M0/a_file;

## Unmount.
TEST umount $M0;

## Mess with the flags as though brick-0 accuses brick-2 while brick-1 is
## missing its brick-2 changelog altogether.
value=0x000000010000000000000000
setfattr -n trusted.afr.${V0}-client-2 -v $value $B0/${V0}-0/a_file
setfattr -x trusted.afr.${V0}-client-2 $B0/${V0}-1/a_file
echo "wrong_data" > $B0/${V0}-2/a_file

## Remount and force a self-heal.
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
stat ${M0}/a_file > /dev/null

## Make sure brick 2 now has the correct contents.
EXPECT "test_data" cat $B0/${V0}-2/a_file

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
