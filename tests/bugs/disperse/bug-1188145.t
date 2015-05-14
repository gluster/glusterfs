#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function create_dirs()
{
    local stop=$1
    local idx
    local res

    res=0
    idx=1
    while [[ -f ${stop} ]]; do
        mkdir $M0/${idx}
        if [[ $? -ne 0 ]]; then
            res=1
            break;
        fi
        idx=$(( idx + 1 ))
    done

    return ${res}
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 2 $H0:$B0/${V0}{0..5}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

name=`mktemp -t ${0##*/}.XXXXXX`
create_dirs ${name} &
pid=$!

sleep 2
TEST $CLI volume set $V0 uss on
sleep 5
TEST $CLI volume set $V0 uss off
sleep 5

TEST rm -f ${name}
TEST wait ${pid}

cleanup
