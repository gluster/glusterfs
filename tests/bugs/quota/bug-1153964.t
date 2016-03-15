#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

function rename_loop()
{
        local i=0
        local limit=$1
        while [ $i -lt $limit ]
        do
                j=$[$i + 1]
                mv $N0/test_dir/file$i $N0/test_dir/file$j
                if [ "$?" != "0" ]
                then
                        return 1
                fi
                i=$[$i + 1]
        done
        return 0
}

function createFile_and_checkLimit()
{
        local count_val=$1;
        dd if=/dev/zero of="$N0/test_dir/file0" bs=1048576 count=$count_val
        sleep 3
        if [ -f $N0/test_dir/file0 ]
        then
                rename_loop 10
                if [ "$?" == "0" ]
                then
                        echo "Y"
                else
                        echo "N"
                fi
        fi
}

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

TEST $CLI volume quota $V0 enable
EXPECT 'on' volinfo_field $V0 'features.quota'

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock;
TEST mkdir -p $N0/test_dir/

# Try to rename file under various case and check if
# quota limit exceeds or not.
TEST $CLI volume quota $V0 limit-usage /test_dir 100MB
# Case1 : If used size is less than hard-limit size
# Create a 600MB file
EXPECT 'Y' createFile_and_checkLimit 60

TEST rm -rf $N0/test_dir/*
# Case2 : If used size is equal to hard-limit size
# Create a 100MB file
EXPECT 'Y' createFile_and_checkLimit 100

TEST rm -rf $N0/test_dir/*
# Case3 : If used size is greater than hard-limit size
# Create a 110MB file
EXPECT 'Y' createFile_and_checkLimit 110

# remove this directory as it has been created as part
# of above testcase
TEST rm -rf $N0/test_dir/

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

cleanup;
