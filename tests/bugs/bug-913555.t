#!/bin/bash

# Test that a volume becomes unwritable when the cluster loses quorum.

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function vglusterd {
	wd=$1/wd-$2
	cp -r /var/lib/glusterd $wd
	rm -rf $wd/peers/* $wd/vols/*
	echo -n "UUID=$(uuidgen)\noperating-version=1\n" > $wd/glusterd.info
	opt1="management.transport.socket.bind-address=127.0.0.$2"
	opt2="management.working-directory=$wd"
	glusterd --xlator-option $opt1 --xlator-option $opt2
}

function check_fs {
	df $1 &> /dev/null
	echo $?
}

function check_peers {
	$VCLI peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup;

topwd=$(mktemp -d)
trap "rm -rf $topwd" EXIT

vglusterd $topwd 100
VCLI="$CLI --remote-host=127.0.0.100"
vglusterd $topwd 101
TEST $VCLI peer probe 127.0.0.101
vglusterd $topwd 102
TEST $VCLI peer probe 127.0.0.102

EXPECT_WITHIN 20 2 check_peers

create_cmd="$VCLI volume create $V0"
for i in $(seq 100 102); do
	mkdir -p $B0/$V0$i
	create_cmd="$create_cmd 127.0.0.$i:$B0/$V0$i"
done

TEST $create_cmd
TEST $VCLI volume set $V0 cluster.server-quorum-type server
TEST $VCLI volume start $V0
TEST glusterfs --volfile-server=127.0.0.100 --volfile-id=$V0 $M0

# Kill one pseudo-node, make sure the others survive and volume stays up.
kill -9 $(ps -ef | grep gluster | grep 127.0.0.102 | awk '{print $2}')
EXPECT_WITHIN 20 1 check_peers
fs_status=$(check_fs $M0)
nnodes=$(pidof glusterfsd | wc -w)
TEST [ "$fs_status" = 0 -a "$nnodes" = 2 ]

# Kill another pseudo-node, make sure the last one dies and volume goes down.
kill -9 $(ps -ef | grep gluster | grep 127.0.0.101 | awk '{print $2}')
EXPECT_WITHIN 20 0 check_peers
fs_status=$(check_fs $M0)
nnodes=$(pidof glusterfsd | wc -w)
TEST [ "$fs_status" = 1 -a "$nnodes" = 0 ]

cleanup
