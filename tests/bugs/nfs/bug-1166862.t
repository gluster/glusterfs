#!/bin/bash
#
# When nfs.mount-rmtab is disabled, it should not get updated.
#
# Based on: bug-904065.t
#

# count the lines of a file, return 0 if the file does not exist
function count_lines()
{
        if [ -n "$1" ]
        then
                $@ 2>/dev/null | wc -l
        else
                echo 0
        fi
}


. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1
EXPECT 'Created' volinfo_field $V0 'Status'
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

# glusterfs/nfs needs some time to start up in the background
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available

# disable the rmtab by settting it to the magic "/-" value
TEST $CLI volume set $V0 nfs.mount-rmtab /-

# before mounting the rmtab should be empty
EXPECT '0' count_lines cat $GLUSTERD_WORKDIR/nfs/rmtab

TEST mount_nfs $H0:/$V0 $N0 nolock
EXPECT '0' count_lines cat $GLUSTERD_WORKDIR/nfs/rmtab

# showmount should list one client
EXPECT '1' count_lines showmount --no-headers $H0

# unmount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

# after resetting the option, the rmtab should get updated again
TEST $CLI volume reset $V0 nfs.mount-rmtab

# before mounting the rmtab should be empty
EXPECT '0' count_lines cat $GLUSTERD_WORKDIR/nfs/rmtab

TEST mount_nfs $H0:/$V0 $N0 nolock
EXPECT '2' count_lines cat $GLUSTERD_WORKDIR/nfs/rmtab

# removing a mount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
EXPECT '0' count_lines cat $GLUSTERD_WORKDIR/nfs/rmtab

cleanup
