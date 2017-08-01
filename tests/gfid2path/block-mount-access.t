#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Create a 2*2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start the volume
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

## Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

#CREATE
fname=$M0/file1
touch $fname;
backpath=$B0/${V0}1/file1

pgfid="00000000-0000-0000-0000-000000000001"

#Check for the presence of xattr
key="trusted.gfid2path"
gfid2path_xattr=$(getfattr -h -d -m. $backpath 2>/dev/null | grep -a $key | cut -f1 -d'=')

#Check getxattr
TEST ! getfattr -h -n $gfid2path_xattr $M0/file1

#Check listgetxattr
EXPECT_NOT $gfid2path_xattr get_xattr_key $key $M0/file1

#Check removexattr
TEST ! setfattr -h -x $gfid2path_xattr $M0/file1

#Check setxattr
TEST ! setfattr -h -n "trusted.gfid2path.d16e15bafe6e4257" -v "$pgfid/file2" $M0/file1

#Cleanup
cleanup;
