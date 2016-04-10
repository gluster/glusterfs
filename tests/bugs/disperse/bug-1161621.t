#!/bin/bash

. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 disperse 5 redundancy 2 $H0:$B0/${V0}{0..4}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "5" ec_child_up_count $V0 0

tmpdir=$(mktemp -d -t ${0##*/}.XXXXXX)
push_trapfunc "rm -rf $tmpdir"

TEST dd if=/dev/urandom of=$tmpdir/file bs=1234 count=20
cs=$(sha1sum $tmpdir/file | awk '{ print $1 }')
# Test O_APPEND on create
TEST dd if=$tmpdir/file of=$M0/file bs=1234 count=10 oflag=append
# Test O_APPEND on open
TEST dd if=$tmpdir/file of=$M0/file bs=1234 skip=10 oflag=append conv=notrunc
EXPECT "$cs" echo $(sha1sum $M0/file | awk '{ print $1 }')

# Fill a file with ff (I don't use 0's because empty holes created by an
# incorrect offset will be returned as 0's and won't be detected)
dd if=/dev/zero bs=24680 count=1000 | tr '\0' '\377' >$tmpdir/shared
cs=$(sha1sum $tmpdir/shared | awk '{ print $1 }')
# Test concurrent writes to the same file using O_APPEND
dd if=$tmpdir/shared of=$M0/shared bs=123400 count=100 oflag=append conv=notrunc &
dd if=$tmpdir/shared of=$M1/shared bs=123400 count=100 oflag=append conv=notrunc &
wait

EXPECT "24680000" stat -c "%s" $M0/shared
EXPECT "$cs" echo $(sha1sum $M0/shared | awk '{ print $1 }')

cleanup
