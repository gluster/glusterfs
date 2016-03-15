#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}

TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2;
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 nfs.disable false

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

MOUNTDIR=$N0;
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
TEST mount_nfs $H0:/$V0 $N0 nolock,timeo=30,retrans=1
TEST touch $N0/testfile

TEST $CLI volume set $V0 debug.error-gen client
TEST $CLI volume set $V0 debug.error-fops stat
TEST $CLI volume set $V0 debug.error-failure 100

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

pid_file=$(read_nfs_pidfile);

getfacl $N0/testfile 2>/dev/null

nfs_pid=$(get_nfs_pid);
if [ ! $nfs_pid ]
then
  nfs_pid=0;
fi

TEST [ $nfs_pid -eq $pid_file ]

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $MOUNTDIR

cleanup;
