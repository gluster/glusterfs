#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup
trap cleanup EXIT

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 feature.simple-quota-pass-through false
# Widen the backend persistence window so overlapping statfs updates exercise
# the race reliably rather than depending on the backend's setxattr latency.
TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.delay-duration 10000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable setxattr
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 --client-pid=-14 $M0
TEST $GFS -s $H0 --volfile-id $V0 --client-pid=-14 $M1

TEST mkdir $M0/quota
TEST setfattr -n trusted.glusterfs.namespace -v true $M0/quota
TEST setfattr -n trusted.gfs.squota.limit -v 1073741824 $M0/quota

# Concurrent statfs calls must not lose positive or negative pending deltas.
TEST python3 $(dirname $0)/simple-quota-concurrent-statfs.py $M0/quota $M1/quota
EXPECT 0 get_text_xattr trusted.gfs.squota.size $B0/${V0}1/quota

# A new allocation must still be charged after the namespace returns to zero.
TEST dd if=/dev/zero of=$M0/quota/after bs=1M count=1
TEST df $M0/quota
TEST [ $(get_text_xattr trusted.gfs.squota.size $B0/${V0}1/quota) -ge 1048576 ]
TEST unlink $M0/quota/after
TEST df $M0/quota
EXPECT 0 get_text_xattr trusted.gfs.squota.size $B0/${V0}1/quota
