#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc
. $(dirname $0)/../fileio.rc
. $(dirname $0)/../nfs.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;

TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;

TEST $CLI volume start $V0;

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..10} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap1 $V0;

for i in {11..20} ; do echo "file" > $M0/file$i ; done

TEST $CLI snapshot create snap2 $V0;

mkdir $M0/dir1;
mkdir $M0/dir2;

for i in {1..10} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {1..10} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap3 $V0;

for i in {11..20} ; do echo "foo" > $M0/dir1/foo$i ; done
for i in {11..20} ; do echo "foo" > $M0/dir2/foo$i ; done

TEST $CLI snapshot create snap4 $V0;

TEST $CLI volume set $V0 features.uss enable;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

# test 15
TEST ls $M0/.snaps;

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
TEST ! mkdir $M0/.snaps/new
TEST ! touch $M0/.snaps/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $M0/dir1/.snaps/snap3/foo1

TEST fd_cat $fd3;

TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3


# similar tests on nfs mount
# test 44
TEST mount_nfs $H0:/$V0 $N0 nolock;

TEST ls -l $N0/.snaps;

NUM_SNAPS=$(ls $N0/.snaps | wc -l);

TEST [ $NUM_SNAPS == 4 ];

TEST ls -l $N0/.snaps/snap1;
TEST ls -l $N0/.snaps/snap2;
TEST ls -l $N0/.snaps/snap3;
TEST ls -l $N0/.snaps/snap4;

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

TEST ! mkdir $N0/.snaps/new

TEST ! touch $N0/.snaps/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $N0/dir1/.snaps/snap3/foo1

TEST fd_cat $fd3;


TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3;

# test 73
TEST $CLI volume set $V0 "features.snapshot-directory" .history

TEST ls $M0/.history;

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
TEST ! mkdir $M0/.history/new
TEST ! touch $M0/.history/snap2/other;

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

TEST ! mkdir $N0/.history/new

TEST ! touch $N0/.history/snap2/other;

TEST fd3=`fd_available`
TEST fd_open $fd3 'r' $N0/dir1/.history/snap3/foo1

TEST fd_cat $fd3;

TEST fd_close $fd1;
TEST fd_close $fd2;
TEST fd_close $fd3;

## Before killing daemon to avoid deadlocks
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" umount_nfs $N0

cleanup;
