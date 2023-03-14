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

total="$(stat -fc "%b" "${M0}")"
total="$((${total} * 4096))"

# Fill 75% of the total space
TEST fallocate -l "$((${total} * 3 / 4))" "${M0}/a"

gfid_new=$(get_gfid_string $M0/a)

#Setting the size in percentage
TEST $CLI volume set $V0 storage.reserve 40

# Wait 5s to update disk_space_full flag because thread check disk space
# after every 5s
sleep 5

# The available available space is below 40%, so creating new files shouldn't
# be allowed.
TEST ! touch $M0/test

# setup_lvm create lvm partition of 150M and 40% is reserved so after
# consuming more than 75% next unlink should not fail.

# Delete the base shard and check shards get cleaned up
TEST unlink $M0/a
TEST ! stat $M0/a

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
