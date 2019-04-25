#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../cluster.rc
. $(dirname $0)/../volume.rc

function peer_count {
eval \$CLI_$1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}

cleanup

TEST launch_cluster 3

TEST $CLI_1 system uuid reset

## basic peer commands
TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 1
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count 2

#probe a unreachable node
TEST kill_glusterd 3
TEST ! $CLI_1 peer probe $H3

#detach a node which is not a part of cluster
TEST ! $CLI_1 peer detach $H3
TEST ! $CLI_1 peer detach $H3 force

TEST start_glusterd 3
TEST $CLI_1 peer probe $H3
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 1
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 2
EXPECT_WITHIN $PROBE_TIMEOUT 2 peer_count 3

# probe a node which is already part of cluster
TEST $CLI_1 peer probe $H3

#probe an invalid address
TEST ! $CLI_1 peer probe 1024.1024.1024.1024

TEST $CLI_1 pool list

TEST $CLI_1 --help
TEST $CLI_1 --version
TEST $CLI_1 --print-logdir
TEST $CLI_1 --print-statedumpdir

# try unrecognised command
TEST ! $CLI_1 volume
TEST pidof glusterd

## all help commands
TEST $CLI_1 global help
TEST $CLI_1 help

TEST $CLI_1 peer help
TEST $CLI_1 volume help
TEST $CLI_1 volume bitrot help
TEST $CLI_1 volume quota help
TEST $CLI_1 snapshot help

## volume operations
TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0
# create a volume with already existing volume name
TEST ! $CLI_1 volume create $V0 $H1:$B1/$V1 $H2:$B2/$V1
TEST $CLI_1 volume start $V0
EXPECT 'Started' cluster_volinfo_field 1 $V0 'Status';

# Mount the volume and create files
TEST glusterfs -s $H1 --volfile-id $V0 $M1
TEST touch $M1/file{1..100}

#fails because $V0 is not shd compatible
TEST ! $CLI_1 volume status $V0 shd

#test explicitly provided options
TEST $CLI_1 --timeout=120 --log-level=INFO volume status

# system commnds
TEST $CLI_1 system help
TEST $CLI_1 system uuid get
TEST $CLI_1 system getspec $V0
TEST $CLI_1 system getwd
TEST $CLI_1 system fsm log

cleanup
