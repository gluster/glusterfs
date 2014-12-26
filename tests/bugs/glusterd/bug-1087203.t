#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../cluster.rc

function get_volume_info ()
{
        local var=$1
        $CLI_1 volume info $V0 | grep "^$var" | sed 's/.*: //'
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
EXPECT 'disable' snap_config CLI_1 'auto-delete'

# Test for invalid value for auto-delete
TEST ! $CLI_1 snapshot config auto-delete test

TEST $CLI_1 snapshot config snap-max-hard-limit 6
TEST $CLI_1 snapshot config snap-max-soft-limit 50

# Create 4 snapshots
snap_index=1
snap_count=4
TEST snap_create CLI_1 $V0 $snap_index $snap_count

# If auto-delete is disabled then oldest snapshot
# should not be deleted automatically.
EXPECT '4' get_snap_count CLI_1;

TEST snap_delete CLI_1 $snap_index $snap_count;

# After all those 4 snaps are deleted, There will not be any snaps present
EXPECT '0' get_snap_count CLI_1;

TEST $CLI_1 snapshot config auto-delete enable

# auto-delete is already enabled, Hence expect a failure.
TEST ! $CLI_1 snapshot config auto-delete on

# Testing other boolean values with auto-delete
TEST $CLI_1 snapshot config auto-delete off
EXPECT 'off' snap_config CLI_1 'auto-delete'

TEST $CLI_1 snapshot config auto-delete true
EXPECT 'true' snap_config CLI_1 'auto-delete'

# Try to create 4 snaps again, As auto-delete is enabled
# oldest snap should be deleted and snapcount should be 3

TEST snap_create CLI_1 $V0 $snap_index $snap_count;
EXPECT '3' get_snap_count CLI_1;

TEST $CLI_1 snapshot config auto-delete disable
EXPECT 'disable' snap_config CLI_1 'auto-delete'

cleanup;

