#!/bin/sh

# This script can be used to provoke 35 fops (if afr is used),
# 28 fops (if afr is not used) (-fstat,-readdirp, and lk,xattrop calls)
# Pending are 7 procedures.
# getspec, fsyncdir, access, fentrylk, fsetxattr, fgetxattr, rchecksum
# TODO: add commands which can generate fops for missing fops

## Script tests below File Operations over RPC (when afr is used)

# CREATE
# ENTRYLK
# FINODELK
# FLUSH
# FSTAT
# FSYNC
# FTRUNCATE
# FXATTROP
# GETXATTR
# INODELK
# LINK
# LK
# LOOKUP
# MKDIR
# MKNOD
# OPEN
# OPENDIR
# READ
# READDIR
# READDIRP
# READLINK
# RELEASE
# RELEASEDIR
# REMOVEXATTR
# RENAME
# RMDIR
# SETATTR
# SETXATTR
# STAT
# STATFS
# SYMLINK
# TRUNCATE
# UNLINK
# WRITE
# XATTROP

set -e

MOUNTPOINT=$1;

[ -z $MOUNTPOINT ] && {
    MOUNTPOINT="/mnt/glusterfs";
}
current_dir=$PWD;

TESTDIR="${MOUNTPOINT}/fop-test-$(date +%Y%m%d%H%M%S)"
mkdir $TESTDIR;
cd $TESTDIR;

echo "Lets see open working" >> testfile;
echo "Lets see truncate working 012345678" > testfile;
truncate -s 30 testfile;
mkdir testdir;
mkfifo testfifo;
ln testfile testfile-ln;
ln -sf testfile testfile-symlink;
mv testfile-symlink testfile-symlink-mv;
chmod 0777 testfile;
flock -x testfile.lock dd if=/dev/zero of=testfile1 conv=fsync bs=1M count=2 > /dev/null 2>&1;
cat testfile1 > /dev/null;
ls -lR . > /dev/null;
setfattr -n trusted.rpc.coverage -v testing testfile;
getfattr -d -m . testfile > /dev/null;
setfattr -x trusted.rpc.coverage testfile;
df -h testdir > /dev/null;

rm -f testfifo testfile testfile1 testfile-ln testfile.lock testfile-symlink-mv;
rmdir testdir;

cd $current_dir;

rmdir ${TESTDIR}

