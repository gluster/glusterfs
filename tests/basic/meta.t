#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 4 $H0:$B0/${V0}{1..16};

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
TEST grep -q attribute_timeout $M0/.meta/master/private;

# check for mount point specified as an option
TEST grep -q $M0 $M0/.meta/master/options/mountpoint;

TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
