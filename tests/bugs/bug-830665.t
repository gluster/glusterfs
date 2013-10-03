#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

function recreate {
	rm -rf $1 && mkdir -p $1
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

## Start and create a volume
recreate ${B0}/${V0}-0
recreate ${B0}/${V0}-1
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

## Make sure stat-prefetch doesn't prevent self-heal checks.
TEST $CLI volume set $V0 performance.stat-prefetch off;

## Make sure automatic self-heal doesn't perturb our results.
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.background-self-heal-count 0

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';


## Wait for volume to register with rpc.mountd
sleep 5;

## Mount NFS
TEST mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0 $N0;

## Create some files and directories
echo "test_data" > $N0/a_file;
mkdir $N0/a_dir;
echo "more_test_data" > $N0/a_dir/another_file;

## Unmount and stop the volume.
TEST umount $N0;
TEST $CLI volume stop $V0;

# Recreate the brick. Note that because of http://review.gluster.org/#change,4202
# we need to preserve and restore the volume ID or else the brick (and thus the
# entire not-very-HA-any-more volume) won't start. When that bug is fixed, we can
# remove the [gs]etxattr calls.
volid=$(getfattr -e hex -n trusted.glusterfs.volume-id $B0/${V0}-0 2> /dev/null \
	| grep = | cut -d= -f2)
rm -rf $B0/${V0}-0;
mkdir $B0/${V0}-0;
setfattr -n trusted.glusterfs.volume-id -v $volid $B0/${V0}-0

## Restart and remount. Note that we use actimeo=0 so that the stat calls
## we need for self-heal don't get blocked by the NFS client.
TEST $CLI volume start $V0;
sleep 5
TEST mount -t nfs -o vers=3,nolock,soft,intr,actimeo=0 $H0:/$V0 $N0;

## The Linux NFS client has a really charming habit of caching stuff right
## after mount, even though we set actimeo=0 above. Life would be much easier
## if NFS developers cared as much about correctness as they do about shaving
## a few seconds off of benchmarks.
ls -l $N0 &> /dev/null;
sleep 5;

## Force entry self-heal.
find $N0 | xargs stat > /dev/null;
#ls -lR $N0 > /dev/null;

## Do NOT check through the NFS mount here. That will force a new self-heal
## check, but we want to test whether self-heal already happened.

## Make sure everything's in order on the recreated brick.
EXPECT 'test_data' cat $B0/${V0}-0/a_file;
EXPECT 'more_test_data' cat $B0/${V0}-0/a_dir/another_file;

if [ "$EXIT_EARLY" = "1" ]; then
	exit 0;
fi

## Finish up
TEST umount $N0;
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
