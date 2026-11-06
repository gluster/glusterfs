#!/bin/bash

# DHT normalizes brick sizes into 1MB chunks in dht_du_info_cbk():
#
#   bpc = (1 << 20) / statvfs->f_bsize;
#   chunks = (f_blocks + bpc - 1) / bpc;
#
# A brick filesystem reporting f_bsize larger than 1MB made bpc
# truncate to zero and the chunks division kill the client with
# SIGFPE on the first statfs reply after mount. ZFS reports
# f_bsize == recordsize, and large_blocks allows recordsize up to
# 16MB, so any recordsize above 1m triggered the crash.
#
# This test puts a distribute volume on two recordsize=2m ZFS bricks
# and checks that a client survives the operations that trigger the
# DHT disk-usage refresh.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../snapshot_zfs.rc

if ! verify_zfs_version; then
    SKIP_TESTS
    exit 0;
fi

cleanup;

# a crashed previous run (unfixed code crashes the client here) can
# leave the loopback pools imported and defeat the rc cleanup;
# destroy leftovers best-effort before setting up
zpool destroy -f ${ZFS_PREFIX}_pool_1 2>/dev/null
zpool destroy -f ${ZFS_PREFIX}_pool_2 2>/dev/null

TEST init_n_bricks 2
TEST setup_zfs 2

# recordsize above 1M needs the large_blocks pool feature and a
# permitting zfs_max_recordsize (both defaults since OpenZFS 2.2);
# skip on a ZFS that refuses the value rather than fail
if ! zfs set recordsize=2M ${ZFS_PREFIX}_pool_1/bricks 2>/dev/null; then
    SKIP_TESTS
    exit 0;
fi
TEST zfs set recordsize=2M ${ZFS_PREFIX}_pool_2/bricks

# guard against a vacuous pass: the brick filesystems must actually
# report a block size above DHT's 1MB chunk unit
EXPECT "2097152" stat -f -c %s /${ZFS_PREFIX}_pool_1/bricks
EXPECT "2097152" stat -f -c %s /${ZFS_PREFIX}_pool_2/bricks

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2

# the brick directories themselves must sit on the 2MB-block
# filesystem, not just the dataset mountpoint checked above
EXPECT "2097152" stat -f -c %s $L1
EXPECT "2097152" stat -f -c %s $L2

TEST $CLI volume start $V0

# truncate any log left by a previous run: the fingerprint count
# below must only see lines from this mount
MOUNT_LOG=$LOGDIR/zfs-recordsize-sigfpe-$V0.log
rm -f $MOUNT_LOG
TEST $GFS -s $H0 --volfile-id=/$V0 --log-level=DEBUG \
    --log-file=$MOUNT_LOG $M0

# mkdir triggers dht_get_du_info(): a statfs is wound to every
# subvolume and dht_du_info_cbk() runs the chunk arithmetic on each
# reply
TEST mkdir $M0/dir1
TEST touch $M0/dir1/file1

# dht_du_info_cbk() logs "avail_percent" right after the chunk
# computation - on broken code SIGFPE fires in the same function
# before the line can log, so this also proves the repaired path ran
# for both large-bsize subvolumes
function du_cbk_completed() {
    local count
    count=$(grep -c "avail_percent" $MOUNT_LOG 2>/dev/null)
    if [ "${count:-0}" -ge 2 ]; then
        echo "Y"
    else
        echo "N"
    fi
}
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" du_cbk_completed

# the client survived the statfs replies: the mount still answers
TEST stat $M0/dir1/file1
TEST ls $M0/dir1

# both bricks received a layout range from the chunk-weighted
# calculation
TEST getfattr -n trusted.glusterfs.dht $L1/dir1
TEST getfattr -n trusted.glusterfs.dht $L2/dir1

TEST umount $M0

cleanup;
