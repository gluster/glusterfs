#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

case $OSTYPE in
Linux)
        ;;
*)
        echo "Skip test: copy_file_range(2) is specific to Linux" >&2
        SKIP_TESTS
        exit 0
        ;;
esac

TEST glusterd

TEST mkdir $B0/bricks

# Just a single brick volume. More test cases need to be
# added in future for distribute, replicate,
# distributed replicate and distributed replicated sharded
# volumes.
TEST $CLI volume create $V0 $H0:$B0/bricks/brick1;
TEST ${CLI} volume set ${V0} open-behind off
TEST ${CLI} volume set ${V0} write-behind off
TEST ${CLI} volume set ${V0} stat-prefetch off
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST glusterfs --fuse-handle-copy_file_range --volfile-id=/$V0 --volfile-server=$H0 $M0

TEST dd if=/dev/urandom of=$M0/file bs=1M count=1;

# check for the existence of the created file
TEST stat  $M0/file;

# grab the size of the file
SRC_SIZE=$(stat -c %s $M0/file);

logdir=`gluster --print-logdir`

tester=${0%.t}

TEST build_tester ${tester}.c

$tester $M0/file $M0/new
res="${?}"
if [[ ${res} -eq 2 ]]; then
    echo "Skip test: copy_file_range(2) is not supported by current kernel" >&2
    SKIP_TESTS
    exit 0
fi

TEST [[ ${res} -eq 0 ]]

# check whether the destination file is created or not
TEST stat $M0/new

# check the size of the destination file
# XXX size will be 0, which can be worked around with some sleep
# TEST sleep 2
DST_SIZE=$(stat -c %s $M0/new);

# The sizes of the source and destination should be same.
# Atleast it ensures that, copy_file_range API is working
# as expected. Whether the actual cloning happened via reflink
# or a read/write happened is different matter.
TEST [ $SRC_SIZE == $DST_SIZE ];

# Go again (test case with already existing target)
# XXX this will fail
TEST ${tester} $M0/file $M0/new

cleanup_tester $tester

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
