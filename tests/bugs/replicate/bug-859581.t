#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2}
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --direct-io-mode=yes --use-readdirp=no --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

mkdir -p $M0/dir1/dir2

TEST rm -f $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1")
TEST rmdir $B0/${V0}1/dir1/dir2

TEST stat $M0/dir1/dir2

TEST [ -d $B0/${V0}1/dir1/dir2 ]
TEST [ ! -d $(gf_get_gfid_backend_file_path $B0/${V0}1 "dir1") ]

# Stop the volume to flush caches and force symlink recreation
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
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

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup

