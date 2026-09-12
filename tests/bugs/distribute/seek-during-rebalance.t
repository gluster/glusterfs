#!/bin/bash
#
# dht_seek_cbk leaks a spurious ENXIO during rebalance instead of redirecting
# SEEK_DATA/SEEK_HOLE to the destination subvolume.
#
# A file whose data is on brick 0 is migrated to brick 1 by an *external*
# remove-brick while an application holds an fd open on it.  dht_seek_cbk carries
# no iatt, so unlike readv/writev/attr it cannot detect the migration
# proactively (IS_DHT_MIGRATION_PHASE2); its only signal is the errno.  The held
# fd still resolves to brick 0, where the source is now a 0-byte leftover, so
# SEEK_DATA/SEEK_HOLE return ENXIO (offset >= EOF).  Unpatched dht_seek_cbk
# returns that ENXIO to the application; the fix lets ENXIO fall through to the
# migration redirect so the SEEK is re-dispatched to brick 1 where the data is.
#
# fstat()/read() on the same fd already redirect, so the bug is SEEK-specific;
# the test asserts the held fd sees the data (SEEK_DATA == 0) after the fix.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

HOLDER=$(dirname $0)/seek-during-rebalance.py

# Create files until one lands with its data on brick 0; print its name.
function pick_file_on_brick0 {
    local i
    for i in $(seq 0 63); do
        echo "seek-me-$i" > $M0/f$i
        if [ -s $B0/${V0}0/f$i ]; then
            echo f$i
            return 0
        fi
    done
    return 1
}

# Probe helpers all exit 0 so EXPECT_WITHIN keeps retrying (a non-zero status
# aborts the retry loop).
function holder_ready {
    head -n 1 $1 2> /dev/null
    return 0
}

function holder_result {
    grep "^$2=" $1 2> /dev/null | cut -d= -f2
    return 0
}

# Migration is complete once the source path is gone and the dst holds the data.
function src_migrated {
    if [ ! -e $B0/${V0}0/$1 ] && [ -s $B0/${V0}1/$1 ]; then
        echo "Y"
    fi
    return 0
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

f=$(pick_file_on_brick0)
TEST [ x$f != x ]
size=$(stat -c %s $M0/$f)

out=$B0/holder.out
go=$B0/go
rm -f $out $go

# Hold an fd open BEFORE the migration.  SEEK is probed first: fstat/read would
# redirect and re-resolve the inode, masking the SEEK bug, so they run after.
$PYTHON $HOLDER $M0/$f $go $out seek_data,seek_hole,fstat &
EXPECT_WITHIN 10 "READY" holder_ready $out

# External migration: remove brick 0, moving the data to brick 1.  The held fd
# is not touched, so it still resolves to brick 0.
TEST $CLI volume remove-brick $V0 $H0:$B0/${V0}0 start
EXPECT_WITHIN 30 "Y" src_migrated $f

TEST touch $go

# With the fix, the held-fd SEEK is redirected to brick 1: SEEK_DATA finds data
# at offset 0 and SEEK_HOLE returns the hole at EOF (== size).  Unpatched,
# dht_seek_cbk returns the source's spurious ENXIO for both.
EXPECT_WITHIN 15 "0" holder_result $out seek_data
EXPECT_WITHIN 15 "$size" holder_result $out seek_hole
# fstat redirects on both patched and unpatched -- a control that the file is
# really present on brick 1 the whole time.
EXPECT_WITHIN 15 "OK" holder_result $out fstat

cleanup;
