#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

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

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

sleep 3

MOUNTDIR=$N0;
TEST mount -t nfs -o vers=3,nolock,soft,timeo=30,retrans=1 $H0:/$V0 $N0
TEST touch $N0/testfile

TEST $CLI volume set $V0 debug.error-gen client
TEST $CLI volume set $V0 debug.error-fops stat
TEST $CLI volume set $V0 debug.error-failure 100

sleep 1

pid_file=$(read_nfs_pidfile);

getfacl $N0/testfile 2>/dev/null

nfs_pid=$(get_nfs_pid);
if [ ! $nfs_pid ]
then
  nfs_pid=0;
fi

TEST [ $nfs_pid -eq $pid_file ]

TEST   umount $MOUNTDIR -l

cleanup;
