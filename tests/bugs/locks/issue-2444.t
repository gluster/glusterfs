#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# An unlink that the brick delays because another client holds an inodelk on
# the file must be resumed when that lock goes away without an explicit
# unlock: because the connection of its holder ended (case 1), or because the
# lock was cleared (case 2).

function is_running() {
    if kill -0 "${1}" 2> /dev/null; then
        echo "Y"
    else
        echo "N"
    fi
}

# Writes to a file and keeps it open. It must not close any descriptor of the
# file: every close sends a flush, which makes replicate release its lock.
HOLD='import os, sys, time
fd = os.open(sys.argv[1], os.O_WRONLY)
os.write(fd, b"data")
time.sleep(300)'

function children_up() {
    local n=0
    local i

    for i in 0 1 2; do
        if [[ "$(afr_child_up_status_meta ${1} $V0-replicate-0 ${i})" == "1" ]]; then
            n=$((n + 1))
        fi
    done
    echo "${n}"
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/brick{0..2}
# A write keeps its transaction lock until the delayed post-op. With this
# delay the lock outlives the test unless something else takes it away.
TEST $CLI volume set $V0 cluster.post-op-delay-secs 300
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/brick2

TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M1
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" children_up $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" children_up $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" children_up $M2

TEST touch $M0/file1 $M0/file2

# Case 1: the connection of the lock holder ends.
$PYTHON -c "$HOLD" $M0/file1 &
hold_pid=$!
sleep 2
rm -f $M1/file1 &
rm_pid=$!
sleep 2
# The unlink waits for the lock of the first client.
EXPECT "Y" is_running $rm_pid
TEST kill -9 $(get_mount_process_pid $V0 $M0)
EXPECT_WITHIN 10 "N" is_running $rm_pid
TEST ! stat $B0/brick0/file1
kill $hold_pid
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Case 2: the lock is cleared.
$PYTHON -c "$HOLD" $M2/file2 &
hold_pid=$!
sleep 2
rm -f $M1/file2 &
rm_pid=$!
sleep 2
EXPECT "Y" is_running $rm_pid
TEST $CLI volume clear-locks $V0 /file2 kind granted inode
EXPECT_WITHIN 10 "N" is_running $rm_pid
TEST ! stat $B0/brick0/file2
kill $hold_pid

cleanup
