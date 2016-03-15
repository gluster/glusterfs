#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

function check_dir()
{
    local count

    count=`ls $1 | grep "dir.[0-9]*" | wc -l`
    if [[ $count -eq 100 ]]; then
        echo "Y"
    else
        echo "N"
    fi
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 2 $H0:$B0/${V0}{0..5}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
TEST mount_nfs $H0:/$V0 $N0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST mkdir $M0/dir.{1..100}

sleep 2

EXPECT "Y" check_dir $N0

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

cleanup
