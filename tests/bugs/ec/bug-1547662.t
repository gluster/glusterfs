#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# Immediately after replace-brick, trusted.ec.version will be absent, so if it
# is present we can assume that heal was started on root
function root_heal_attempted {
        if [ -z $(get_hex_xattr trusted.ec.version $1) ]; then
                echo "N"
        else
                echo "Y"
        fi
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST ${CLI} volume create ${V0} disperse 6 redundancy 2 ${H0}:${B0}/${V0}{0..5}
TEST ${CLI} volume start ${V0}
TEST ${GFS} --volfile-server ${H0} --volfile-id ${V0} ${M0}
EXPECT_WITHIN ${CHILD_UP_TIMEOUT} "6" ec_child_up_count ${V0} 0

TEST mkdir ${M0}/base
TEST mkdir ${M0}/base/dir.{1,2}
TEST mkdir ${M0}/base/dir.{1,2}/dir.{1,2}
TEST mkdir ${M0}/base/dir.{1,2}/dir.{1,2}/dir.{1,2}
TEST mkdir ${M0}/base/dir.{1,2}/dir.{1,2}/dir.{1,2}/dir.{1,2}
TEST mkdir ${M0}/base/dir.{1,2}/dir.{1,2}/dir.{1,2}/dir.{1,2}/dir.{1,2}
TEST mkdir ${M0}/base/dir.{1,2}/dir.{1,2}/dir.{1,2}/dir.{1,2}/dir.{1,2}/dir.{1,2}

TEST ${CLI} volume replace-brick ${V0} ${H0}:${B0}/${V0}5 ${H0}:${B0}/${V0}6 commit force
EXPECT_WITHIN ${CHILD_UP_TIMEOUT} "6" ec_child_up_count ${V0} 0
EXPECT_WITHIN ${PROCESS_UP_TIMEOUT} "Y" glustershd_up_status
EXPECT_WITHIN ${CHILD_UP_TIMEOUT} "6" ec_child_up_count_shd ${V0} 0
EXPECT_WITHIN ${HEAL_TIMEOUT} "Y" root_heal_attempted ${B0}/${V0}6
EXPECT_WITHIN ${HEAL_TIMEOUT} "^0$" get_pending_heal_count ${V0}
EXPECT "^127$" echo $(find ${B0}/${V0}6/base -type d | wc -l)

cleanup;
