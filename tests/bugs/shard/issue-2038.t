#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc

cleanup

FILE_COUNT_TIME=5

function get_file_count {
    ls $1* | wc -l
}

TEST verify_lvm_version
TEST glusterd
TEST pidof glusterd
TEST init_n_bricks 1
TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0

$CLI volume info

TEST $CLI volume set $V0 features.shard on
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

#Setting the size in percentage
TEST $CLI volume set $V0 storage.reserve 40

#wait 5s to reset disk_space_full flag
sleep 5

TEST touch $M0/test
TEST unlink $M0/test

TEST dd if=/dev/zero of=$M0/a bs=80M count=1
TEST dd if=/dev/zero of=$M0/b bs=10M count=1

gfid_new=$(get_gfid_string $M0/a)

# Wait 5s to update disk_space_full flag because thread check disk space
# after every 5s

sleep 5
# setup_lvm create lvm partition of 150M and 40M are reserve so after
# consuming more than 110M next unlink should not fail
# Delete the base shard and check shards get cleaned up
TEST unlink $M0/a
TEST ! stat $M0/a

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
