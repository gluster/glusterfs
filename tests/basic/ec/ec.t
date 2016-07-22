#!/bin/bash

. $(dirname $0)/../../traps.rc
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TEST_USER=test-ec-user
TEST_UID=27341

function my_getfattr {
    getfattr --only-values -e text $* 2> /dev/null
}

function get_rep_count {
    v=$(my_getfattr -n trusted.jbr.rep-count $1)
    #echo $v > /dev/tty
    echo $v
}

function create_file {
    dd if=/dev/urandom of=$1 bs=4k count=$2 conv=sync 2> /dev/null
}

function setup_perm_file {
    mkdir $1/perm_dir               || return 1
    chown ${TEST_USER} $1/perm_dir              || return 1
    su ${TEST_USER} -c "touch $1/perm_dir/perm_file"    || return 1
    return 0
}

# Functions to check repair for specific operation types.

function check_create_write {
    for b in $*; do
        cmp $tmpdir/create-write $b/create-write || return 1
    done
    return 0
}

function check_truncate {
    truncate -s 8192 $tmpdir/truncate
    for b in $*; do
        cmp $tmpdir/truncate $b/truncate || return 1
    done
    return 0
}

function check_hard_link {
    stat $M0/hard-link-1
    stat $M0/hard-link-2
    for b in $*; do
        inum1=$(ls -i $b/hard-link-1 | cut -d' ' -f1)
        inum2=$(ls -i $b/hard-link-2 | cut -d' ' -f1)
        if [ "$inum1" != "$inum2" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_soft_link {
    stat $M0/soft-link
    for b in $*; do
        if [ "$(readlink $b/soft-link)" != "soft-link-tgt" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_unlink {
    stat $M0/unlink
    for b in $*; do
        if [ -e $b/unlink ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_mkdir {
    getfattr -m. -d $M0/mkdir
    for b in $*; do
        if [ ! -d $b/mkdir ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_rmdir {
    getfattr -m. -d $M0/rmdir
    for b in $*; do
        if [ -e $b/rmdir ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_setxattr {
    getfattr -d -m. -e hex $M0/setxattr
    for b in $*; do
        v=$(my_getfattr -n user.foo $b/setxattr)
        if [ "$v" != "ash_nazg_durbatuluk" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_removexattr {
    getfattr -d -m. -e hex $M0/removexattr
    for b in $*; do
        my_getfattr -n user.bar $b/removexattr 2> /dev/null
        if [ $? -eq 0 ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

function check_perm_file {
    stat $M0/perm_dir/perm_file
    getfattr -m. -d $M0/perm_dir
    b1=$1
    shift 1
    ftext=$(stat -c "%u %g %a" $b1/perm_dir/perm_file)
    #echo "first u/g/a = $ftext" > /dev/tty
    for b in $*; do
        btext=$(stat -c "%u %g %a" $b/perm_dir/perm_file)
        #echo "  next u/a/a = $btext" > /dev/tty
        if [ x"$btext" != x"$ftext" ]; then
            echo "N"
            return 0
        fi
    done
    echo "Y"
    return 0
}

cleanup

TEST useradd -o -M -u ${TEST_UID} ${TEST_USER}
push_trapfunc "userdel --force ${TEST_USER}"

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

TEST mkdir -p $B0/${V0}{0,1,2,3,4,5,6,7,8,9}
TEST $CLI volume create $V0 disperse 10 redundancy 2 $H0:$B0/${V0}{0,1,2,3,4,5,6,7,8,9}

EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'
EXPECT '10' brick_count $V0

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'

# Mount FUSE with caching disabled
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "10" ec_child_up_count $V0 0

# Create local files for comparisons etc.
tmpdir=$(mktemp -d -t ${0##*/}.XXXXXX)
push_trapfunc "rm -rf $tmpdir"
TEST create_file $tmpdir/create-write 10
TEST create_file $tmpdir/truncate 10

# Prepare files and directories we'll need later.
TEST cp $tmpdir/truncate $M0/
TEST touch $M0/hard-link-1
TEST touch $M0/unlink
TEST mkdir $M0/rmdir
TEST touch $M0/setxattr
TEST touch $M0/removexattr
TEST setfattr -n user.bar -v "ash_nazg_gimbatul" $M0/removexattr

sleep 2

# Kill a couple of bricks and allow some time for things to settle.
TEST kill_brick $V0 $H0 $B0/${V0}3
TEST kill_brick $V0 $H0 $B0/${V0}8
sleep 10

# Test create+write
TEST cp $tmpdir/create-write $M0/
# Test truncate
TEST truncate -s 8192 $M0/truncate
# Test hard link
TEST ln $M0/hard-link-1 $M0/hard-link-2
# Test soft link
TEST ln -s soft-link-tgt $M0/soft-link
# Test unlink
TEST rm $M0/unlink
# Test rmdir
TEST rmdir $M0/rmdir
# Test mkdir
TEST mkdir $M0/mkdir
# Test setxattr
TEST setfattr -n user.foo -v "ash_nazg_durbatuluk" $M0/setxattr
# Test removexattr
TEST setfattr -x user.bar $M0/removexattr
# Test uid/gid behavior
TEST setup_perm_file $M0

sleep 2

# Unmount/remount so that create/write and truncate don't see cached data.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $GFS -s $H0 --volfile-id $V0 $M1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "8" ec_child_up_count $V0 0

# Test create/write and truncate *before* the bricks are brought back.
TEST check_create_write $M1
TEST check_truncate $M1

# Restart the bricks and allow repair to occur.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 'Started' volinfo_field $V0 'Status'
EXPECT_WITHIN $CHILD_UP_TIMEOUT "10" ec_child_up_count $V0 0

# Unmount/remount again, same reason as before.
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1
TEST $GFS -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "10" ec_child_up_count $V0 0

# Make sure everything is as it should be.  Most tests check for consistency
# between the bricks and the front end.  This is not valid for disperse, so we
# check the mountpoint state instead.

TEST check_create_write $M0
TEST check_truncate $M0

EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_hard_link $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_soft_link $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_unlink $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_rmdir $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_mkdir $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_setxattr $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_removexattr $B0/${V0}{0..9}
EXPECT_WITHIN $HEAL_TIMEOUT "Y" check_perm_file $B0/${V0}{0..9}

cleanup
