#!/bin/bash

. $(dirname $0)/../include.rc
cleanup

function recreate {
	# The rm is necessary so we don't get fooled by leftovers from old runs.
	rm -rf $1 && mkdir -p $1
}

function count_bricks {
	local count
	local pid
	count=0
	for pid in /var/lib/glusterd/vols/${1}/run/*pid; do
		if kill -0 $(cat $pid); then
			count=$((count+1))
		fi
	done
	echo $count
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
TEST recreate ${B0}/${V0}-0
TEST recreate ${B0}/${V0}-1
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}-{0,1}

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify that all bricks start.
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT 2 count_bricks $V0
TEST $CLI volume stop $V0

# Nuke one of the bricks and make sure it *doesn't* start.
TEST recreate ${B0}/${V0}-1
# We can't do the usual TEST/startup thing here because of another bug.  If
# a server fails to start a brick, it won't start any others either.  Since
# all of our bricks in testing are on one server, that means no bricks start
# and so the volume doesn't start either.  Changing the order etc. doesn't
# help, because the attempted startup order is non-deterministic.  Instead,
# we just don't rely on whether or not the volume starts; the brick count is
# sufficient for our purposes.
$CLI volume start $V0;
EXPECT 1 count_bricks $V0
# If we can't depend on the volume starting, we can't depend on it stopping
# either.
$CLI volume stop $V0

# Label the recreated brick and make sure it starts now.
TEST $CLI volume label $V0 ${H0}:${B0}/${V0}-1
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
EXPECT 2 count_bricks $V0

# Make sure we can mount and use the volume.
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0
TEST dd if=/dev/zero of=$M0/block bs=4k count=1

if [ "$EXIT_EARLY" = "1" ]; then
	exit 0;
fi

## Finish up
TEST umount $M0
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
