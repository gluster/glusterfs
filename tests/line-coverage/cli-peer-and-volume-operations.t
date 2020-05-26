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

#changing timezone to a different one, to check localtime logging feature
TEST export TZ='Asia/Kolkata'
TEST restart_glusterd 1

#localtime logging enable
TEST $CLI_1 volume set all cluster.localtime-logging enable
EXPECT '1' logging_time_check $LOGDIR

#localtime logging disable
TEST $CLI_1 volume set all cluster.localtime-logging disable
EXPECT '0' logging_time_check $LOGDIR

#changing timezone back to original timezone
TEST export TZ='UTC'

#negative tests for volume options
#'set' option to enable quota/inode-quota is now depreciated
TEST ! $CLI_1 volume set $V0 quota enable
TEST ! $CLI_1 volume set $V0 inode-quota enable

#invalid transport type 'rcp'
TEST ! $CLI_1 volume set $V0 config.transport rcp

#'op-version' option is not valid for a single volume
TEST ! $CLI_1 volume set $V0 cluster.op-version 72000

#'op-version' option can't be used with any other option
TEST ! $CLI_1 volume set all cluster.localtime-logging disable cluster.op-version 72000

#invalid format of 'op-version'
TEST ! $CLI_1 volume set all cluster.op-version 72-000

#provided 'op-version' value is greater than max allowed op-version
op_version=$($CLI_1 volume get all cluster.max-op-version | awk 'NR==3 {print$2}')
op_version=$((op_version+1000))  #this can be any number greater than 0
TEST ! $CLI_1 volume set all cluster.op-version $op_version

#provided 'op-verison' value cannot be less than the current cluster op-version value
TEST ! $CLI_1 volume set all cluster.op-version 00000

# system commnds
TEST $CLI_1 system help
TEST $CLI_1 system uuid get
TEST $CLI_1 system getspec $V0
TEST $CLI_1 system getwd
TEST $CLI_1 system fsm log

# Both these may fail, but it covers xdr functions and some
# more code in cli/glusterd
$CLI_1 system:: mount test local:/$V0
$CLI_1 system:: umount $M0 lazy
$CLI_1 system:: copy file options
$CLI_1 system:: portmap brick2port $H0:$B0/brick
$CLI_1 system:: uuid reset

cleanup
