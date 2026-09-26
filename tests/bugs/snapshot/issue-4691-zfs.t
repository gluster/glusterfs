#!/bin/bash

# gluster/glusterfs#4691: soft-limit auto-delete must not select a snapshot
# that still has a dependent clone.
#
# With "snapshot config auto-delete enable", a create that pushes a volume
# past its soft limit makes glusterd delete the oldest snapshot. It used to
# do so unconditionally: for a snapshot with a dependent clone (made by
# "snapshot clone"/"restore", which clone the backend without promoting it)
# the backend removal fails -- ZFS refuses "zfs destroy" of a snapshot that
# still has a clone -- but the failure was swallowed, so glusterd dropped the
# snapshot object and left the backend snapshot behind, untracked.
#
# Now the oldest *removable* over-limit snapshot is deleted; when none is
# removable nothing is reclaimed and snap_count rises to the hard limit,
# where creation is refused. The check reads replicated glusterd metadata
# (restored_from_snap), so every peer selects the same snapshot: the only
# brick lives on node 1 and node 2 must still see the same list.
#
# ZFS twin of issue-4691.t. It additionally checks that the number of
# backend snapshots keeps matching glusterd's, i.e. that nothing is orphaned.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../snapshot_zfs.rc

if ! verify_zfs_version; then
    SKIP_TESTS
    exit 0;
fi

# Number of ZFS snapshots of the brick dataset $1 itself. The clone datasets
# are children of it ($1/<clone-volume-id>_<brick>) and are left out by the
# anchor.
function zfs_snapshot_count() {
        zfs list -H -t snapshot -o name -r "$1" | grep -c -- "^$1@"
}

cleanup;

TEST verify_zfs_version
TEST launch_cluster 2
TEST setup_zfs 1

TEST $CLI_1 peer probe $H2
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

TEST $CLI_1 volume create $V0 $H1:$L1
TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field_1 $V0 'Status'

# the dataset behind $L1 (see init_zfs in snapshot_zfs.rc)
DS1="${ZFS_PREFIX}_pool_1/bricks"
EXPECT "0" zfs_snapshot_count $DS1

# hard limit 4 with a 50% soft limit: auto-delete runs past 2 snapshots
TEST $CLI_1 snapshot config $V0 snap-max-hard-limit 4
TEST $CLI_1 snapshot config snap-max-soft-limit 50
TEST $CLI_1 snapshot config auto-delete enable

# The auto-delete order is the snapshots' creation time in whole seconds
# (glusterd_compare_snap_vol_time over time_t), and the ordered insert puts a
# tied newcomer BEFORE the existing entry, so two snapshots created within the
# same second sort newest-first and "the oldest over-limit snapshot" is not
# the one this test means. Keep every create in its own second.
TEST $CLI_1 snapshot create snap1 $V0 no-timestamp
sleep 1
TEST $CLI_1 snapshot create snap2 $V0 no-timestamp

# a clone needs an activated snapshot; only the snapshots that get cloned
# are activated, so the ones auto-delete removes are never mounted
TEST $CLI_1 snapshot activate snap1
TEST $CLI_1 snapshot clone ${V0}_clone1 snap1
EXPECT 'Created' volinfo_field_1 ${V0}_clone1 'Status'

# 3 snapshots, soft limit 2: the only over-limit snapshot is snap1, which
# is clone-held, so nothing is reclaimed (the old code deleted snap1 here
# and orphaned its backend snapshot)
sleep 1
TEST $CLI_1 snapshot create snap3 $V0 no-timestamp
TEST snapshot_exists 1 snap1
EXPECT "3" get_snap_count CLI_1
EXPECT_WITHIN $PROBE_TIMEOUT "3" get_snap_count CLI_2
EXPECT "3" zfs_snapshot_count $DS1

# 4 snapshots: two over the limit; snap1 is skipped and snap2, the next
# oldest, is removed
sleep 1
TEST $CLI_1 snapshot create snap4 $V0 no-timestamp
TEST snapshot_exists 1 snap1
TEST ! snapshot_exists 1 snap2
TEST snapshot_exists 1 snap3
TEST snapshot_exists 1 snap4
EXPECT "3" get_snap_count CLI_1
EXPECT_WITHIN $PROBE_TIMEOUT "3" get_snap_count CLI_2
TEST snapshot_exists 2 snap1
TEST ! snapshot_exists 2 snap2
EXPECT "3" zfs_snapshot_count $DS1

# hold snap3 as well: every over-limit snapshot is now clone-held, nothing
# is reclaimed and snap_count reaches the hard limit ...
TEST $CLI_1 snapshot activate snap3
TEST $CLI_1 snapshot clone ${V0}_clone3 snap3
EXPECT 'Created' volinfo_field_1 ${V0}_clone3 'Status'
sleep 1
TEST $CLI_1 snapshot create snap5 $V0 no-timestamp
TEST snapshot_exists 1 snap1
TEST snapshot_exists 1 snap3
EXPECT "4" get_snap_count CLI_1
EXPECT_WITHIN $PROBE_TIMEOUT "4" get_snap_count CLI_2
EXPECT "4" zfs_snapshot_count $DS1

# ... where creation is refused rather than a snapshot orphaned
sleep 1
TEST ! $CLI_1 snapshot create snap6 $V0 no-timestamp
EXPECT "4" get_snap_count CLI_1
EXPECT "4" zfs_snapshot_count $DS1

cleanup;
