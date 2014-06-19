#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST mkdir -p $B0/posixonly
cat > $B0/posixonly.vol <<EOF
volume poisxonly
  type storage/posix
  option directory $B0/posixonly
end-volume
EOF

TEST glusterfs -f $B0/posixonly.vol $M0;

TEST touch $M0/filename;
TEST stat $M0/filename;
TEST mkdir $M0/dirname;
TEST stat $M0/dirname;
TEST touch $M0/dirname/filename;
TEST stat $M0/dirname/filename;
TEST ln $M0/dirname/filename $M0/dirname/linkname;
TEST chown 100:100 $M0/dirname/filename;
TEST chown 100:100 $M0/dirname;
TEST rm -rf $M0/filename $M0/dirname;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup;
