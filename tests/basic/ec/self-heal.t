#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks self-healing feature of dispersed volumes

cleanup

tmp=`mktemp -d -t ${0##*/}.XXXXXX`
if [ ! -d $tmp ]; then
    exit 1
fi

TESTS_EXPECTED_IN_LOOP=250

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 2 $H0:$B0/${V0}{0..5}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0;
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST dd if=/dev/urandom of=$tmp/test bs=1024 count=1024

cs=$(sha1sum $tmp/test | awk '{ print $1 }')

TEST df -h
TEST stat $M0

for idx in {0..5}; do
    brick[$idx]=$(gf_get_gfid_backend_file_path $B0/$V0$idx)
done


cd $M0
TEST cp $tmp/test test
TEST chmod 644 test
TEST touch -d "@946681200" test
EXPECT "-rw-r--r--" stat -c "%A" test
EXPECT "946681200" stat -c "%Y" test

for idx1 in {0..5}; do
    TEST chmod 666 ${brick[$idx1]}/test
    TEST truncate -s 0 ${brick[$idx1]}/test
    TEST setfattr -n user.test -v "test1" ${brick[$idx1]}/test
    sleep 1
    EXPECT "-rw-r--r--" stat -c "%A" test
    EXPECT_WITHIN $HEAL_TIMEOUT "262144" stat -c "%s" ${brick[$idx1]}/test
    EXPECT_WITHIN $HEAL_TIMEOUT "-rw-r--r--" stat -c "%A" ${brick[$idx1]}/test
    EXPECT_WITHIN $HEAL_TIMEOUT "946681200" stat -c "%Y" ${brick[$idx1]}/test
    TEST ! getfattr -n user.test ${brick[$idx1]}/test
done

for idx1 in {0..4}; do
    for idx2 in `seq $(($idx1 + 1)) 5`; do
        if [ $idx1 -ne $idx2 ]; then
            TEST chmod 666 ${brick[$idx1]}/test
            TEST chmod 600 ${brick[$idx2]}/test
            TEST truncate -s 0 ${brick[$idx1]}/test
            TEST truncate -s 2097152 ${brick[$idx2]}/test
            TEST setfattr -n user.test -v "test1" ${brick[$idx1]}/test
            TEST setfattr -n user.test -v "test2" ${brick[$idx2]}/test
            sleep 1
            EXPECT "-rw-r--r--" stat -c "%A" test
            EXPECT_WITHIN $HEAL_TIMEOUT "262144" stat -c "%s" ${brick[$idx1]}/test
            EXPECT_WITHIN $HEAL_TIMEOUT "262144" stat -c "%s" ${brick[$idx2]}/test
            EXPECT_WITHIN $HEAL_TIMEOUT "-rw-r--r--" stat -c "%A" ${brick[$idx1]}/test
            EXPECT_WITHIN $HEAL_TIMEOUT "-rw-r--r--" stat -c "%A" ${brick[$idx2]}/test
            EXPECT_WITHIN $HEAL_TIMEOUT "946681200" stat -c "%Y" ${brick[$idx1]}/test
            EXPECT_WITHIN $HEAL_TIMEOUT "946681200" stat -c "%Y" ${brick[$idx2]}/test
            TEST ! getfattr -n user.test ${brick[$idx1]}/test
            TEST ! getfattr -n user.test ${brick[$idx2]}/test
        fi
    done
done

TEST kill_brick $V0 $H0 $B0/${V0}0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST cp $tmp/test test2
EXPECT "1048576" stat -c "%s" test2
TEST chmod 777 test2
EXPECT "-rwxrwxrwx" stat -c "%A" test2

TEST mkdir dir1
TEST ls -al dir1

TEST ln -s test2 test3
TEST [ -h test3 ]

TEST ln test2 test4
TEST [ -f test4 ]
EXPECT "2" stat -c "%h" test2
EXPECT "2" stat -c "%h" test4

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

EXPECT "1048576" stat -c "%s" test2
EXPECT "-rwxrwxrwx" stat -c "%A" test2
EXPECT_WITHIN $HEAL_TIMEOUT "262144" stat -c "%s" ${brick[0]}/test2
EXPECT_WITHIN $HEAL_TIMEOUT "262144" stat -c "%s" ${brick[1]}/test2
EXPECT "-rwxrwxrwx" stat -c "%A" ${brick[0]}/test2
EXPECT "-rwxrwxrwx" stat -c "%A" ${brick[1]}/test2

TEST ls -al dir1
EXPECT_WITHIN $HEAL_TIMEOUT "1" eval "if [ -d ${brick[0]}/dir1 ]; then echo 1; fi"
EXPECT_WITHIN $HEAL_TIMEOUT "1" eval "if [ -d ${brick[1]}/dir1 ]; then echo 1; fi"

TEST [ -h test3 ]
EXPECT_WITHIN $HEAL_TIMEOUT "1" eval "if [ -h ${brick[0]}/test3 ]; then echo 1; fi"
EXPECT_WITHIN $HEAL_TIMEOUT "1" eval "if [ -h ${brick[1]}/test3 ]; then echo 1; fi"

EXPECT "2" stat -c "%h" test4
EXPECT_WITHIN $HEAL_TIMEOUT "3" stat -c "%h" ${brick[0]}/test4
EXPECT_WITHIN $HEAL_TIMEOUT "3" stat -c "%h" ${brick[1]}/test4

TEST rm -rf $tmp

cleanup
