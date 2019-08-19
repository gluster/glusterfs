#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/bug-1507896.c -lgfapi

TEST ./$(dirname $0)/bug-1507896 $H0 $V0 $logdir/bug-1507896.log

#volume name precedding with '/'
TEST ! ./$(dirname $0)/bug-1507896 $H0 /$V0 $logdir/bug-1507896.log

#volume name passed with any special characters
TEST ! ./$(dirname $0)/bug-1507896 $H0 test@_$V0 $logdir/bug-1507896.log

cleanup_tester $(dirname $0)/bug-1507896

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
