#!/bin/bash
# Test to check
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#Check if incorrect log-level keywords does not crash the CLI
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick1 $H0:$B0/brick2
TEST $CLI volume start $V0

function set_log_level_status {
        local level=$1
        $CLI volume set $V0 diagnostics.client-log-level $level 2>&1 |grep -oE 'success|failed'
}


LOG_LEVEL="trace"
EXPECT "failed" set_log_level_status $LOG_LEVEL


LOG_LEVEL="error-gen"
EXPECT "failed" set_log_level_status $LOG_LEVEL


LOG_LEVEL="TRACE"
EXPECT "success" set_log_level_status $LOG_LEVEL

EXPECT "$LOG_LEVEL" echo `$CLI volume info | grep diagnostics | awk '{print $2}'`

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
