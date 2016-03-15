#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 performance.open-behind off;
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume start $V0

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;


useradd tmp_user 2>/dev/null 1>/dev/null;
mkdir $M0/dir;
mkdir $M0/other;
cp /etc/passwd $M0/;
cp $M0/passwd $M0/file;
chmod 600 $M0/file;

TEST mount_nfs $H0:/$V0 $N0 nolock;

chown -R nfsnobody:nfsnobody $M0/dir;
chown -R tmp_user:tmp_user $M0/other;

TEST $CLI volume set $V0 server.root-squash on;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

# create files and directories in the root of the glusterfs and nfs mount
# which is owned by root and hence the right behavior is getting EACCESS
# as the fops are executed as nfsnobody.
touch $M0/foo 2>/dev/null;
TEST [ $? -ne 0 ]
touch $N0/foo 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $M0/new 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $N0/new 2>/dev/null;
TEST [ $? -ne 0 ]
cp $M0/file $M0/tmp_file 2>/dev/null;
TEST [ $? -ne 0 ]
cp $N0/file $N0/tmp_file 2>/dev/null;
TEST [ $? -ne 0 ]
cat $M0/file 2>/dev/null;
TEST [ $? -ne 0 ]
# here read should be allowed because eventhough file "passwd" is owned
# by root, the permissions if the file allow other users to read it.
cat $M0/passwd 1>/dev/null;
TEST [ $? -eq 0 ]
cat $N0/passwd 1>/dev/null;
TEST [ $? -eq 0 ]

# create files and directories should succeed as the fops are being executed
# inside the directory owned by nfsnobody
TEST touch $M0/dir/file;
TEST touch $N0/dir/foo;
TEST mkdir $M0/dir/new;
TEST mkdir $N0/dir/other;
TEST rm -f $M0/dir/file $M0/dir/foo;
TEST rmdir $N0/dir/*;

# create files and directories here should fail as other directory is owned
# by tmp_user.
touch $M0/other/foo 2>/dev/null;
TEST [ $? -ne 0 ]
touch $N0/other/foo 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $M0/other/new 2>/dev/null;
TEST [ $? -ne 0 ]
mkdir $N0/other/new 2>/dev/null;
TEST [ $? -ne 0 ]

userdel tmp_user;
rm -rf /home/tmp_user;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
