#!/bin/bash

WD="$(dirname "${0}")"

. ${WD}/../../include.rc
. ${WD}/../../volume.rc

function assign() {
    local _assign_var="${1}"
    local _assign_value="${2}"

    printf -v "${_assign_var}" "%s" "${_assign_value}"
}

function pipe_create() {
    local _pipe_create_var="${1}"
    local _pipe_create_name
    local _pipe_create_fd

    _pipe_create_name="$(mktemp -u)"
    mkfifo "${_pipe_create_name}"
    exec {_pipe_create_fd}<>"${_pipe_create_name}"
    rm "${_pipe_create_name}"

    assign "${_pipe_create_var}" "${_pipe_create_fd}"
}

function pipe_close() {
    local _pipe_close_fd="${!1}"

    exec {_pipe_close_fd}>&-
}

function tester_start() {
    declare -ag tester
    local tester_in
    local tester_out

    pipe_create tester_in
    pipe_create tester_out

    ${WD}/tester <&${tester_in} >&${tester_out} &

    tester=("$!" "${tester_in}" "${tester_out}")
}

function tester_send() {
    declare -ag tester
    local tester_res
    local tester_extra

    echo "${*}" >&${tester[1]}

    read -t 3 -u ${tester[2]} tester_res tester_extra
    echo "${tester_res} ${tester_extra}"
    if [[ "${tester_res}" == "OK" ]]; then
        return 0
    fi

    return 1
}

function tester_stop() {
    declare -ag tester
    local tester_res

    tester_send "quit"

    tester_res=0
    if ! wait ${tester[0]}; then
        tester_res=$?
    fi

    unset tester

    return ${tester_res}
}

function count_open() {
    local file="$(realpath "${B0}/${V0}/${1}")"
    local count="0"
    local inode
    local ref

    inode="$(stat -c %i "${file}")"

    for fd in /proc/${BRICK_PID}/fd/*; do
        ref="$(readlink "${fd}")"
        if [[ "${ref}" == "${B0}/${V0}/"* ]]; then
            if [[ "$(stat -c %i "${ref}")" == "${inode}" ]]; then
                count="$((${count} + 1))"
            fi
        fi
    done

    echo "${count}"
}

cleanup

TEST build_tester ${WD}/tester.c ${WD}/tester-fd.c

TEST glusterd
TEST pidof glusterd
TEST ${CLI} volume create ${V0} ${H0}:${B0}/${V0}
TEST ${CLI} volume set ${V0} flush-behind off
TEST ${CLI} volume set ${V0} write-behind off
TEST ${CLI} volume set ${V0} quick-read off
TEST ${CLI} volume set ${V0} stat-prefetch on
TEST ${CLI} volume set ${V0} io-cache off
TEST ${CLI} volume set ${V0} open-behind on
TEST ${CLI} volume set ${V0} lazy-open off
TEST ${CLI} volume set ${V0} read-after-open off
TEST ${CLI} volume start ${V0}

TEST ${GFS} --volfile-id=/${V0} --volfile-server=${H0} ${M0};

BRICK_PID="$(get_brick_pid ${V0} ${H0} ${B0}/${V0})"

TEST touch "${M0}/test"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ${GFS} --volfile-id=/${V0} --volfile-server=${H0} ${M0};

TEST tester_start

TEST tester_send fd open 0 "${M0}/test"
EXPECT_WITHIN 5 "1" count_open "/test"
TEST tester_send fd close 0
EXPECT_WITHIN 5 "0" count_open "/test"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ${CLI} volume set ${V0} lazy-open on
TEST ${GFS} --volfile-id=/${V0} --volfile-server=${H0} ${M0};

TEST tester_send fd open 0 "${M0}/test"
sleep 2
EXPECT "0" count_open "/test"
TEST tester_send fd write 0 "test"
EXPECT "1" count_open "/test"
TEST tester_send fd close 0
EXPECT_WITHIN 5 "0" count_open "/test"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ${GFS} --volfile-id=/${V0} --volfile-server=${H0} ${M0};

TEST tester_send fd open 0 "${M0}/test"
EXPECT "0" count_open "/test"
EXPECT "test" tester_send fd read 0 64
# Even though read-after-open is disabled, use-anonymous-fd is also disabled,
# so reads need to open the file first.
EXPECT "1" count_open "/test"
TEST tester_send fd close 0
EXPECT "0" count_open "/test"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ${GFS} --volfile-id=/${V0} --volfile-server=${H0} ${M0};

TEST tester_send fd open 0 "${M0}/test"
EXPECT "0" count_open "/test"
TEST tester_send fd open 1 "${M0}/test"
EXPECT "2" count_open "/test"
TEST tester_send fd close 0
EXPECT_WITHIN 5 "1" count_open "/test"
TEST tester_send fd close 1
EXPECT_WITHIN 5 "0" count_open "/test"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST ${CLI} volume set ${V0} read-after-open on
TEST ${GFS} --volfile-id=/${V0} --volfile-server=${H0} ${M0};

TEST tester_send fd open 0 "${M0}/test"
EXPECT "0" count_open "/test"
EXPECT "test" tester_send fd read 0 64
EXPECT "1" count_open "/test"
TEST tester_send fd close 0
EXPECT_WITHIN 5 "0" count_open "/test"

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST tester_stop

cleanup
