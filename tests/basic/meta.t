#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../traps.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1..9};

EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $GFS -s $H0 --volfile-id $V0 $M0;

# verify json validity

TEST json_verify < $M0/.meta/frames;

TEST json_verify < $M0/.meta/cmdline;

TEST json_verify < $M0/.meta/version;

# default log level (INFO) is 7
TEST grep -q 7 $M0/.meta/logging/loglevel;

# check for attribute_timeout exposed through state dump
TEST grep -q attribute_timeout $M0/.meta/root/private;

# check for mount point specified as an option
TEST grep -q $M0 $M0/.meta/root/options/mountpoint;


# Series of tests to excercise runtime setting of root options
# via tuning fuse dump
dumpdir=$(mktemp -d)
push_trapfunc "rm -rf $dumpdir"
DUMPCTL=fuse-dumpfile
DUMPFILE=$dumpdir/test.fuse

# Test if non-absolute paths are not accepted
TEST "! echo everything/is/relative > $M0/.meta/root/options/$DUMPCTL"

# Test if paths that can't be created are not accepted
TEST "! echo $dumpdir/nosuchdir/test.fuse > $M0/.meta/root/options/$DUMPCTL"

# Test if valid input to $DUMPCTL is accepted and properly acted upon
# (ie. dump file is created)
TEST ! stat $DUMPFILE
TEST "echo $DUMPFILE > $M0/.meta/root/options/$DUMPCTL"
TEST stat $DUMPFILE

# Test if dumping happens upon using the mount
dumpsize1=`stat -c '%s' $DUMPFILE`
stat $M0 > /dev/null
dumpsize2=`stat -c '%s' $DUMPFILE`
TEST [ $dumpsize1 -ne $dumpsize2 ]

# Test if invalid input to $DUMPCTL is rejected, and leaves dumping intact
TEST "! echo silly thing > $M0/.meta/root/options/$DUMPCTL"
dumpsize1=`stat -c '%s' $DUMPFILE`
stat $M0 > /dev/null
dumpsize2=`stat -c '%s' $DUMPFILE`
TEST [ $dumpsize1 -ne $dumpsize2 ]

# Test if empty input to $DUMPCTL is accepted and stops dumping
TEST "echo > $M0/.meta/root/options/$DUMPCTL"
dumpsize1=`stat -c '%s' $DUMPFILE`
stat $M0 > /dev/null
dumpsize2=`stat -c '%s' $DUMPFILE`
TEST [ $dumpsize1 -eq $dumpsize2 ]


TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
