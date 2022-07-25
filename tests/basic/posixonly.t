#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST mkdir -p $B0/posixonly
cat > $B0/posixonly.vol <<EOF
volume test
  type mgmt/volfile-server
  option transport-type socket
  option volspec-directory $B0
end-volume

volume poisxonly
  type storage/posix
  option directory $B0/posixonly
end-volume

EOF

cat > $B0/test-volfile-server.vol <<EOF
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

TEST glusterfs -s localhost --volfile-id test-volfile-server $M1;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

cleanup;
