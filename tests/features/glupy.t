#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST mkdir -p $B0/glupytest
cat > $B0/glupytest.vol <<EOF
volume vol-posix
    type storage/posix
    option directory $B0/glupytest
end-volume

volume vol-glupy
    type features/glupy
    option module-name helloworld
    subvolumes vol-posix
end-volume
EOF

TEST glusterfs -f $B0/glupytest.vol $M0;

TEST touch $M0/filename;
EXPECT "filename" ls $M0
TEST rm -f $M0/filename;

TEST umount -l $M0;

cleanup;
