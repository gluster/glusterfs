#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function check_ec_size
{
    local res

    for i in {0..2}; do
        res=$(( `getfattr -n trusted.ec.size -e hex $B0/$V0$i/$1 | sed -n "s/^trusted.ec.size=//p"` ))
        if [ "x$res" == "x" -o "$res" != "$2" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 1 $H0:$B0/${V0}{0..2}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0

TEST dd if=/dev/zero of=$M0/file1 bs=4k count=1
TEST mv $M0/file1 $M0/file2
TEST ! stat $M0/file1
TEST stat $M0/file2

TEST build_tester $(dirname $0)/bug-1161886.c -lgfapi -Wall -O2
TEST $(dirname $0)/bug-1161886 $H0 $V0 /file2
EXPECT "^0$" stat -c "%s" $M0/file2
EXPECT "^Y$" check_ec_size file2 0

rm -f $(dirname $0)/bug-1161886

cleanup
