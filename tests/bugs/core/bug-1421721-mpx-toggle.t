#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

write_a_file () {
	echo $1 > $2
}

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}[0,1]

TEST $CLI volume set all cluster.brick-multiplex on
TEST $CLI volume start $V0

TEST $GFS -s $H0 --volfile-id=$V0 $M0
TEST write_a_file "hello" $M0/a_file

TEST force_umount $M0
TEST $CLI volume stop $V0

TEST $CLI volume set all cluster.brick-multiplex off
TEST $CLI volume start $V0

cleanup
