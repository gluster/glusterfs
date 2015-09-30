#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc


checksticky () {
        i=0;
        while [ ! -k $1 ]; do
                sleep 1
                i=$((i+1));
                if [[ $i == 10 ]]; then
                        return $i
                fi
                echo "Waiting... $i"
        done
        echo "done ...got out @ $i"
        return 0
}

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..3};
TEST $CLI volume start $V0

# Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST mkdir $M0/dir1

# Create a large file (1GB), so that rebalance takes time
dd if=/dev/zero of=$M0/dir1/FILE2 bs=64k count=10240

# Rename the file to create a linkto, for rebalance to
# act on the file
TEST mv $M0/dir1/FILE2 $M0/dir1/FILE1

build_tester $(dirname $0)/bug-1193636.c

TEST $CLI volume rebalance $V0 start force

TEST checksticky $B0/${V0}3/dir1/FILE1

TEST setfattr -n "user.test1" -v "test1" $M0/dir1/FILE1
TEST setfattr -n "user.test2" -v "test1" $M0/dir1/FILE1
TEST setfattr -n "user.test3" -v "test1" $M0/dir1/FILE1

TEST $(dirname $0)/bug-1193636 $M0/dir1/FILE1 user.fsetx 0
TEST $(dirname $0)/bug-1193636 $M0/dir1/FILE1 user.fremx 0

TEST getfattr -n "user.fremx" $M0/dir1/FILE1
TEST setfattr -x "user.test2" $M0/dir1/FILE1


TEST $(dirname $0)/bug-1193636 $M0/dir1/FILE1 user.fremx 1

EXPECT_WITHIN $REBALANCE_TIMEOUT "completed" rebalance_status_field $V0

TEST getfattr -n "user.fsetx" $M0/dir1/FILE1
TEST getfattr -n "user.test1" $M0/dir1/FILE1
TEST ! getfattr -n "user.test2" $M0/dir1/FILE1
TEST ! getfattr -n "user.fremx" $M0/dir1/FILE1
TEST getfattr -n "user.test3" $M0/dir1/FILE1


cleanup;
