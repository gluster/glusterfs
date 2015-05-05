#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../snapshot.rc

function get-op_errno-xml()
{
        $CLI $1 --xml | xmllint --format - | grep opErrno | sed 's/\(<opErrno>\|<\/opErrno>\)//g'
}

cleanup;
TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0
TEST $CLI volume create $V1 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};
TEST $CLI volume start $V1

EXPECT 0 get-op_errno-xml "snapshot create snap1 $V0 no-timestamp"
EXPECT 30806 get-op_errno-xml "snapshot create snap1 imaginary_volume"
EXPECT 30807 get-op_errno-xml "snapshot delete imaginary_snap"
EXPECT 30809 get-op_errno-xml "snapshot restore snap1"
TEST $CLI volume stop $V0
EXPECT 30810 get-op_errno-xml "snapshot create snap1 $V0"
TEST $CLI volume start $V0
EXPECT 30811 get-op_errno-xml "snapshot clone $V0 snap1"
EXPECT 30812 get-op_errno-xml "snapshot create snap1 $V0 no-timestamp"
EXPECT 30815 get-op_errno-xml "snapshot create snap2 $V1 no-timestamp"

EXPECT 0 get-op_errno-xml "snapshot delete snap1"
TEST $CLI volume stop $V0
TEST $CLI volume stop $V1

cleanup;
