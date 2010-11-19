#!/bin/bash

# This script can be used to provoke 35 fops (if afr is used),
# 28 fops (if afr is not used) (-fstat,-readdirp, and lk,xattrop calls)
# Pending are 7 procedures.
# getspec, fsyncdir, access, fentrylk, fsetxattr, fgetxattr, rchecksum
# TODO: add commands which can generate fops for missing fops

## Script tests below File Operations over RPC (when afr is used)

# CREATE
# ENTRYLK
# FINODELK
# FLUSH
# FSTAT
# FSYNC
# FTRUNCATE
# FXATTROP
# GETXATTR
# INODELK
# LINK
# LK
# LOOKUP
# MKDIR
# MKNOD
# OPEN
# OPENDIR
# READ
# READDIR
# READDIRP
# READLINK
# RELEASE
# RELEASEDIR
# REMOVEXATTR
# RENAME
# RMDIR
# SETATTR
# SETXATTR
# STAT
# STATFS
# SYMLINK
# TRUNCATE
# UNLINK
# WRITE
# XATTROP

set -e;
set -o pipefail;

function test_mkdir()
{
    mkdir -p $PFX/dir;

    test $(stat -c '%F' $PFX/dir) == "directory";
}


function test_create()
{
    : > $PFX/dir/file;

    test "$(stat -c '%F' $PFX/dir/file)" == "regular empty file";
}


function test_statfs()
{
    local size;

    size=$(stat -f -c '%s' $PFX/dir/file);

    test "x$size" != "x0";
}


function test_open()
{
    exec 4<$PFX/dir/file;
    exec 4>&-;
}


function test_write()
{
    dd if=/dev/zero of=$PFX/dir/file bs=65536 count=16 2>/dev/null;

    test $(stat -c '%s' $PFX/dir/file) == 1048576;
}


function test_read()
{
    local count;

    count=$(dd if=$PFX/dir/file bs=64k count=16 2>/dev/null | wc -c);

    test $count == 1048576;
}


function test_truncate()
{
    truncate -s 512 $PFX/dir/file;

    test $(stat -c '%s' $PFX/dir/file) == 512;

    truncate -s 0 $PFX/dir/file;

    test $(stat -c '%s' $PFX/dir/file) == 0;
}


function test_fstat()
{
    local msg;

    export PFX;
    msg=$(sh -c 'tail -f $PFX/dir/file --pid=$$ & sleep 1 && echo hooha > $PFX/dir/file && sleep 1');

    test "x$msg" == "xhooha";
}


function test_mknod()
{
    mknod -m 0666 $PFX/dir/block b 13 42;

    test "$(stat -c '%F %a %t %T' $PFX/dir/block)" == "block special file 666 d 2a";

    mknod -m 0666 $PFX/dir/char c 13 42;

    test "$(stat -c '%F %a %t %T' $PFX/dir/char)" == "character special file 666 d 2a";

    mknod -m 0666 $PFX/dir/fifo p;

    test "$(stat -c '%F %a' $PFX/dir/fifo)" == "fifo 666";
}


function test_symlink()
{
    local msg;

    ln -s $PFX/dir/file $PFX/dir/symlink;

    test "$(stat -c '%F' $PFX/dir/symlink)" == "symbolic link";

    msg=$(cat $PFX/dir/symlink);

    test "x$msg" == "xhooha";
}


function test_hardlink()
{
    local ino1;
    local ino2;
    local nlink1;
    local nlink2;
    local msg;

    ln $PFX/dir/file $PFX/dir/hardlink;

    ino1=$(stat -c '%i' $PFX/dir/file);
    nlink1=$(stat -c '%h' $PFX/dir/file);
    ino2=$(stat -c '%i' $PFX/dir/hardlink);
    nlink2=$(stat -c '%h' $PFX/dir/hardlink);

    test $ino1 == $ino2;
    test $nlink1 == 2;
    test $nlink2 == 2;

    msg=$(cat $PFX/dir/hardlink);

    test "x$msg" == "xhooha";
}


function test_rename()
{
    local ino1;
    local ino2;
    local ino3;
    local msg;

    #### file

    ino1=$(stat -c '%i' $PFX/dir/file);

    mv $PFX/dir/file $PFX/dir/file2;

    msg=$(cat $PFX/dir/file2);
    test "x$msg" == "xhooha";

    ino2=$(stat -c '%i' $PFX/dir/file2);
    test $ino1 == $ino2;

    mv $PFX/dir/file2 $PFX/dir/file;

    msg=$(cat $PFX/dir/file);
    test "x$msg" == "xhooha";

    ino3=$(stat -c '%i' $PFX/dir/file);
    test $ino1 == $ino3;

    #### dir

    ino1=$(stat -c '%i' $PFX/dir);

    mv $PFX/dir $PFX/dir2;

    ino2=$(stat -c '%i' $PFX/dir2);
    test $ino1 == $ino2;

    mv $PFX/dir2 $PFX/dir;

    ino3=$(stat -c '%i' $PFX/dir);
    test $ino1 == $ino3;
}


function test_chmod()
{
    local mode0;
    local mode1;
    local mode2;


    #### file

    mode0=$(stat -c '%a' $PFX/dir/file);

    chmod 0753 $PFX/dir/file;

    mode1=$(stat -c '%a' $PFX/dir/file);
    test 0$mode1 == 0753;

    chmod 0$mode0 $PFX/dir/file;

    mode2=$(stat -c '%a' $PFX/dir/file);

    test 0$mode2 == 0$mode0;

    #### dir

    mode0=$(stat -c '%a' $PFX/dir);

    chmod 0753 $PFX/dir;

    mode1=$(stat -c '%a' $PFX/dir);
    test 0$mode1 == 0753;

    chmod 0$mode0 $PFX/dir;

    mode2=$(stat -c '%a' $PFX/dir);

    test 0$mode2 == 0$mode0;
}


function test_chown()
{
    local user1;
    local user2;
    local group1;
    local group2;

    #### file

    user1=$(stat -c '%u' $PFX/dir/file);
    group1=$(stat -c '%g' $PFX/dir/file);

    chown 13:42 $PFX/dir/file;

    user2=$(stat -c '%u' $PFX/dir/file);
    group2=$(stat -c '%g' $PFX/dir/file);

    test $user2 == 13;
    test $group2 == 42;

    chown $user1:$group1 $PFX/dir/file;

    user2=$(stat -c '%u' $PFX/dir/file);
    group2=$(stat -c '%g' $PFX/dir/file);

    test $user2 == $user1;
    test $group2 == $group1;

    #### dir

    user1=$(stat -c '%u' $PFX/dir);
    group1=$(stat -c '%g' $PFX/dir);

    chown 13:42 $PFX/dir;

    user2=$(stat -c '%u' $PFX/dir);
    group2=$(stat -c '%g' $PFX/dir);

    test $user2 == 13;
    test $group2 == 42;

    chown $user1:$group1 $PFX/dir;

    user2=$(stat -c '%u' $PFX/dir);
    group2=$(stat -c '%g' $PFX/dir);

    test $user2 == $user1;
    test $group2 == $group1;
}


function test_utimes()
{
    local acc0;
    local acc1;
    local acc2;
    local mod0;
    local mod1;
    local mod2;

    #### file

    acc0=$(stat -c '%X' $PFX/dir/file);
    mod0=$(stat -c '%Y' $PFX/dir/file);

    sleep 1;
    touch -a $PFX/dir/file;

    acc1=$(stat -c '%X' $PFX/dir/file);
    mod1=$(stat -c '%Y' $PFX/dir/file);

    sleep 1;
    touch -m $PFX/dir/file;

    acc2=$(stat -c '%X' $PFX/dir/file);
    mod2=$(stat -c '%Y' $PFX/dir/file);

    test $acc0 != $acc1;
    test $acc1 == $acc2;
    test $mod0 == $mod1;
    test $mod1 != $mod2;

    #### dir

    acc0=$(stat -c '%X' $PFX/dir);
    mod0=$(stat -c '%Y' $PFX/dir);

    sleep 1;
    touch -a $PFX/dir;

    acc1=$(stat -c '%X' $PFX/dir);
    mod1=$(stat -c '%Y' $PFX/dir);

    sleep 1;
    touch -m $PFX/dir;

    acc2=$(stat -c '%X' $PFX/dir);
    mod2=$(stat -c '%Y' $PFX/dir);

    test $acc0 != $acc1;
    test $acc1 == $acc2;
    test $mod0 == $mod1;
    test $mod1 != $mod2;
}


function test_locks()
{
    exec 200>$PFX/dir/lockfile;

    ## exclusive locks test
    flock -e 200;
    ! flock -n -e $PFX/dir/lockfile -c true;
    ! flock -n -s $PFX/dir/lockfile -c true;
    flock -u 200;

    ## shared locks test
    flock -s 200;
    ! flock -n -e $PFX/dir/lockfile -c true;
    flock -n -s $PFX/dir/lockfile -c true;
    flock -u 200;

    exec 200>&-;

}


function test_readdir()
{
    /bin/ls $PFX/dir >/dev/null
}


function test_setxattr()
{
    setfattr -n trusted.testing -v c00k33 $PFX/dir/file;
}


function test_listxattr()
{
    getfattr -m trusted $PFX/dir/file 2>/dev/null | grep -q trusted.testing;
}


function test_getxattr()
{
    getfattr -n trusted.testing $PFX/dir/file 2>/dev/null | grep -q c00k33;
}


function test_removexattr()
{
    setfattr -x trusted.testing $PFX/dir/file;
    getfattr -n trusted.testing $PFX/dir/file 2>&1 | grep -q 'No such attribute';
}


function test_unlink()
{
    rm $PFX/dir/file;
}


function test_rmdir()
{
    rm -rf $PFX;
}


function run_tests()
{
    test_mkdir;
    test_create;
    test_statfs;
    test_open;
    test_write;
    test_read;
    test_truncate;
    test_fstat;
    test_mknod;
    test_hardlink;
    test_symlink;
    test_rename;
    test_chmod;
    test_chown;
    test_utimes;
    test_locks;
    test_readdir;
    test_setxattr;
    test_listxattr;
    test_getxattr;
    test_removexattr;
    test_unlink;
    test_rmdir;
}


function _init()
{
    DIR=$(pwd);
}


function parse_cmdline()
{
    if [ "x$1" == "x" ] ; then
        echo "Usage: $0 /path/mount"
        exit 1
    fi

    DIR=$1;

    if [ ! -d "$DIR" ] ; then
        echo "$DIR: not a directory"
        exit 1
    fi

    PFX="$DIR/coverage";
    rm -rvf $PFX;
}


function main()
{
    parse_cmdline "$@";

    run_tests;

    exit 0;
}


_init && main "$@";
