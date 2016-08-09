#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

TEST glusterd

TEST $CLI volume create $V0 ${H0}:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

# Special Snowflake NetBSD isn't set up to run tests that involve building
# executables.  Until that's fixed somewhere else, patch things up here.
if [ x"$OSTYPE" = x"NetBSD" ]; then
	mkdir -p $logdir
	extra_cflags="-I/build/install/include -L/build/install/lib"
fi

build_tester $(dirname $0)/gfapi-trunc.c -lgfapi ${extra_cflags}

TEST ./$(dirname $0)/gfapi-trunc $V0 $logdir/gfapi-trunc.log

cleanup_tester $(dirname $0)/gfapi-trunc

cleanup;
