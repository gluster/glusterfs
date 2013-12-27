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

#set -e;
set -o pipefail;

function fail() {
    echo "$*: failed.";
    exit 1;
}

function test_mkdir()
{
    mkdir -p $PFX/dir;
    test $(stat -c '%F' $PFX/dir) == "directory" || fail "mkdir"
}


function test_create()
{
    : > $PFX/dir/file;

    test "$(stat -c '%F' $PFX/dir/file)" == "regular empty file" || fail "create"
}


function test_statfs()
{
    local size;

    size=$(stat -f -c '%s' $PFX/dir/file);
    test "x$size" != "x0" || fail "statfs"
}


function test_open()
{
    exec 4<$PFX/dir/file || fail "open"
    exec 4>&- || fail "open"
}


function test_write()
{
    dd if=/dev/zero of=$PFX/dir/file bs=65536 count=16 2>/dev/null;
    test $(stat -c '%s' $PFX/dir/file) == 1048576 || fail "open"
}


function test_read()
{
    local count;

    count=$(dd if=$PFX/dir/file bs=64k count=16 2>/dev/null | wc -c);
    test $count == 1048576 || fail "read"
}


function test_truncate()
{
    truncate -s 512 $PFX/dir/file;
    test $(stat -c '%s' $PFX/dir/file) == 512 || fail "truncate"

    truncate -s 0 $PFX/dir/file;
    test $(stat -c '%s' $PFX/dir/file) == 0 || fail "truncate"
}


function test_fstat()
{
    local msg;

    export PFX;
    msg=$(sh -c 'tail -f $PFX/dir/file --pid=$$ & sleep 1 && echo hooha > $PFX/dir/file && sleep 1');
    test "x$msg" == "xhooha" || fail "fstat"
}


function test_mknod()
{
    mknod -m 0666 $PFX/dir/block b 13 42;
    test "$(stat -c '%F %a %t %T' $PFX/dir/block)" == "block special file 666 d 2a" \
	|| fail "mknod for block device"

    mknod -m 0666 $PFX/dir/char c 13 42;
    test "$(stat -c '%F %a %t %T' $PFX/dir/char)" == "character special file 666 d 2a" \
	|| fail "mknod for character device"

    mknod -m 0666 $PFX/dir/fifo p;
    test "$(stat -c '%F %a' $PFX/dir/fifo)" == "fifo 666" || \
	fail "mknod for fifo"
}


function test_symlink()
{
    local msg;

    pushd;
    cd $PFX/dir;
    ln -s file symlink;
    popd;
    test "$(stat -c '%F' $PFX/dir/symlink)" == "symbolic link" || fail "Creation of symlink"

    msg=$(cat $PFX/dir/symlink);
    test "x$msg" == "xhooha" || fail "Content match for symlink"
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

    test $ino1 == $ino2 || fail "Inode comparison for hardlink"
    test $nlink1 == 2 || fail "Link count for hardlink"
    test $nlink2 == 2 || fail "Link count for hardlink"

    msg=$(cat $PFX/dir/hardlink);

    test "x$msg" == "xhooha" || fail "Content match for hardlinks"
}


function test_rename()
{
    local ino1;
    local ino2;
    local ino3;
    local msg;

    #### file

    ino1=$(stat -c '%i' $PFX/dir/file);

    mv $PFX/dir/file $PFX/dir/file2 || fail "mv"
    msg=$(cat $PFX/dir/file2);
    test "x$msg" == "xhooha" || fail "File contents comparison after mv"

    ino2=$(stat -c '%i' $PFX/dir/file2);
    test $ino1 == $ino2 || fail "Inode comparison after mv"

    mv $PFX/dir/file2 $PFX/dir/file;
    msg=$(cat $PFX/dir/file);
    test "x$msg" == "xhooha" || fail "File contents comparison after mv"

    ino3=$(stat -c '%i' $PFX/dir/file);
    test $ino1 == $ino3 || fail "Inode comparison after mv"

    #### dir

    ino1=$(stat -c '%i' $PFX/dir);

    mv $PFX/dir $PFX/dir2 || fail "Directory mv"
    ino2=$(stat -c '%i' $PFX/dir2);
    test $ino1 == $ino2 || fail "Inode comparison after directory mv"

    mv $PFX/dir2 $PFX/dir || fail "Directory mv"
    ino3=$(stat -c '%i' $PFX/dir);
    test $ino1 == $ino3 || fail "Inode comparison after directory mv"
}


function test_chmod()
{
    local mode0;
    local mode1;
    local mode2;


    #### file

    mode0=$(stat -c '%a' $PFX/dir/file);
    chmod 0753 $PFX/dir/file || fail "chmod"

    mode1=$(stat -c '%a' $PFX/dir/file);
    test 0$mode1 == 0753 || fail "Mode comparison after chmod"

    chmod 0$mode0 $PFX/dir/file || fail "chmod"
    mode2=$(stat -c '%a' $PFX/dir/file);
    test 0$mode2 == 0$mode0 || fail "Mode comparison after chmod"

    #### dir

    mode0=$(stat -c '%a' $PFX/dir);
    chmod 0753 $PFX/dir || fail "chmod"

    mode1=$(stat -c '%a' $PFX/dir);
    test 0$mode1 == 0753 || fail "Mode comparison after chmod"

    chmod 0$mode0 $PFX/dir || fail "chmod"
    mode2=$(stat -c '%a' $PFX/dir);
    test 0$mode2 == 0$mode0  || fail "Mode comparison after chmod"
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

    chown 13:42 $PFX/dir/file || fail "chown"

    user2=$(stat -c '%u' $PFX/dir/file);
    group2=$(stat -c '%g' $PFX/dir/file);

    test $user2 == 13 || fail "User comparison after chown"
    test $group2 == 42 || fail "Group comparison after chown"

    chown $user1:$group1 $PFX/dir/file || fail "chown"

    user2=$(stat -c '%u' $PFX/dir/file);
    group2=$(stat -c '%g' $PFX/dir/file);

    test $user2 == $user1 || fail "User comparison after chown"
    test $group2 == $group1 || fail "Group comparison after chown"

    #### dir

    user1=$(stat -c '%u' $PFX/dir);
    group1=$(stat -c '%g' $PFX/dir);

    chown 13:42 $PFX/dir || fail "chown"

    user2=$(stat -c '%u' $PFX/dir);
    group2=$(stat -c '%g' $PFX/dir);

    test $user2 == 13 || fail "User comparison after chown"
    test $group2 == 42 || fail "Group comparison after chown"

    chown $user1:$group1 $PFX/dir || fail "chown"

    user2=$(stat -c '%u' $PFX/dir);
    group2=$(stat -c '%g' $PFX/dir);

    test $user2 == $user1 || fail "User comparison after chown"
    test $group2 == $group1 || fail "Group comparison after chown"
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
    touch -a $PFX/dir/file || fail "atime change on file"

    acc1=$(stat -c '%X' $PFX/dir/file);
    mod1=$(stat -c '%Y' $PFX/dir/file);

    sleep 1;
    touch -m $PFX/dir/file || fail "mtime change on file"

    acc2=$(stat -c '%X' $PFX/dir/file);
    mod2=$(stat -c '%Y' $PFX/dir/file);

    test $acc0 != $acc1 || fail "atime mismatch comparison on file"
    test $acc1 == $acc2 || fail "atime match comparison on file"
    test $mod0 == $mod1 || fail "mtime match comparison on file"
    test $mod1 != $mod2 || fail "mtime mismatch comparison on file"

    #### dir

    acc0=$(stat -c '%X' $PFX/dir);
    mod0=$(stat -c '%Y' $PFX/dir);

    sleep 1;
    touch -a $PFX/dir || fail "atime change on directory"

    acc1=$(stat -c '%X' $PFX/dir);
    mod1=$(stat -c '%Y' $PFX/dir);

    sleep 1;
    touch -m $PFX/dir || fail "mtime change on directory"

    acc2=$(stat -c '%X' $PFX/dir);
    mod2=$(stat -c '%Y' $PFX/dir);

    test $acc0 != $acc1 || fail "atime mismatch comparison on directory"
    test $acc1 == $acc2 || fail "atime match comparison on directory"
    test $mod0 == $mod1 || fail "mtime match comparison on directory"
    test $mod1 != $mod2 || fail "mtime mismatch comparison on directory"
}


function test_locks()
{
    exec 200>$PFX/dir/lockfile || fail "exec"

    ## exclusive locks test
    flock -e 200 || fail "flock -e"
    ! flock -n -e $PFX/dir/lockfile -c true || fail "! flock -n -e"
    ! flock -n -s $PFX/dir/lockfile -c true || fail "! flock -n -s"
    flock -u 200 || fail "flock -u"

    ## shared locks test
    flock -s 200 || fail "flock -s"
    ! flock -n -e $PFX/dir/lockfile -c true || fail "! flock -n -e"
    flock -n -s $PFX/dir/lockfile -c true || fail "! flock -n -s"
    flock -u 200 || fail "flock -u"

    exec 200>&- || fail "exec"

}


function test_readdir()
{
    /bin/ls $PFX/dir >/dev/null || fail "ls"
}


function test_setxattr()
{
    setfattr -n trusted.testing -v c00k33 $PFX/dir/file || fail "setfattr"
}


function test_listxattr()
{
    getfattr -m trusted $PFX/dir/file 2>/dev/null | grep -q trusted.testing || fail "getfattr"
}


function test_getxattr()
{
    getfattr -n trusted.testing $PFX/dir/file 2>/dev/null | grep -q c00k33 || fail "getfattr"
}


function test_removexattr()
{
    setfattr -x trusted.testing $PFX/dir/file || fail "setfattr remove"
    getfattr -n trusted.testing $PFXf/dir/file 2>&1 | grep -q "No such attribute"
}


function test_unlink()
{
    rm $PFX/dir/file || fail "rm"
}


function test_rmdir()
{
    rm -rf $PFX || fail "rm -rf"
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
