#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
# Needed to be sure self-heal daemon is running
sleep 5

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

mkdir -p $M0/dir1/dir2

TEST rm -f $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1")
TEST rmdir $B0/${V0}1/dir1/dir2

TEST $CLI volume heal $V0 full
sleep 5

TEST [ -d $B0/${V0}1/dir1/dir2 ]
TEST [ ! -d $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1") ]

# Stop the volume to flush caches and force symlink recreation
TEST umount $M0
TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

ls -l $M0/

TEST [ -h $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1") ]

TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup

