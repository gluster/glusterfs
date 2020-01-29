#!/usr/bin/env bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc


# Considering `--enable-metadisp` is an option for `./configure`,
# which is disabled by default, this test will never pass regression.
# But to see the value of this test, run below after configuring
# with above option :
# `prove -vmfe '/bin/bash' tests/basic/metadisp/metadisp.t`

#G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST

cleanup;

TEST mkdir -p $B0/b0/{0,1}

TEST setfattr -n trusted.glusterfs.volume-id -v 0xddab9eece7b64a95b07351a1f748f56f ${B0}/b0/0
TEST setfattr -n trusted.glusterfs.volume-id -v 0xddab9eece7b64a95b07351a1f748f56f ${B0}/b0/1

TEST $GFS --volfile=$(dirname $0)/metadisp.vol --volfile-id=$V0 $M0;

NUM_FILES=40
TEST touch $M0/{1..${NUM_FILES}}

# each drive should get 40 files
TEST [ $(dir -1 $B0/b0/0/ | wc -l) -eq $NUM_FILES ]
TEST [ $(dir -1 $B0/b0/1/ | wc -l) -eq $NUM_FILES ]

# now write some data to a file
echo "hello" > $M0/3
filename=$$
echo "hello" > /tmp/metadisp-write-${filename}
checksum=$(md5sum /tmp/metadisp-write-${filename} | awk '{print $1}')
TEST [ "$(md5sum $M0/3 | awk '{print $1}')" == "$checksum" ]

# check that the backend file exists on b1
gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/b0/*/3))
TEST [ $(dir -1 $B0/b0/1/$gfid | wc -l) -eq 1 ]

# check that the backend file matches the frontend
TEST [ "$(md5sum $B0/b0/1/$gfid | awk '{print $1}')" == "$checksum" ]

# delete the file
TEST rm $M0/3

# ensure the frontend and backend files are cleaned up
TEST ! -e $M0/3
TEST ! [ stat $B0/b*/*/$gfid ]

# Test TRUNCATE + WRITE flow
echo "hello" | tee $M0/4
echo "goo"   | tee $M0/4
filename=$$
echo "goo" | tee /tmp/metadisp-truncate-${filename}
checksum=$(md5sum /tmp/metadisp-truncate-${filename} | awk '{print $1}')
TEST [ "$(md5sum $M0/4 | awk '{print $1}')" == "$checksum" ]

# Test mkdir + rmdir.
TEST mkdir $M0/rmdir_me
nfiles=$(ls -d $B0/b*/*/rmdir_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "1" ]
TEST rmdir $M0/rmdir_me
nfiles=$(ls -d $B0/b*/*/rmdir_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "0" ]

# Test rename.
TEST touch $M0/rename_me
nfiles=$(ls $B0/b*/*/rename_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "1" ]
nfiles=$(ls $B0/b*/*/such_rename 2> /dev/null | wc -l)
TEST [ "$nfiles" = "0" ]
TEST mv $M0/rename_me $M0/such_rename
nfiles=$(ls $B0/b*/*/rename_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "0" ]
nfiles=$(ls $B0/b*/*/such_rename 2> /dev/null | wc -l)
TEST [ "$nfiles" = "1" ]

# Test rename of a file that doesn't exist.
TEST ! mv $M0/does-not-exist $M0/neither-does-this


# cleanup all the other files.
TEST rm -v $M0/1 $M0/2 $M0/{4..${NUM_FILES}}
TEST rm $M0/such_rename
TEST [ $(ls /d/backends/b0/0/ | wc -l) -eq 0 ]
TEST [ $(ls /d/backends/b0/1/ | wc -l) -eq 0 ]

# Test CREATE flow
NUM_FILES=40
TEST touch $M0/{1..${NUM_FILES}}
TEST [ $(ls /d/backends/b0/0/ | wc -l) -eq $NUM_FILES ]
TEST [ $(ls /d/backends/b0/1/ | wc -l) -eq $NUM_FILES ]

# Test UNLINK flow
# No drives should have any files
TEST rm -v $M0/{1..${NUM_FILES}}
TEST [ $(ls /d/backends/b0/0/ | wc -l) -eq 0 ]
TEST [ $(ls /d/backends/b0/1/ | wc -l) -eq 0 ]

# Test CREATE + WRITE + READ flow
filename=$$
dd if=/dev/urandom of=/tmp/${filename} bs=1M count=10
checksum=$(md5sum /tmp/${filename} | awk '{print $1}')
TEST cp -v /tmp/${filename} $M0/1
TEST cp -v /tmp/${filename} $M0/2
TEST cp -v /tmp/${filename} $M0/3
TEST cp -v /tmp/${filename} $M0/4
TEST [ "$(md5sum $M0/1 | awk '{print $1}')" == "$checksum" ]
TEST [ "$(md5sum $M0/2 | awk '{print $1}')" == "$checksum" ]
TEST [ "$(md5sum $M0/3 | awk '{print $1}')" == "$checksum" ]
TEST [ "$(md5sum $M0/4 | awk '{print $1}')" == "$checksum" ]

# Test TRUNCATE + WRITE flow
TEST dd if=/dev/zero of=$M0/1 bs=1M count=20

# Check that readdir stats the files properly and we get the correct sizes
TEST [ $(find $M0 -size +9M | wc -l) -eq 4 ];

# Test mkdir + rmdir.
TEST mkdir $M0/rmdir_me
nfiles=$(ls -d $B0/b*/*/rmdir_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "1" ]
TEST rmdir $M0/rmdir_me
nfiles=$(ls -d $B0/b*/*/rmdir_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "0" ]

# Test rename.
# Still flaky, so disabled until it can be debugged.
TEST touch $M0/rename_me
nfiles=$(ls $B0/b*/*/rename_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "1" ]
nfiles=$(ls $B0/b*/*/such_rename 2> /dev/null | wc -l)
TEST [ "$nfiles" = "0" ]
TEST mv $M0/rename_me $M0/such_rename
nfiles=$(ls $B0/b*/*/rename_me 2> /dev/null | wc -l)
TEST [ "$nfiles" = "0" ]
nfiles=$(ls $B0/b*/*/such_rename 2> /dev/null | wc -l)
TEST [ "$nfiles" = "1" ]

# Test rename of a file that doesn't exist.
TEST ! mv $M0/does-not-exist $M0/neither-does-this

# Test rename over an existing file.
ok=yes
for i in $(seq 0 9); do
        echo foo > $M0/src$i
        echo bar > $M0/dst$i
done
for i in $(seq 0 9); do
        mv $M0/src$i $M0/dst$i
done
for i in $(seq 0 9); do
        nfiles=$(cat $B0/b0/*/dst$i | wc -l)
        if [ "$nfiles" = "2" ]; then
                echo "COLLISION on dst$i"
                (ls -l $B0/b0/*/dst$i; cat $B0/b0/*/dst$i) | sed "/^/s//  /"
                ok=no
        fi
done
EXPECT "yes" echo $ok

# Test rename of a directory.
count_copies () {
        ls -d $B0/b?/?/$1 2> /dev/null | wc -l
}
TEST mkdir $M0/foo_dir
EXPECT 1 count_copies foo_dir
EXPECT 0 count_copies bar_dir
TEST mv $M0/foo_dir $M0/bar_dir
EXPECT 0 count_copies foo_dir
EXPECT 1 count_copies bar_dir

for x in $(seq 0 99); do
        touch $M0/target$x
        ln -s $M0/target$x $M0/link$x
done
on_0=$(ls $B0/b*/0/link* | wc -l)
on_1=$(ls $B0/b*/1/link* | wc -l)
TEST [ "$on_0" -eq 100 ]
TEST [ "$on_1" -eq 0 ]
TEST [ "$(ls -l $M0/link* | wc -l)" = 100 ]

# Test (hard) link.
_test_hardlink () {
        local b
        local has_src
        local has_dst
        local src_inum
        local dst_inum
        touch $M0/hardsrc$1
        ln $M0/hardsrc$1 $M0/harddst$1
        for b in $B0/b{0}/{0,1}; do
                [ -f $b/hardsrc$1 ]; has_src=$?
                [ -f $b/harddst$1 ]; has_dst=$?
                if [ "$has_src" != "$has_dst" ]; then
                        echo "MISSING $b/hardxxx$1 $has_src $has_dst"
                        return
                fi
                if [ "$has_src$has_dst" = "00" ]; then
                        src_inum=$(stat -c '%i' $b/hardsrc$1)
                        dst_inum=$(stat -c '%i' $b/harddst$1)
                        if [ "$dst_inum" != "$src_inum" ]; then
                                echo "MISMATCH $b/hardxx$i $src_inum $dst_inum"
                                return
                        fi
                fi
        done
        echo "OK"
}

test_hardlink () {
        local result=$(_test_hardlink $*)
        # [ "$result" = "OK" ] || echo $result > /dev/tty
        echo $result
}

# Do this multiple times to make sure colocation isn't a fluke.
EXPECT "OK" test_hardlink 0
EXPECT "OK" test_hardlink 1
EXPECT "OK" test_hardlink 2
EXPECT "OK" test_hardlink 3
EXPECT "OK" test_hardlink 4
EXPECT "OK" test_hardlink 5
EXPECT "OK" test_hardlink 6
EXPECT "OK" test_hardlink 7
EXPECT "OK" test_hardlink 8
EXPECT "OK" test_hardlink 9

# Test remove hardlink source. ensure deleting one file
# doesn't delete the data unless link-count is 1
TEST mkdir $M0/hardlink
TEST touch $M0/hardlink/fileA
echo "data" >> $M0/hardlink/fileA
checksum=$(md5sum $M0/hardlink/fileA | awk '{print $1}')
TEST ln $M0/hardlink/fileA $M0/hardlink/fileB
TEST [ $(dir -1 $M0/hardlink/ | wc -l) -eq 2 ]
TEST rm $M0/hardlink/fileA
TEST [ $(dir -1 $M0/hardlink/ | wc -l) -eq 1 ]
TEST [ "$(md5sum $M0/hardlink/fileB | awk '{print $1}')" == "$checksum" ]

#
# FIXME: statfs values look ok but the test is bad
#
# Test statfs. If we're doing it right, the numbers for the mountpoint should be
# double those for the brick filesystem times the number of bricks,
# but unless we're on a completely idle
# system (which never happens) the numbers can change even while this function
# runs and that would trip us up. Do a sloppy comparison to deal with that.
#compare_fields () {
#       val1=$(df $1 | grep / | awk "{print \$$3}")
#        val2=$(df $2 | grep / | awk "{print \$$3}")
#       [ "$val2" -gt "$(((val1/(29/10))*19/10))" -a "$val2" -lt "$(((val1/(31/10))*21/10))" ]
#}

#brick_df=$(df $B0 | grep /)
#mount_df=$(df $M0 | grep /)
#TEST compare_fields $B0 $M0 2       # Total blocks
#TEST compare_fields $B0 $M0 3       # Used
#TEST compare_fields $B0 $M0 4       # Available

# Test removexattr.
#RXATTR_FILE=$(get_file_not_on_disk0 rxtest)
#TEST setfattr -n user.foo -v bar $M0/$RXATTR_FILE
#TEST getfattr -n user.foo $B0/b0/1/$RXATTR_FILE
#TEST setfattr -x user.foo $M0/$RXATTR_FILE
#TEST ! getfattr -n user.foo $B0/b0/1/$RXATTR_FILE

# Test fsyncdir. We can't really test whether it's doing the right thing,
# but we can test that it doesn't fail and we can hand-check that it's calling
# down to all of the disks instead of just one.
#
# P.S. There's no fsyncdir test in the rest of Gluster, so who even knows if
# other translators are handling it correctly?

#FSYNCDIR_EXE=$(dirname $0)/fsyncdir
#build_tester ${FSYNCDIR_EXE}.c
#TEST touch $M0/fsyncdir_src
#TEST $FSYNCDIR_EXE $M0 $M0/fsyncdir_src $M0/fsyncdir_dst
#TEST rm -f $FSYNCDIR_EXE

# Test fsetxattr, fgetxattr, fremovexattr (in that order).
FXATTR_FILE=$M0/fxfile1
TEST touch $FXATTR_FILE
FXATTR_EXE=$(dirname $0)/fxattr
build_tester ${FXATTR_EXE}.c
TEST ! getfattr -n user.fxtest $FXATTR_FILE
TEST $FXATTR_EXE $FXATTR_FILE set value1
TEST getfattr -n user.fxtest $FXATTR_FILE
TEST setfattr -n user.fxtest -v value2 $FXATTR_FILE
TEST $FXATTR_EXE $FXATTR_FILE get value2
TEST $FXATTR_EXE $FXATTR_FILE remove
TEST ! getfattr -n user.fxtest $FXATTR_FILE
TEST rm -f $FXATTR_EXE

# Test ftruncate
FTRUNCATE_EXE=$(dirname $0)/ftruncate
build_tester ${FTRUNCATE_EXE}.c
FTRUNCATE_FILE=$M0/ftfile1
TEST dd if=/dev/urandom of=$FTRUNCATE_FILE count=1 bs=1MB 
TEST $FTRUNCATE_EXE $FTRUNCATE_FILE
#gfid=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/b0/*/ftfile1))

# Test fallocate, discard, zerofill. Actually we don't so much check that these
# *work* as that they don't throw any errors (especially ENOENT because the
# file's not on disk zero).
FALLOC_FILE=fatest1
TEST touch $M0/$FALLOC_FILE
TEST fallocate -l $((4096*5)) $M0/$FALLOC_FILE
TEST fallocate -p -o 4096 -l 4096 $M0/$FALLOC_FILE
# This actually fails with "operation not supported" on most filesystems, so
# don't leave it enabled except to test changes.
#TEST fallocate -z -o $((4096*3)) -l 4096 $M0/$FALLOC_FILE

#cleanup;
