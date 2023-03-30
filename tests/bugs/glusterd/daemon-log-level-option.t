#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TESTS_EXPECTED_IN_LOOP=11

function count_messages() {
    local level="${1}"
    local file="${2}"

    grep "^\[[^]]\+\] ${level} \[" "${file}" | wc -l
}

function wait_started() {
    local file="${1}"
    local count

    count="$(count_messages "I" "${file}")"

    if [[ ${count} -ge 1 ]]; then
        return 0
    fi

    return 1
}

function prepare() {
    local level="${1}"
    local file="${2}"

    rm -f "${file}"
    TEST ${CLI} volume set all cluster.daemon-log-level "${level}"
    TEST ${CLI} volume start "${V0}"

# Using 'online_brick_count' test only checks that the process has started,
# but not necessarily fully initialized. To avoid spurious failures, we'll
# use the fact that io-stats always logs an "INFO" message before configuring
# the requested log level to determine when initialization has completed.
#
# WARNING: This condition could change in the future. Be aware of that in
#          case this test fails.

    TEST_WITHIN "${PROCESS_UP_TIMEOUT}" wait_started "${file}"
}

function reconfig() {
    local level="${1}"
    local file="${2}"

    TEST ${CLI} volume stop "${V0}"
    prepare "${level}" "${file}"
}

cleanup;

# Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a 3X2 distributed-replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..6};

logfile="${LOGDIR}/glustershd.log"

prepare "DEBUG" "${logfile}"

# There shouldn't be any TRACE messages
EXPECT "^0$" count_messages "T" "${logfile}"

reconfig "INFO" "${logfile}"

# There shouldn't be any TRACE or DEBUG messages
EXPECT "^0$" count_messages "T" "${logfile}"
EXPECT "^0$" count_messages "D" "${logfile}"

reconfig "WARNING" "${logfile}"

# There shouldn't be any TRACE or DEBUG messages, and only one INFO message
# from io-stats xlator.
EXPECT "^0$" count_messages "T" "${logfile}"
EXPECT "^0$" count_messages "D" "${logfile}"
EXPECT "^1$" count_messages "I" "${logfile}"

reconfig "ERROR" "${logfile}"

# There shouldn't be any TRACE, DEBUG or WARNING messages, and only one INFO
# message from io-stats xlator.
EXPECT "^0$" count_messages "T" "${logfile}"
EXPECT "^0$" count_messages "D" "${logfile}"
EXPECT "^1$" count_messages "I" "${logfile}"
EXPECT "^0$" count_messages "W" "${logfile}"

cleanup
