#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

function file_count()
{
        val=1

        if [ "$1" == "0" ]
        then
                if [ "$2" == "0" ]
                then
                        val=0
                fi
        fi
        echo $val
}

BRICK_COUNT=2

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
## Mount nfs, with nocache option
TEST mount_nfs $H0:/$V0 $M0 nolock,noac;

touch $M0/files{1..1000};

# Kill a brick process
kill_brick $V0 $H0 $B0/${V0}0

drop_cache $M0

ls -l $M0 >/dev/null;

NEW_FILE_COUNT=`echo $?`;

TEST $CLI volume start $V0 force

# Kill a brick process
kill_brick $V0 $H0 $B0/${V0}1

drop_cache $M0

ls -l $M0 >/dev/null;

NEW_FILE_COUNT1=`echo $?`;

EXPECT "0" file_count $NEW_FILE_COUNT $NEW_FILE_COUNT1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup
