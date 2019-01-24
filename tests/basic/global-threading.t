#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

# Test if the given process has a number of threads of a given type between
# min and max.
function check_threads() {
    local pid="${1}"
    local pattern="${2}"
    local min="${3}"
    local max="${4-}"
    local count

    count="$(ps hH -o comm ${pid} | grep "${pattern}" | wc -l)"
    if [[ ${min} -gt ${count} ]]; then
        return 1
    fi
    if [[ ! -z "${max}" && ${max} -lt ${count} ]]; then
        return 1
    fi

    return 0
}

cleanup

TEST glusterd

# Glusterd shouldn't use any thread
TEST check_threads $(get_glusterd_pid) glfs_tpw 0 0
TEST check_threads $(get_glusterd_pid) glfs_iotwr 0 0

TEST pkill -9 glusterd

TEST glusterd --global-threading

# Glusterd shouldn't use global threads, even if enabled
TEST check_threads $(get_glusterd_pid) glfs_tpw 0 0
TEST check_threads $(get_glusterd_pid) glfs_iotwr 0 0

TEST $CLI volume create $V0 replica 2 $H0:$B0/b{0,1}

# Normal configuration using io-threads on bricks
TEST $CLI volume set $V0 config.global-threading off
TEST $CLI volume set $V0 performance.iot-pass-through off
TEST $CLI volume set $V0 performance.client-io-threads off
TEST $CLI volume start $V0

# There shouldn't be global threads
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b0) glfs_tpw 0 0
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b1) glfs_tpw 0 0

# There should be at least 1 io-thread
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b0) glfs_iotwr 1
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b1) glfs_iotwr 1

# Self-heal should be using global threads
TEST check_threads $(get_shd_process_pid) glfs_tpw 1
TEST check_threads $(get_shd_process_pid) glfs_iotwr 0 0

TEST $CLI volume stop $V0

# Configuration with global threads on bricks
TEST $CLI volume set $V0 config.global-threading on
TEST $CLI volume set $V0 performance.iot-pass-through on
TEST $CLI volume start $V0

# There should be at least 1 global thread
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b0) glfs_tpw 1
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b1) glfs_tpw 1

# There shouldn't be any io-thread worker threads
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b0) glfs_iotwr 0 0
TEST check_threads $(get_brick_pid $V0 $H0 $B0/b1) glfs_iotwr 0 0

# Normal configuration using io-threads on clients
TEST $CLI volume set $V0 performance.iot-pass-through off
TEST $CLI volume set $V0 performance.client-io-threads on
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

# There shouldn't be global threads
TEST check_threads $(get_mount_process_pid $V0 $M0) glfs_tpw 0 0

# There should be at least 1 io-thread
TEST check_threads $(get_mount_process_pid $V0 $M0) glfs_iotwr 1

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Configuration with global threads on clients
TEST $CLI volume set $V0 performance.client-io-threads off
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 --global-threading $M0

# There should be at least 1 global thread
TEST check_threads $(get_mount_process_pid $V0 $M0) glfs_tpw 1

# There shouldn't be io-threads
TEST check_threads $(get_mount_process_pid $V0 $M0) glfs_iotwr 0 0

# Some basic volume access checks with global-threading enabled everywhere
TEST mkdir ${M0}/dir
TEST dd if=/dev/zero of=${M0}/dir/file bs=128k count=8

cleanup
