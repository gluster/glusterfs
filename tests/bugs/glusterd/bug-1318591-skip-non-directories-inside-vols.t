#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../env.rc
. $(dirname $0)/../../snapshot.rc

cleanup;

TEST verify_lvm_version
TEST glusterd
TEST pidof glusterd

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0

TEST $CLI volume status $V0;

TEST touch $GLUSTERD_WORKDIR/vols/file

TEST $CLI snapshot create snap1 $V0 no-timestamp

TEST touch $GLUSTERD_WORKDIR/snaps/snap1/file

TEST killall_gluster

TEST glusterd

cleanup;
