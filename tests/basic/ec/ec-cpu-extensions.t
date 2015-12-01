#!/bin/bash

DISPERSE=6
REDUNDANCY=2

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TESTS_EXPECTED_IN_LOOP=96

function check_contents
{
    local src=$1
    local cs=$2

    TEST cp $src $M0/file
    TEST [ -f $M0/file ]

    for ext in none x64 sse avx; do
        EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
        TEST $CLI volume set $V0 disperse.cpu-extensions $ext
        TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
        EXPECT_WITHIN $CHILD_UP_TIMEOUT "$DISPERSE" ec_child_up_count $V0 0

        EXPECT "$cs" echo $(sha1sum $M0/file | awk '{ print $1 }')
    done

    TEST rm -f $M0/file
}

cleanup

tmp=`mktemp -p ${LOGDIR} -d -t ${0##*/}.XXXXXX`
if [ ! -d $tmp ]; then
    exit 1
fi

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy $REDUNDANCY $H0:$B0/${V0}{1..$DISPERSE}
TEST $CLI volume set $V0 performance.flush-behind off
EXPECT 'Created' volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'

TEST dd if=/dev/urandom of=$tmp/file bs=1048576 count=1
cs_file=$(sha1sum $tmp/file | awk '{ print $1 }')

for ext in none x64 sse avx; do
    TEST $CLI volume set $V0 disperse.cpu-extensions $ext
    TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
    EXPECT_WITHIN $CHILD_UP_TIMEOUT "$DISPERSE" ec_child_up_count $V0 0

    check_contents $tmp/file $cs_file

    EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
done

TEST rm -rf $tmp

cleanup
