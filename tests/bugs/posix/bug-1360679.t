#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup
function num_entries {
        ls -l $1 | wc -l
}

function create_unlink_entry {
	for i in {0..1}
	do
		mkdir -p $B0/${V0}$i/.glusterfs/unlink/{1..3}/{1..10}/1
		dd if=/dev/urandom of=$B0/${V0}$i/.glusterfs/unlink/file-1 bs=1M count=1
		dd if=/dev/urandom of=$B0/${V0}$i/.glusterfs/unlink/file-2 bs=1M count=1
		dd if=/dev/urandom of=$B0/${V0}$i/.glusterfs/unlink/1/file-1 bs=1M count=1
		dd if=/dev/urandom of=$B0/${V0}$i/.glusterfs/unlink/2/file-1 bs=1M count=1
		dd if=/dev/urandom of=$B0/${V0}$i/.glusterfs/unlink/3/file-1 bs=1M count=1
		ln $B0/${V0}$i/.glusterfs/unlink/file-1 $B0/${V0}$i/.glusterfs/unlink/file-link
		ln -s $B0/${V0}$i/.glusterfs/unlink/1 $B0/${V0}$i/.glusterfs/unlink/link
		ln -s $B0/${V0}$i/.glusterfs/unlink/2 $B0/${V0}$i/.glusterfs/unlink/link-2
	done
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 6 redundancy 2 $H0:$B0/${V0}{0..5}
TEST $CLI volume start $V0
TEST $CLI volume stop $V0
create_unlink_entry
TEST $CLI volume start $V0
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}0/.glusterfs/unlink/
EXPECT_WITHIN $UNLINK_TIMEOUT "^1$" num_entries $B0/${V0}1/.glusterfs/unlink/
cleanup;
