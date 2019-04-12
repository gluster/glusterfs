#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST mkdir -p $B0/test
cat > $B0/test.vol <<EOF
volume test
  type storage/posix
  option directory $B0/test
  option multiple-line-string "I am
 testing a feature of volfile graph.l"
  option single-line-string "this is running on $H0"
  option option-with-back-tick `date +%Y%M%d`
end-volume
EOF

# This should succeed, but it will have some unknown options, which is OK.
TEST glusterfs -f $B0/test.vol $M0;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0;

# This should not succeed
cat > $B0/test.vol <<EOF
volume test
  type storage/posix
EOF
TEST ! glusterfs -f $B0/test.vol $M0;


# This should not succeed
cat > $B0/test.vol <<EOF
  type storage/posix
end-volume
EOF
TEST ! glusterfs -f $B0/test.vol $M0;

# This should not succeed
cat > $B0/test.vol <<EOF
volume test
end-volume
EOF
TEST ! glusterfs -f $B0/test.vol $M0;

# This should not succeed
cat > $B0/test.vol <<EOF
volume test
  option test and test
end-volume
EOF
TEST ! glusterfs -f $B0/test.vol $M0;

# This should not succeed
cat > $B0/test.vol <<EOF
volume test
  subvolumes
end-volume
EOF
TEST ! glusterfs -f $B0/test.vol $M0;

# This should not succeed
cat > $B0/test.vol <<EOF
volume test
  type storage/posix
  new-option key value
  option directory $B0/test
end-volume
EOF
TEST ! glusterfs -f $B0/test.vol $M0;

cleanup;
