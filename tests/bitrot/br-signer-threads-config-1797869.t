#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../cluster.rc

function get_bitd_count_1 {
        ps auxww | grep glusterfs | grep bitd.pid | grep -v grep | grep $H1 | wc -l
}

function get_bitd_count_2 {
        ps auxww | grep glusterfs | grep bitd.pid | grep -v grep | grep $H2 | wc -l
}

function get_bitd_pid_1 {
        ps auxww | grep glusterfs | grep bitd.pid | grep -v grep | grep $H1 | awk '{print $2}'
}

function get_bitd_pid_2 {
        ps auxww | grep glusterfs | grep bitd.pid | grep -v grep | grep $H2 | awk '{print $2}'
}

function get_signer_th_count_1 {
        ps -eL | grep $(get_bitd_pid_1) | grep glfs_brpobj | wc -l
}

function get_signer_th_count_2 {
        ps -eL | grep $(get_bitd_pid_2) | grep glfs_brpobj | wc -l
}

cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

TEST $CLI_1 volume create $V0 $H1:$B1
TEST $CLI_1 volume create $V1 $H2:$B2
EXPECT 'Created' volinfo_field_1 $V0 'Status';
EXPECT 'Created' volinfo_field_1 $V1 'Status';

TEST $CLI_1 volume start $V0
TEST $CLI_1 volume start $V1
EXPECT 'Started' volinfo_field_1 $V0 'Status';
EXPECT 'Started' volinfo_field_1 $V1 'Status';

#Enable bitrot
TEST $CLI_1 volume bitrot $V0 enable
TEST $CLI_1 volume bitrot $V1 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count_2

EXPECT_WITHIN $PROCESS_UP_TIMEOUT "4" get_signer_th_count_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "4" get_signer_th_count_2

old_bitd_pid_1=$(get_bitd_pid_1)
old_bitd_pid_2=$(get_bitd_pid_2)
TEST $CLI_1 volume bitrot $V0 signer-threads 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_signer_th_count_1
EXPECT_NOT "$old_bitd_pid_1" get_bitd_pid_1;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "4" get_signer_th_count_2
EXPECT "$old_bitd_pid_2" get_bitd_pid_2;

old_bitd_pid_1=$(get_bitd_pid_1)
old_bitd_pid_2=$(get_bitd_pid_2)
TEST $CLI_1 volume bitrot $V1 signer-threads 2
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" get_signer_th_count_2
EXPECT_NOT "$old_bitd_pid_2" get_bitd_pid_2;
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_signer_th_count_1
EXPECT "$old_bitd_pid_1" get_bitd_pid_1;

cleanup;
