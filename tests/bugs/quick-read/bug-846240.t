#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

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

MOUNTDIR=$M0;
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $MOUNTDIR;
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M1;

TEST touch $M0/testfile;

# open the file with the fd as 4
TEST fd=`fd_available`;
TEST fd_open $fd 'w' "$M0/testfile";

# remove the file from the other mount point. If unlink is sent from
# $M0 itself, then the file will be actually opened by open-behind which
# we dont want for this testcase
TEST rm -f $M1/testfile;

# below command opens the file and writes to the file.
# upon open, open-behind unwinds the open call with success.
# now when write comes, open-behind actually opens the file
# and then sends write on the fd. But before sending open itself,
# the file would have been removed from the mount $M1. open() gets error
# and the write call which is put into a stub (open had to be sent first)
# should unwind with the error received in the open call.
echo "data" >> $M0/testfile 2>/dev/null 1>/dev/null;
TEST [ $? -ne 0 ]

TEST fd_close $fd;

TEST rm -rf $MOUNTDIR/*

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $MOUNTDIR

cleanup;
