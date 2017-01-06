#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
    $CLI_1 peer status | grep 'Peer in Cluster (Connected)' | wc -l
}
cleanup

#setup cluster and test volume
TEST launch_cluster 3; # start 3-node virtual cluster
TEST $CLI_1 peer probe $H2; # peer probe server 2 from server 1 cli
TEST $CLI_1 peer probe $H3; # peer probe server 3 from server 1 cli

EXPECT_WITHIN $PROBE_TIMEOUT 2 check_peers;

TEST $CLI_1 volume create $V0 $H1:$B1/$V0 $H2:$B2/$V0 $H3:$B3/$V0
TEST $CLI_1 volume start $V0

# test CLI parameter acceptance
TEST ! $CLI_1 volume statedump $V0 client $H2:0
TEST ! $CLI_2 volume statedump $V0 client $H2:-1
TEST $CLI_3 volume statedump $V0 client $H2:765
TEST ! $CLI_1 volume statedump $V0 client $H2:
TEST ! $CLI_2 volume statedump $V0 client
TEST ! $CLI_3 volume statedump $V0 client $H2 $GFAPI_PID

# build and run a gfapi appliction for triggering a statedump
logdir=`gluster --print-logdir`
STATEDUMP_TIMEOUT=60

build_tester $(dirname $0)/bug-1169302.c -lgfapi
$(dirname $0)/bug-1169302 $V0 $H1 $logdir/bug-1169302.log testfile & GFAPI_PID=$!

cleanup_statedump

# Take the statedump of the process connected to $H1, it should match the
# hostname or IP-address with the connection from the bug-1169302 executable.
# In our CI it seems not possible to use $H0, 'localhost', $(hostname --fqdn)
# or even "127.0.0.1"....
TEST $CLI_3 volume statedump $V0 client $H1:$GFAPI_PID
EXPECT_WITHIN $STATEDUMP_TIMEOUT "Y" path_exists $statedumpdir/glusterdump.$GFAPI_PID*

kill $GFAPI_PID

cleanup_statedump
cleanup_tester $(dirname $0)/bug-1169302
cleanup
