#!/bin/bash
#
# A mknod whose name hashes to a decommissioned brick goes through
# dht_mknod_lock: DHT takes an F_RDLCK on the parent directory in
# DHT_LAYOUT_HEAL_DOMAIN, refreshes the layout and re-issues the mknod.
# The shared callback dht_newfile_cbk only reached the unlock when the
# mknod succeeded; on failure it unwound with the lock still granted on
# the brick and freed the client-side lock objects, so nothing ever
# released it.  Every later F_WRLCK taker of that domain on the same
# directory (layout self-heal, fix-layout, commit-hash update) then
# blocked until the client disconnected.
#
# The mknods are made to fail on the brick with EEXIST: the names are
# created directly on brick 0's backend, so the client's lookup under its
# stale layout (which goes to brick 1) stays negative, the mknod reaches
# DHT, takes the lock path, is re-issued to brick 0 per the refreshed
# layout and fails there.  Neither error-gen nor permissions serve here:
# stock error-gen picks a random errno and an ENOENT becomes ESTALE in
# fuse, after which the kernel re-looks-up the directory and the client
# refreshes its cached layout; a permission failure is refused before
# DHT (kernel default_permissions, or posix-acl on the client with --acl).

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# dht_mknod_finish sends the unlock on a copied frame and unwinds the mknod
# without waiting for it, so the brick may still show the lock for a moment
# after mkfifo has returned.
LOCK_RELEASE_TIMEOUT=10

function active_layout_heal_locks {
        local vol=$1
        local host=$2
        local brick=$3
        local dump=$(generate_brick_statedump $vol $host $brick)
        awk '/^lock-dump.domain.domain=/ { dom = $0 }
             /^inodelk.inodelk\[[0-9]+\]\(ACTIVE\)/ && dom ~ /dht.layout.heal/ { n++ }
             END { print n + 0 }' $dump
}

function decommission_path_hits {
        local log=$1
        if [ ! -r "$log" ]; then
                echo 0
                return
        fi
        grep -c "part of decommission brick list" $log
}

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG
# The reproducer needs a negative lookup on the (stale-cached) hashed subvol to
# be terminal, so the mknod actually reaches DHT.  lookup-optimize is on by
# default; set it explicitly so the test does not depend on the default.
TEST $CLI volume set $V0 cluster.lookup-optimize on
TEST $CLI volume start $V0
# Long kernel timeouts: the client must keep using its cached layout of
# $M0/dir after the decommission rewrites the one on disk, which is what
# any client that has not re-looked-up the directory does.
TEST _GFS --attribute-timeout=3600 --entry-timeout=3600 --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST ls $M0/dir

# Decommission brick 1.  glusterd pushes a client volfile that carries
# cluster.decommissioned-bricks and the fix-layout crawl drops brick 1
# from the on-disk layout of $M0/dir.  The client's cached layout still
# hashes about half of the names to brick 1, so those mknods go through
# dht_mknod_lock + layout refresh.
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}1 start
EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" remove_brick_status_completed_field "$V0" "$H0:$B0/${V0}1"

# Make every mknod fail on the brick that receives it.
for i in $(seq 1 40); do
        touch $B0/${V0}0/dir/fifo$i
done
for i in $(seq 1 40); do
        mkfifo $M0/dir/fifo$i 2>/dev/null
done

# Positive control: some of the mknods did take the decommissioned-brick path.
# The mount log name is $M0 with '/' turned into '-'.
mnt_log="$LOGDIR/$(echo ${M0#/} | tr '/' '-').log"
EXPECT_NOT "0" decommission_path_hits "$mnt_log"

# Nothing may stay granted on the decommissioned brick: the failed mknods
# must have released their parent-layout locks.  (A leaked lock would block
# every fix-layout / layout self-heal of $M0/dir in D state until this
# client disconnects, so that is not asserted directly.)
EXPECT_WITHIN $LOCK_RELEASE_TIMEOUT "0" active_layout_heal_locks $V0 $H0 $B0/${V0}1

cleanup;
