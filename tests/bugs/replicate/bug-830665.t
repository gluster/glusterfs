#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

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
TEST $CLI volume set $V0 nfs.disable false

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}

#EXPECT_WITHIN fails the test if the command it executes fails. This function
#returns "" when the file doesn't exist
function friendly_cat {
        if [ ! -f $1 ];
        then
                echo "";
        else
                cat $1;
        fi
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


EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
## Mount NFS
TEST mount_nfs $H0:/$V0 $N0 nolock;

## Create some files and directories
echo "test_data" > $N0/a_file;
mkdir $N0/a_dir;
echo "more_test_data" > $N0/a_dir/another_file;

## Unmount and stop the volume.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
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
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock,actimeo=0;

## The Linux NFS client has a really charming habit of caching stuff right
## after mount, even though we set actimeo=0 above. Life would be much easier
## if NFS developers cared as much about correctness as they do about shaving
## a few seconds off of benchmarks.
ls -l $N0 &> /dev/null;
sleep 5;

## Force entry self-heal.
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST gluster volume heal $V0 full
#ls -lR $N0 > /dev/null;

## Do NOT check through the NFS mount here. That will force a new self-heal
## check, but we want to test whether self-heal already happened.

## Make sure everything's in order on the recreated brick.
EXPECT_WITHIN $HEAL_TIMEOUT 'test_data' friendly_cat $B0/${V0}-0/a_file;
EXPECT_WITHIN $HEAL_TIMEOUT 'more_test_data' friendly_cat $B0/${V0}-0/a_dir/another_file;

if [ "$EXIT_EARLY" = "1" ]; then
	exit 0;
fi

## Finish up
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
