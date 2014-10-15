#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks self-healing feature of dispersed volumes

cleanup

function check_mount_dir
{
    for i in {1..20}; do
        ls | grep "dir1"
        if [ $? -ne 0 ]; then
            return 1
        fi
    done

    return 0
}

function check_size
{
    stat $1
    for i in "${brick[@]}"; do
        res=`stat -c "%s" $i/$1`
        if [ "$res" != "$2" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_mode
{
    stat $1
    for i in "${brick[@]}"; do
        res=`stat -c "%A" $i/$1`
        if [ "$res" != "$2" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_date
{
    stat $1
    for i in "${brick[@]}"; do
        res=`stat -c "%Y" $i/$1`
        if [ "$res" != "$2" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_xattr
{
    stat $1
    for i in "${brick[@]}"; do
        getfattr -n $2 $i/$1 2>/dev/null
        if [ $? -eq 0 ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_dir
{
    getfattr -m. -d dir1
    for i in "${brick[@]}"; do
        if [ ! -d $i/dir1 ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_soft_link
{
    stat test3
    for i in "${brick[@]}"; do
        if [ ! -h $i/test3 ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_hard_link
{
    stat test4
    for i in "${brick[@]}"; do
        if [ `stat -c "%h" $i/test4` -ne 3 ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

tmp=`mktemp -d -t ${0##*/}.XXXXXX`
if [ ! -d $tmp ]; then
    exit 1
fi

TESTS_EXPECTED_IN_LOOP=194

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 redundancy 2 $H0:$B0/${V0}{0..5}
EXPECT "Created" volinfo_field $V0 'Status'
TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Started" volinfo_field $V0 'Status'
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
# Wait until all 6 childs have been recognized by the ec xlator
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
    EXPECT "-rw-r--r--" stat -c "%A" test
    EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_size test "262144"
    EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_mode test "-rw-r--r--"
    EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_date test "946681200"
    EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_xattr test "user.test"
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
            EXPECT "-rw-r--r--" stat -c "%A" test
            EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_size test "262144"
            EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_mode test "-rw-r--r--"
            EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_date test "946681200"
            EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_xattr test "user.test"
        fi
    done
done

sleep 2

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

sleep 2

TEST $CLI volume start $V0 force
# Wait until the killed bricks have been started and recognized by the ec
# xlator
EXPECT_WITHIN $CHILD_UP_TIMEOUT "6" ec_child_up_count $V0 0

TEST check_mount_dir

EXPECT "1048576" stat -c "%s" test2
EXPECT "-rwxrwxrwx" stat -c "%A" test2
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_size test2 "262144"
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_mode test2 "-rwxrwxrwx"

TEST ls -al dir1
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_dir

TEST [ -h test3 ]
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_soft_link

EXPECT "2" stat -c "%h" test4
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_hard_link

TEST rm -rf $tmp

cleanup
