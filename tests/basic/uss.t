#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc
. $(dirname $0)/../fileio.rc
. $(dirname $0)/../nfs.rc

function check_readonly()
{
    $@ 2>&1 | grep -q 'Read-only file system'
    return $?
}

function lookup()
{
    ls $1
    if [ "$?" == "0" ]
    then
        echo "Y"
    else
        echo "N"
    fi
}

cleanup;
TESTS_EXPECTED_IN_LOOP=10

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume set $V0 nfs.disable false


TEST $CLI volume start $V0;

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..10} ; do echo "file" > $M0/file$i ; done

# Create file and hard-links
TEST touch $M0/f1
TEST mkdir $M0/dir
TEST ln $M0/f1 $M0/f2
TEST ln $M0/f1 $M0/dir/f3

TEST $CLI snapshot config activate-on-create enable
TEST $CLI volume set $V0 features.uss enable;

TEST $CLI snapshot create snap1 $V0 no-timestamp;

for i in {11..20} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap2 $V0 no-timestamp;

########### Test inode numbers ###########
s1_f1_ino=$(STAT_INO $M0/.snaps/snap1/f1)
TEST [ $s1_f1_ino != 0 ]

# Inode number of f1 should be same as f2 f3 within snapshot
EXPECT $s1_f1_ino STAT_INO $M0/.snaps/snap1/f2
EXPECT $s1_f1_ino STAT_INO $M0/.snaps/snap1/dir/f3
EXPECT $s1_f1_ino STAT_INO $M0/dir/.snaps/snap1/f3

# Inode number of f1 in snap1 should be different from f1 in snap2
tmp_ino=$(STAT_INO $M0/.snaps/snap2/f1)
TEST [ $s1_f1_ino != $tmp_ino ]

# Inode number of f1 in snap1 should be different from f1 in regular volume
tmp_ino=$(STAT_INO $M0/f1)
TEST [ $s1_f1_ino != $tmp_ino ]

# Directory inode of snap1 should be different in each sub-dir
s1_ino=$(STAT_INO $M0/.snaps/snap1)
tmp_ino=$(STAT_INO $M0/dir/.snaps/snap1)
TEST [ $s1_ino != $tmp_ino ]
##########################################

mkdir $M0/dir1;
mkdir $M0/dir2;

for i in {1..10} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {1..10} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap3 $V0 no-timestamp;

for i in {11..20} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {11..20} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap4 $V0 no-timestamp;
## Test that features.uss takes only options enable/disable and throw error for
## any other argument.
for i in {1..10}; do
        RANDOM_STRING=$(uuidgen | tr -dc 'a-zA-Z' | head -c 8)
        TEST_IN_LOOP ! $CLI volume set $V0 features.uss $RANDOM_STRING
done

## Test that features.snapshot-directory:
##   contains only '0-9a-z-_'
#    starts with dot (.)
#    value cannot exceed 255 characters
## and throws error for any other argument.
TEST ! $CLI volume set $V0 features.snapshot-directory a/b
TEST ! $CLI volume set $V0 features.snapshot-directory snaps
TEST ! $CLI volume set $V0 features.snapshot-directory -a
TEST ! $CLI volume set $V0 features.snapshot-directory .
TEST ! $CLI volume set $V0 features.snapshot-directory ..
TEST ! $CLI volume set $V0 features.snapshot-directory .123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

# test 15
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "4" count_snaps $M0

NUM_SNAPS=$(ls $M0/.snaps | wc -l);

TEST [ $NUM_SNAPS == 4 ]
TEST ls $M0/.snaps/snap1;
TEST ls $M0/.snaps/snap2;
TEST ls $M0/.snaps/snap3;
TEST ls $M0/.snaps/snap4;

TEST ls $M0/.snaps/snap3/dir1;
TEST ls $M0/.snaps/snap3/dir2;

TEST ls $M0/.snaps/snap4/dir1;
TEST ls $M0/.snaps/snap4/dir2;

TEST ls $M0/dir1/.snaps/
TEST ! ls $M0/dir1/.snaps/snap1;
TEST ! ls $M0/dir2/.snaps/snap2;
TEST   ls $M0/dir1/.snaps/snap3;
TEST   ls $M0/dir2/.snaps/snap4;

TEST fd1=`fd_available`
TEST fd_open $fd1 'r' $M0/.snaps/snap1/file1;
TEST fd_cat $fd1

# opening fd with in write mode for snapshot files should fail
TEST fd2=`fd_available`
TEST ! fd_open $fd1 'w' $M0/.snaps/snap1/file2;

# lookup on .snaps in the snapshot world should fail
TEST ! stat $M0/.snaps/snap1/.snaps

# creating new entries in snapshots should fail
TEST check_readonly mkdir $M0/.snaps/new
TEST check_readonly touch $M0/.snaps/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $M0/dir1/.snaps/snap3/foo1

TEST fd_cat $fd3;

TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3


# similar tests on nfs mount
##Wait for connection establishment between nfs server and brick process
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;
#test 44
TEST mount_nfs $H0:/$V0 $N0 nolock;

NUM_SNAPS=$(ls $N0/.snaps | wc -l);

TEST [ $NUM_SNAPS == 4 ];

TEST stat $N0/.snaps/snap1;
TEST stat $N0/.snaps/snap2;

TEST ls -l $N0/.snaps;

# readdir + lookup on each entry
TEST ls -l $N0/.snaps/snap1;
TEST ls -l $N0/.snaps/snap2;

# readdir + access each entry by doing stat. If snapview-server has not
# filled the fs instance and handle in the inode context of the entry as
# part of readdirp, then when stat comes (i.e fop comes directly without
# a previous lookup), snapview-server should do a lookup of the entry via
# gfapi call and fill in the fs instance + handle information in the inode
# context
TEST ls $N0/.snaps/snap3/;
TEST stat $N0/.snaps/snap3/dir1;
TEST stat $N0/.snaps/snap3/dir2;

TEST ls -l $N0/.snaps/snap3/dir1;
TEST ls -l $N0/.snaps/snap3/dir2;
TEST ls -l $N0/.snaps/snap4/dir1;
TEST ls -l $N0/.snaps/snap4/dir2;

TEST ! ls -l $N0/dir1/.snaps/snap1;
TEST ! ls -l $N0/dir2/.snaps/snap2;
TEST   ls -l $N0/dir1/.snaps/snap3;
TEST   ls -l $N0/dir2/.snaps/snap4;


TEST fd1=`fd_available`
TEST fd_open $fd1 'r' $N0/.snaps/snap1/file1;
TEST fd_cat $fd1

TEST fd2=`fd_available`
TEST ! fd_open $fd1 'w' $N0/.snaps/snap1/file2;

TEST ! stat $N0/.snaps/snap1/.stat

TEST check_readonly mkdir $N0/.snaps/new

TEST check_readonly touch $N0/.snaps/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $N0/dir1/.snaps/snap3/foo1

TEST fd_cat $fd3;


TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3;

# test 73
TEST $CLI volume set $V0 "features.snapshot-directory" .history

#snapd client might take fraction of time to compare the volfile from glusterd
#hence a EXPECT_WITHIN is a better choice here
EXPECT_WITHIN 2 "Y" lookup "$M0/.history";

NUM_SNAPS=$(ls $M0/.history | wc -l);

TEST [ $NUM_SNAPS == 4 ]

TEST ls $M0/.history/snap1;
TEST ls $M0/.history/snap2;
TEST ls $M0/.history/snap3;
TEST ls $M0/.history/snap4;

TEST ls $M0/.history/snap3/dir1;
TEST ls $M0/.history/snap3/dir2;

TEST ls $M0/.history/snap4/dir1;
TEST ls $M0/.history/snap4/dir2;

TEST ls $M0/dir1/.history/
TEST ! ls $M0/dir1/.history/snap1;
TEST ! ls $M0/dir2/.history/snap2;
TEST   ls $M0/dir1/.history/snap3;
TEST   ls $M0/dir2/.history/snap4;

TEST fd1=`fd_available`
TEST fd_open $fd1 'r' $M0/.history/snap1/file1;
TEST fd_cat $fd1

# opening fd with in write mode for snapshot files should fail
TEST fd2=`fd_available`
TEST ! fd_open $fd1 'w' $M0/.history/snap1/file2;

# lookup on .history in the snapshot world should fail
TEST ! stat $M0/.history/snap1/.history

# creating new entries in snapshots should fail
TEST check_readonly mkdir $M0/.history/new
TEST check_readonly touch $M0/.history/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $M0/dir1/.history/snap3/foo1

TEST fd_cat $fd3;

TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3


# similar tests on nfs mount
# test 103
TEST ls $N0/.history;

NUM_SNAPS=$(ls $N0/.history | wc -l);

TEST [ $NUM_SNAPS == 4 ];

TEST ls -l $N0/.history/snap1;
TEST ls -l $N0/.history/snap2;
TEST ls -l $N0/.history/snap3;
TEST ls -l $N0/.history/snap4;

TEST ls -l $N0/.history/snap3/dir1;
TEST ls -l $N0/.history/snap3/dir2;

TEST ls -l $N0/.history/snap4/dir1;
TEST ls -l $N0/.history/snap4/dir2;

TEST ! ls -l $N0/dir1/.history/snap1;
TEST ! ls -l $N0/dir2/.history/snap2;
TEST   ls -l $N0/dir1/.history/snap3;
TEST   ls -l $N0/dir2/.history/snap4;

TEST fd1=`fd_available`
TEST fd_open $fd1 'r' $N0/.history/snap1/file1;
TEST fd_cat $fd1

TEST fd2=`fd_available`
TEST ! fd_open $fd1 'w' $N0/.history/snap1/file2;

TEST ! stat $N0/.history/snap1/.stat

TEST check_readonly mkdir $N0/.history/new

TEST check_readonly touch $N0/.history/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $N0/dir1/.history/snap3/foo1

TEST fd_cat $fd3;

TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3;

## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

#test 131
TEST $CLI snapshot create snap5 $V0 no-timestamp
TEST ls $M0/.history;

function count_snaps
{
    local mount_point=$1;
    local num_snaps;

    num_snaps=$(ls $mount_point/.history | wc -l);

    echo $num_snaps;
}

EXPECT_WITHIN 30 "5" count_snaps $M0;

# deletion of a snapshot and creation of a new snapshot with same name
# should not create problems. The data that was supposed to be present
# in the deleted snapshot need not be present in the new snapshot just
# because the name is same. Ex:
# 1) Create a file "aaa"
# 2) Create a snapshot snap6
# 3) stat the file "aaa" in snap6 and it should succeed
# 4) delete the file "aaa"
# 5) Delete the snapshot snap6
# 6) Create a snapshot snap6
# 7) stat the file "aaa" in snap6 and it should fail now

echo "aaa" > $M0/aaa;

TEST $CLI snapshot create snap6 $V0 no-timestamp

TEST ls $M0/.history;

EXPECT_WITHIN 30 "6" count_snaps $M0;

EXPECT_WITHIN 10 "Y" lookup $M0/.history/snap6/aaa

TEST rm -f $M0/aaa;

TEST $CLI snapshot delete snap6;

TEST $CLI snapshot create snap6 $V0 no-timestamp

TEST ls $M0/.history;

EXPECT_WITHIN 30 "6" count_snaps $M0;

TEST ls $M0/.history/snap6/;

TEST ! stat $M0/.history/snap6/aaa;

cleanup;
