#!/bin/bash
#
# Bug 985074 - Verify stale inode/dentry mappings are cleaned out.
#
# This test verifies that an inode/dentry mapping for a file removed via a
# separate mount point is cleaned up appropriately. We create a file and hard
# link from client 1. Next we remove the link via client 2. Finally, from client
# 1 we attempt to rename the original filename to the name of the just removed
# hard link.
#
# If the inode is not unlinked properly, the removed directory entry can resolve
# to an inode (on the client that never saw the rm) that ends up passed down
# through the lookup call. If md-cache holds valid metadata on the inode (due to
# a large timeout value or recent lookup on the valid name), it is tricked into
# returning a successful lookup that should have returned ENOENT. This manifests
# as an error from the mv command in the following test sequence because file
# and file.link resolve to the same file:
#
# # mv /mnt/glusterfs/0/file /mnt/glusterfs/0/file.link
# mv: `/mnt/glusterfs/0/file' and `/mnt/glusterfs/0/file.link' are the same file
#
###

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/$V0
TEST $CLI volume start $V0
TEST $CLI volume set $V0 md-cache-timeout 3

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0 --entry-timeout=0 --attribute-timeout=0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M1 --entry-timeout=0 --attribute-timeout=0

TEST touch $M0/file
TEST ln $M0/file $M0/file.link
TEST ls -ali $M0 $M1
TEST rm -f $M1/file.link
TEST ls -ali $M0 $M1
# expire the md-cache timeout
sleep 3
TEST mv $M0/file $M0/file.link
TEST stat $M0/file.link
TEST ! stat $M0/file

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
