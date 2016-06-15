#!/bin/bash

# This test verifies that when 'lock-notify-contention' option is enabled,
# locks xlator actually sends an upcall notification that causes the acquired
# lock from one client to be released before it's supposed to when another
# client accesses the file.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function elapsed_time() {
        local start="`date +%s`"

        if [[ "test" == `cat "$1"` ]]; then
                echo "$((`date +%s` - ${start}))"
        fi
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 3 redundancy 1 $H0:$B0/${V0}{0..2}
TEST $CLI volume set $V0 performance.stat-prefetch off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.read-ahead off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 features.locks-notify-contention off
TEST $CLI volume set $V0 disperse.eager-lock on
TEST $CLI volume set $V0 disperse.eager-lock-timeout 6
TEST $CLI volume set $V0 disperse.other-eager-lock on
TEST $CLI volume set $V0 disperse.other-eager-lock-timeout 6
TEST $CLI volume start $V0

TEST $GFS --direct-io-mode=yes --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0 $M0

TEST $GFS --direct-io-mode=yes --volfile-id=/$V0 --volfile-server=$H0 $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0 $M1

TEST $(echo "test" >$M0/file)

# With locks-notify-contention set to off, accessing the file from another
# client should take 6 seconds. Checking against 3 seconds to be safe.
elapsed="$(elapsed_time $M1/file)"
TEST [[ ${elapsed} -ge 3 ]]

elapsed="$(elapsed_time $M0/file)"
TEST [[ ${elapsed} -ge 3 ]]

TEST $CLI volume set $V0 features.locks-notify-contention on

# With locks-notify-contention set to on, accessing the file from another
# client should be fast. Checking against 3 seconds to be safe.
elapsed="$(elapsed_time $M1/file)"
TEST [[ ${elapsed} -le 3 ]]

elapsed="$(elapsed_time $M0/file)"
TEST [[ ${elapsed} -le 3 ]]

cleanup
