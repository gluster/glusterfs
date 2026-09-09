#!/bin/bash
#
# dht_check_and_open_fd_on_subvol_task() re-opens a migrated file's fd on the
# new cached subvolume when a fd-based fop returns EBADF/EBADFD.  When that
# re-open itself fails, the application must see the real errno of the failed
# open (or a legitimate one) -- not EPERM, and not op_errno 0.
#
# Case 1: destination brick down -> the re-open fails with ENOTCONN.
#         fstat/fallocate/close on the held fd must return ENOTCONN.
#         (Unpatched: EPERM.)
# Case 2: destination gfid handle gone -> the re-open fails with ESTALE.
#         fstat on the held fd must return EBADFD and close() must fail.
#         (Unpatched: EIO, and close() silently succeeds.)
#
# readv/writev/fsync take protocol/client's anonymous-fd fallback and never
# reach the re-open, so the probes are fstat, fallocate and close (flush).

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

HOLDER=$(dirname $0)/fd-reopen-error-path.py

# Create files PREFIX0.. and rename each to a name hashing elsewhere until one
# ends up with its data on brick 0 and a linkto on brick 1; print that name.
function pick_split_file {
    local prefix=$1 i
    for i in $(seq 0 63); do
        echo "data-$i" > $M0/${prefix}a$i
        mv $M0/${prefix}a$i $M0/${prefix}$i
        if [ -s $B0/${V0}0/${prefix}$i ] && [ -e $B0/${V0}1/${prefix}$i ] && \
           getfattr -n trusted.glusterfs.dht.linkto -h --only-values \
                    $B0/${V0}1/${prefix}$i > /dev/null 2>&1; then
            echo ${prefix}$i
            return 0
        fi
    done
    return 1
}

# Create a file whose data lands on brick 1 (used to probe connectivity).
function pick_file_on_brick1 {
    local i
    for i in $(seq 0 63); do
        echo "probe-$i" > $M0/z$i
        if [ -s $B0/${V0}1/z$i ]; then
            echo z$i
            return 0
        fi
    done
    return 1
}

# All probe helpers exit 0: EXPECT_WITHIN stops retrying on a non-zero status.
function holder_ready {
    head -n 1 $1 2> /dev/null
    return 0
}

function holder_result {
    grep "^$2=" $1 2> /dev/null | cut -d= -f2
    return 0
}

function can_read {
    cat $1 > /dev/null 2>&1 && echo "OK"
    return 0
}

# Path of a brick file's .glusterfs gfid handle.  (getfattr --only-values
# prints the raw bytes even with -e hex, so parse the "trusted.gfid=0x.." line.)
function gfid_handle {
    local gfid=$(getfattr -n trusted.gfid -e hex $1 2> /dev/null | sed -n 's/^trusted.gfid=0x//p')
    [ ${#gfid} -eq 32 ] || return 0
    echo "$(dirname $1)/.glusterfs/${gfid:0:2}/${gfid:2:2}/${gfid:0:8}-${gfid:8:4}-${gfid:12:4}-${gfid:16:4}-${gfid:20:12}"
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
TEST $CLI volume set $V0 network.ping-timeout 3
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0

probe=$(pick_file_on_brick1)
TEST [ x$probe != x ]

##### Case 1: destination brick down -> re-open fails with ENOTCONN #####

f1=$(pick_split_file p)
TEST [ x$f1 != x ]
out1=$B0/holder1.out; go1=$B0/go1
rm -f $out1 $go1
$PYTHON $HOLDER $M0/$f1 $go1 $out1 fstat,fallocate,close &
EXPECT_WITHIN 10 "READY" holder_ready $out1

# Migrate the data brick 0 -> brick 1 while the fd is open on brick 0.
TEST setfattr -n trusted.distribute.migrate-data -v force $M0/$f1
TEST [ -s $B0/${V0}1/$f1 ]
TEST ! [ -e $B0/${V0}0/$f1 ]

TEST kill_brick $V0 $H0 $B0/${V0}1
TEST touch $go1
EXPECT_WITHIN 30 "ENOTCONN" holder_result $out1 fstat
EXPECT_WITHIN 30 "ENOTCONN" holder_result $out1 fallocate
EXPECT_WITHIN 30 "ENOTCONN" holder_result $out1 close

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "OK" can_read $M0/$probe

##### Case 2: destination gfid handle gone -> re-open fails with ESTALE #####

f2=$(pick_split_file q)
TEST [ x$f2 != x ]
out2=$B0/holder2.out; go2=$B0/go2
rm -f $out2 $go2
$PYTHON $HOLDER $M0/$f2 $go2 $out2 fstat,close &
EXPECT_WITHIN 10 "READY" holder_ready $out2

TEST setfattr -n trusted.distribute.migrate-data -v force $M0/$f2
TEST [ -s $B0/${V0}1/$f2 ]
handle=$(gfid_handle $B0/${V0}1/$f2)
TEST [ x$handle != x ]
TEST [ -e $handle ]

# Remove the destination's gfid handle behind a stopped brick, then restart
# it so the by-gfid open done by the re-open cannot be served from cache.
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST rm -f "$handle"
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "OK" can_read $M0/$probe

TEST touch $go2
EXPECT_WITHIN 30 "EBADFD" holder_result $out2 fstat
EXPECT_WITHIN 30 "EBADFD" holder_result $out2 close

cleanup;
