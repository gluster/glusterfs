#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST mkdir -p $B0/single-brick
cat > $B0/template.vol <<EOF
volume posix
  type storage/posix
  option directory $B0/single-brick
end-volume

volume trace
  type debug/trace
  option log-file yes
  option log-history yes
  subvolumes posix
end-volume
EOF

TEST glusterfs -f $B0/template.vol $M0

TEST $(dirname $0)/rpc-coverage.sh --no-locks $M0

# Take statedump to get maximum code coverage
pid=$(ps auxww | grep glusterfs | grep -E "template.vol" | awk '{print $2}' | head -1)
TEST generate_statedump $pid

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

cleanup;
