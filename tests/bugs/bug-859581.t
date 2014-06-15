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
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1

TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

mkdir -p $M0/dir1/dir2

TEST rm -f $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1")
TEST rmdir $B0/${V0}1/dir1/dir2

TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "Y" path_exists $B0/${V0}1/dir1/dir2
EXPECT_WITHIN $HEAL_TIMEOUT "0" afr_get_pending_heal_count $V0

TEST [ -d $B0/${V0}1/dir1/dir2 ]
TEST [ ! -d $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1") ]

# Stop the volume to flush caches and force symlink recreation
TEST umount $M0
TEST $CLI volume stop $V0
EXPECT 'Stopped' volinfo_field $V0 'Status';
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

# Till now, protocol/server was not doing inode linking as part of readdirp.
# But pas part of user servicable snapshots patcth, changes to do inode linking
# in protocol/server in readdirp,  were introduced. So now to make sure
# the gfid handle of dir1 is healed, explicit lookup has to be sent on it.
# Otherwise, whenever ls -l is done just on the mount point $M0, lookup on the
# entries received as part of readdirp, is not sent, because the inodes for
# those entries were linked as part of readdirp itself. i.e instead of doing
# "ls -l $M0", it has to be the below command.
ls -l $M0/dir1;

TEST [ -h $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1") ]

TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup

