#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../snapshot.rc
. $(dirname $0)/../fileio.rc

cleanup;

TEST init_n_bricks 3;
TEST setup_lvm 3;

# start glusterd
TEST glusterd;

TEST pidof glusterd;

TEST $CLI volume create $V0 $H0:$L1 $H0:$L2 $H0:$L3;
TEST $CLI volume set $V0 nfs.disable false


TEST $CLI volume start $V0;

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

for i in {1..10} ; do echo "file" > $M0/file$i ; done

# Create file and directory
TEST touch $M0/f1
TEST mkdir $M0/dir

TEST $CLI snapshot config activate-on-create enable
TEST $CLI volume set $V0 features.uss enable;

for i in {1..10} ; do echo "file" > $M0/dir/file$i ; done

TEST $CLI snapshot create snap1 $V0 no-timestamp;

for i in {11..20} ; do echo "file" > $M0/file$i ; done
for i in {11..20} ; do echo "file" > $M0/dir/file$i ; done

TEST $CLI snapshot create snap2 $V0 no-timestamp;

TEST fd1=`fd_available`
TEST fd_open $fd1 'r' $M0/.snaps/snap2/dir/file11;
TEST fd_cat $fd1

TEST $CLI snapshot delete snap2;

TEST ! fd_cat $fd1;

# the return value of this command (i.e. fd_close) depetends
# mainly on how the release operation on a file descriptor is
# handled in snapview-server process. As of now snapview-server
# returns 0 for the release operation. And it is similar to how
# posix xlator does. So, as of now the expectation is to receive
# success for the close operation.
TEST fd_close $fd1;

# This check is mainly to ensure that the snapshot daemon
# (snapd) is up and running. If it is not running, the following
# stat would receive ENOTCONN.

TEST stat $M0/.snaps/snap1/dir/file1

TEST $CLI snapshot delete snap1;

TEST rm -rf $M0/*;

TEST $CLI volume stop $V0;

TEST $CLI volume delete $V0;

cleanup
