#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd

## Create a 1*2 volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start the volume
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

## Mount the volume
TEST $GFS --volfile-server=$H0 --aux-gfid-mount --volfile-id=$V0 $M0;

root_gfid="00000000-0000-0000-0000-000000000001"

#Check for ROOT
EXPECT "/" get_gfid2path $M0/.gfid/$root_gfid

#CREATE
fname=$M0/file1
touch $fname;

#Get gfid of file1
gfid=$(getfattr -h --only-values -n glusterfs.gfid.string $M0/file1)

#Get path from virt xattr
EXPECT "/file1" get_gfid2path $M0/.gfid/$gfid

#Create hardlink and get path
ln $fname $M0/hl_file1
EXPECT "/file1" get_gfid2path $M0/.gfid/$gfid
EXPECT "/hl_file1" get_gfid2path $M0/.gfid/$gfid

#Rename and get path
mv $fname $M0/rn_file1
EXPECT "/hl_file1" get_gfid2path $M0/.gfid/$gfid
EXPECT "/rn_file1" get_gfid2path $M0/.gfid/$gfid

#Create symlink and get path
ln -s $fname $M0/sym_file1
gfid=$(getfattr -h --only-values -n glusterfs.gfid.string $M0/sym_file1)
EXPECT "/sym_file1" get_gfid2path $M0/.gfid/$gfid

#Create dir and get path
mkdir -p $M0/dir1/dir2
gfid=$(getfattr -h --only-values -n glusterfs.gfid.string $M0/dir1/dir2)
EXPECT "/dir1/dir2" get_gfid2path $M0/.gfid/$gfid

#Create file under dir2 and get path
touch $M0/dir1/dir2/file1
gfid=$(getfattr -h --only-values -n glusterfs.gfid.string $M0/dir1/dir2/file1)
EXPECT "/dir1/dir2/file1" get_gfid2path $M0/.gfid/$gfid

#Create hardlink under dir2 and get path
ln $M0/dir1/dir2/file1 $M0/dir1/hl_file1
gfid=$(getfattr -h --only-values -n glusterfs.gfid.string $M0/dir1/dir2/file1)
EXPECT "/dir1/dir2/file1" get_gfid2path $M0/.gfid/$gfid
EXPECT "/dir1/hl_file1" get_gfid2path $M0/.gfid/$gfid

cleanup;
