#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc
. $(dirname $0)/../cluster.rc

function config_validate ()
{
        local var=$1
        $CLI_1 snapshot config | grep "^$var" | sed 's/.*: //'
}

function snap_create ()
{
        local limit=$1;
        local i=0
        while [ $i -lt $limit ]
        do
                $CLI_1 snapshot create snap$i ${V0}
                i=$[$i+1]
        done
}

function snap_delete ()
{
        local limit=$1;
        local i=0
        while [ $i -lt $limit ]
        do
                $CLI_1 snapshot delete snap$i
                i=$[$i+1]
        done
}

function get_snap_count ()
{
        $CLI_1 snapshot list | wc -l
}

function get_volume_info ()
{
        local var=$1
        $CLI_1 volume info $V0 | grep "^$var" | sed 's/.*: //'
}

function is_snapshot_present ()
{
        $CLI_1 snapshot list
}

cleanup;

TEST verify_lvm_version
TEST launch_cluster 2
TEST setup_lvm 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

TEST $CLI_1 volume create $V0 $H1:$L1 $H2:$L2
EXPECT "$V0" get_volume_info 'Volume Name';
EXPECT 'Created' get_volume_info 'Status';

TEST $CLI_1 volume start $V0
EXPECT 'Started' get_volume_info 'Status';


# Setting system limit
TEST $CLI_1 snapshot config snap-max-hard-limit 100

# Volume limit cannot exceed system limit, as limit is set to 100,
# this should fail.
TEST ! $CLI_1 snapshot config $V0 snap-max-hard-limit 101

# Following are the invalid cases
TEST ! $CLI_1 snapshot config $V0 snap-max-hard-limit a10
TEST ! $CLI_1 snapshot config snap-max-hard-limit 10a
TEST ! $CLI_1 snapshot config snap-max-hard-limit 10%
TEST ! $CLI_1 snapshot config snap-max-soft-limit 50%1
TEST ! $CLI_1 snapshot config snap-max-soft-limit 0111
TEST ! $CLI_1 snapshot config snap-max-hard-limit OXA
TEST ! $CLI_1 snapshot config snap-max-hard-limit 11.11
TEST ! $CLI_1 snapshot config snap-max-soft-limit 50%
TEST ! $CLI_1 snapshot config snap-max-hard-limit -100
TEST ! $CLI_1 snapshot config snap-max-soft-limit -90

# Soft limit cannot be assigned to volume
TEST ! $CLI_1 snapshot config $V0 snap-max-soft-limit 10

# Valid case
TEST $CLI_1 snapshot config snap-max-soft-limit 50
TEST $CLI_1 snapshot config $V0 snap-max-hard-limit 10

# Validating auto-delete feature
# Make sure auto-delete is disabled by default
EXPECT 'disable' config_validate 'auto-delete'

# Test for invalid value for auto-delete
TEST ! $CLI_1 snapshot config auto-delete test

TEST $CLI_1 snapshot config snap-max-hard-limit 6
TEST $CLI_1 snapshot config snap-max-soft-limit 50

# Create 4 snapshots
TEST snap_create 4;

# If auto-delete is disabled then oldest snapshot
# should not be deleted automatically.
EXPECT '4' get_snap_count;

TEST snap_delete 4;

# After all those 4 snaps are deleted, There will not be any snaps present
EXPECT 'No snapshots present' is_snapshot_present;

TEST $CLI_1 snapshot config auto-delete enable
# auto-delete is already enabled, Hence expect a failure.
TEST ! $CLI_1 snapshot config auto-delete on

# Testing other boolean values with auto-delete
TEST $CLI_1 snapshot config auto-delete off
EXPECT 'off' config_validate 'auto-delete'

TEST $CLI_1 snapshot config auto-delete true
EXPECT 'true' config_validate 'auto-delete'

# Try to create 4 snaps again, As auto-delete is enabled
# oldest snap should be deleted and snapcount should be 3

TEST snap_create 4;
EXPECT '3' get_snap_count;

TEST $CLI_1 snapshot config auto-delete disable
EXPECT 'disable' config_validate 'auto-delete'

cleanup;

