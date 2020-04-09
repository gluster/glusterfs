#! /bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

function check_peers {
count=`$CLI_3 peer status | grep 'Peer in Cluster (Connected)' | wc -l`
echo $count
}

function check_shd {
ps aux | grep $1 | grep glustershd | wc -l
}

cleanup


TEST launch_cluster 6

TESTS_EXPECTED_IN_LOOP=25
for i in $(seq 2 6); do
    hostname="H$i"
    TEST $CLI_1 peer probe ${!hostname}
done


EXPECT_WITHIN $PROBE_TIMEOUT 5 check_peers;
for i in $(seq 1 5); do

    TEST $CLI_1 volume create ${V0}_$i replica 3 $H1:$B1/${V0}_$i $H2:$B2/${V0}_$i $H3:$B3/${V0}_$i $H4:$B4/${V0}_$i $H5:$B5/${V0}_$i $H6:$B6/${V0}_$i
    TEST $CLI_1 volume start ${V0}_$i force

done

#kill a node
TEST kill_node 3

TEST $glusterd_3;
EXPECT_WITHIN $PROBE_TIMEOUT 5 check_peers

EXPECT_WITHIN $PROCESS_UP_TIMEOUT 1 check_shd $H3

for i in $(seq 1 5); do

    TEST $CLI_1 volume stop ${V0}_$i
    TEST $CLI_1 volume delete ${V0}_$i

done

for i in $(seq 1 6); do
    hostname="H$i"
    EXPECT_WITHIN $PROCESS_DOWN_TIMEOUT 0 check_shd ${!hostname}
done
cleanup
