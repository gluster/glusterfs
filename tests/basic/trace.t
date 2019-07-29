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

# Now, use the glusterd way of enabling trace
TEST glusterd
TEST $CLI volume create $V0 replica 3  $H0:$B0/${V0}{1,2,3,4,5,6};

TEST $CLI volume set $V0 debug.trace marker
TEST $CLI volume set $V0 debug.log-file yes
#TEST $CLI volume set $V0 debug.log-history yes

TEST $CLI volume start $V0;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

TEST $(dirname $0)/rpc-coverage.sh --no-locks $M1
cp $(dirname ${0})/gfapi/glfsxmp-coverage.c ./glfsxmp.c
build_tester ./glfsxmp.c -lgfapi
./glfsxmp $V0 $H0 > /dev/null
cleanup_tester ./glfsxmp
rm ./glfsxmp.c

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

cleanup;
