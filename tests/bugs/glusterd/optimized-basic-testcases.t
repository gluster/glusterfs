#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

function get_opret_value () {
  local VOL=$1
  $CLI volume info $VOL --xml | sed -ne 's/.*<opRet>\([-0-9]*\)<\/opRet>/\1/p'
}

function check_brick()
{
        vol=$1;
        num=$2
        $CLI volume info $V0 | grep "Brick$num" | awk '{print $2}';
}

function brick_count()
{
        local vol=$1;

        $CLI volume info $vol | egrep "^Brick[0-9]+: " | wc -l;
}

function get_brick_host_uuid()
{
    local vol=$1;
    local uuid_regex='[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}'
    local host_uuid_list=$($CLI volume info $vol --xml | grep "brick.uuid" | grep -o -E "$uuid_regex");

    echo $host_uuid_list | awk '{print $1}'
}


cleanup;

TEST glusterd;
TEST pidof glusterd;

#bug-1238135-lazy-daemon-initialization-on-demand

GDWD=$($CLI system getwd)

# glusterd.info file will be created on either first peer probe or volume
# creation, hence we expect file to be not present in this case
TEST ! -e $GDWD/glusterd.info

#bug-913487 - setting volume options before creation of volume should fail

TEST ! $CLI volume set $V0 performance.open-behind off;
TEST pidof glusterd;

#bug-1433578 - glusterd should not crash after probing a invalid peer

TEST ! $CLI peer probe invalid-peer
TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
EXPECT 'Created' volinfo_field $V0 'Status';

#bug-955588 - uuid validation

uuid=`grep UUID $GLUSTERD_WORKDIR/glusterd.info | cut -f2 -d=`
EXPECT $uuid get_brick_host_uuid $V0
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

#bug-958790 - set options from file

touch $GLUSTERD_WORKDIR/groups/test
echo "read-ahead=off" > $GLUSTERD_WORKDIR/groups/test
echo "open-behind=off" >> $GLUSTERD_WORKDIR/groups/test

TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
TEST $CLI volume set $V0 group test
EXPECT "off" volume_option $V0 performance.read-ahead
EXPECT "off" volume_option $V0 performance.open-behind

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

#bug-1321836 - validate opret value for non existing volume

EXPECT 0 get_opret_value $V0
EXPECT -1 get_opret_value "novol"

EXPECT '2' brick_count $V0

#bug-862834 - validate brick status

EXPECT "$H0:$B0/${V0}1" check_brick $V0 '1';
EXPECT "$H0:$B0/${V0}2" check_brick $V0 '2';

TEST ! $CLI volume create $V1 $H0:$B0/${V1}0 $H0:$B0/${V0}1;

#bug-1482344 - setting volume-option-at-cluster-level should not result in glusterd crash

TEST ! $CLI volume set all transport.listen-backlog 128

# Check the volume info output, if glusterd would have crashed then this command
# will fail
TEST $CLI volume info $V0;

#bug-1002556 and bug-1199451 - command should retrieve current op-version of the node
TEST $CLI volume get all cluster.op-version

#bug-1315186 - reject-lowering-down-op-version

OP_VERS_ORIG=$(grep 'operating-version' ${GDWD}/glusterd.info | cut -d '=' -f 2)
OP_VERS_NEW=`expr $OP_VERS_ORIG-1`

TEST ! $CLI volume set all $V0 cluster.op-version $OP_VERS_NEW

#bug-1022055 - validate log rotate command

TEST $CLI volume log rotate $V0;

#bug-1092841 - validating barrier enable/disable

TEST $CLI volume barrier $V0 enable;
TEST ! $CLI volume barrier $V0 enable;

TEST $CLI volume barrier $V0 disable;
TEST ! $CLI volume barrier $V0 disable;

#bug-1095097 - validate volume profile command

TEST $CLI volume profile $V0 start
TEST $CLI volume profile $V0 info

#bug-839595 - validate server-quorum options

TEST $CLI volume set $V0 cluster.server-quorum-type server
EXPECT "server" volume_option $V0 cluster.server-quorum-type
TEST $CLI volume set $V0 cluster.server-quorum-type none
EXPECT "none" volume_option $V0 cluster.server-quorum-type
TEST $CLI volume reset $V0 cluster.server-quorum-type
TEST ! $CLI volume set $V0 cluster.server-quorum-type abc
TEST ! $CLI volume set all cluster.server-quorum-type none
TEST ! $CLI volume set $V0 cluster.server-quorum-ratio 100

TEST ! $CLI volume set all cluster.server-quorum-ratio abc
TEST ! $CLI volume set all cluster.server-quorum-ratio -1
TEST ! $CLI volume set all cluster.server-quorum-ratio 100.0000005
TEST $CLI volume set all cluster.server-quorum-ratio 0
EXPECT "0" volume_option $V0 cluster.server-quorum-ratio
TEST $CLI volume set all cluster.server-quorum-ratio 100
EXPECT "100" volume_option $V0 cluster.server-quorum-ratio
TEST $CLI volume set all cluster.server-quorum-ratio 0.0000005
EXPECT "0.0000005" volume_option $V0 cluster.server-quorum-ratio
TEST $CLI volume set all cluster.server-quorum-ratio 100%
EXPECT "100%" volume_option $V0 cluster.server-quorum-ratio

#bug-1265479 - validate-distributed-volume-options

#Setting data-self-heal option on for distribute volume
TEST ! $CLI volume set $V0 data-self-heal on
EXPECT '' volinfo_field $V0 'cluster.data-self-heal';
TEST ! $CLI volume set $V0 cluster.data-self-heal on
EXPECT '' volinfo_field $V0 'cluster.data-self-heal';

#Setting metadata-self-heal option on for distribute volume
TEST ! $CLI volume set $V0 metadata-self-heal on
EXPECT '' volinfo_field $V0 'cluster.metadata-self-heal';
TEST ! $CLI volume set $V0 cluster.metadata-self-heal on
EXPECT '' volinfo_field $V0 'cluster.metadata-self-heal';

#Setting entry-self-heal option on for distribute volume
TEST ! $CLI volume set $V0 entry-self-heal on
EXPECT '' volinfo_field $V0 'cluster.entrydata-self-heal';
TEST ! $CLI volume set $V0 cluster.entry-self-heal on
EXPECT '' volinfo_field $V0 'cluster.entrydata-self-heal';

#bug-1163108 - validate min-free-disk-option

## Setting invalid value for option cluster.min-free-disk should fail
TEST ! $CLI volume set $V0 min-free-disk ""
TEST ! $CLI volume set $V0 min-free-disk 143.!/12
TEST ! $CLI volume set $V0 min-free-disk 123%
TEST ! $CLI volume set $V0 min-free-disk 194.34%

## Setting fractional value as a size (unit is byte) for option
## cluster.min-free-disk should fail
TEST ! $CLI volume set $V0 min-free-disk 199.051
TEST ! $CLI volume set $V0 min-free-disk 111.999

## Setting valid value for option cluster.min-free-disk should pass
TEST  $CLI volume set $V0 min-free-disk 12%
TEST  $CLI volume set $V0 min-free-disk 56.7%
TEST  $CLI volume set $V0 min-free-disk 120
TEST  $CLI volume set $V0 min-free-disk 369.0000

#bug-1179175-uss-option-validation

## Set features.uss option with non-boolean value. These non-boolean value
## for features.uss option should fail.
TEST ! $CLI volume set $V0 features.uss abcd
TEST ! $CLI volume set $V0 features.uss #$#$
TEST ! $CLI volume set $V0 features.uss 2324

## Setting other options with valid value. These options should succeed.
TEST $CLI volume set $V0 barrier enable
TEST $CLI volume set $V0 ping-timeout 60

## Set features.uss option with valid boolean value. It should succeed.
TEST  $CLI volume set $V0 features.uss enable
TEST  $CLI volume set $V0 features.uss disable


## Setting other options with valid value. These options should succeed.
TEST $CLI volume set $V0 barrier enable
TEST $CLI volume set $V0 ping-timeout 60

#bug-1209329 - daemon-svcs-on-reset-volume

##enable the bitrot and verify bitd is running or not
TEST $CLI volume bitrot $V0 enable
EXPECT 'on' volinfo_field $V0 'features.bitrot'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count

##Do reset force which set the bitrot options to default
TEST $CLI volume reset $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_bitd_count

##enable the uss option and verify snapd is running or not
TEST $CLI volume set $V0 features.uss on
EXPECT 'on' volinfo_field $V0 'features.uss'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_snapd_count

##Do reset force which set the uss options to default
TEST $CLI volume reset $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_snapd_count

##verify initial nfs disabled by default
EXPECT "0" get_nfs_count

##enable nfs and verify
TEST $CLI volume set $V0 nfs.disable off
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available
EXPECT "1" get_nfs_count

##Do reset force which set the nfs.option to default
TEST $CLI volume reset $V0 force;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_nfs_count

##enable the uss option and verify snapd is running or not
TEST $CLI volume set $V0 features.uss on
EXPECT 'on' volinfo_field $V0 'features.uss'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_snapd_count

##Disable the uss option using set command and verify snapd
TEST $CLI volume set $V0 features.uss  off
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_snapd_count

##enable nfs.disable and verify
TEST $CLI volume set $V0 nfs.disable on
EXPECT 'on' volinfo_field $V0 'nfs.disable'
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "0" get_nfs_count

## disable nfs.disable option using set command
TEST $CLI volume set $V0 nfs.disable  off
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_nfs_count

TEST $CLI volume info;
TEST $CLI volume create $V1 $H0:$B0/${V1}1
TEST $CLI volume start $V1
pkill glusterd;
pkill glusterfsd;
TEST glusterd
TEST $CLI volume status $V1
TEST $CLI volume stop $V1
TEST $CLI volume delete $V1

#bug 1721109 - volfile should be created with transport type both
TEST $CLI volume create $V1 transport tcp,rdma $H0:$B0/${V1}2

cleanup
