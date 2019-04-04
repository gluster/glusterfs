#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function count_up_bricks {
        $CLI --xml volume status $1 | grep '<status>1' | wc -l
}

function count_brick_processes {
        pgrep glusterfsd | wc -l
}

logdir=`gluster --print-logdir`

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume set all cluster.brick-multiplex on
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{1,2,3};
TEST $CLI volume create $V1 replica 3 $H0:$B0/${V1}{1,2,3};

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume start $V1;
EXPECT 'Started' volinfo_field $V1 'Status';


EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 count_up_bricks $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT 4 count_up_bricks $V1

EXPECT 1 count_brick_processes

# Mount V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

function client-log-file-name()
{
    logfilename=$M0".log"
    echo ${logfilename:1} | tr / -
}

function brick-log-file-name()
{
    logfilename=$B0"/"$V0"1.log"
    echo ${logfilename:1} | tr / -
}

log_file=$logdir"/"`client-log-file-name`
nofdlog=$(cat $log_file | grep " D " | wc -l)
TEST [ $((nofdlog)) -eq 0 ]

brick_log_file=$logdir"/bricks/"`brick-log-file-name`
nofdlog=$(cat $brick_log_file | grep " D " | wc -l)
TEST [ $((nofdlog)) -eq 0 ]

## Set brick-log-level to DEBUG
TEST $CLI volume set $V0 diagnostics.brick-log-level DEBUG

# Do some operation
touch $M0/file1

# Check debug message debug message should be exist only for V0
# Server xlator is common in brick_mux so after enabling DEBUG log
# some debug message should be available for other xlators like posix

brick_log_file=$logdir"/bricks/"`brick-log-file-name`
nofdlog=$(cat $brick_log_file | grep file1 | grep -v server | wc -l)
TEST [ $((nofdlog)) -ne 0 ]

#Check if any debug log exist in client-log file
nofdlog=$(cat $log_file | grep " D " | wc -l)
TEST [ $((nofdlog)) -eq 0 ]

## Set brick-log-level to INFO
TEST $CLI volume set $V0 diagnostics.brick-log-level INFO

## Set client-log-level to DEBUG
TEST $CLI volume set $V0 diagnostics.client-log-level DEBUG

# Do some operation
touch $M0/file2

nofdlog=$(cat $brick_log_file | grep " D " | grep file2 | wc -l)
TEST [ $((nofdlog)) -eq 0 ]

nofdlog=$(cat $log_file | grep " D " | wc -l)
TEST [ $((nofdlog)) -ne 0 ]

# Unmount V0
TEST umount $M0

#Mount V1
TEST glusterfs --volfile-id=$V1 --volfile-server=$H0 --entry-timeout=0 $M0;

#do some operation
touch $M0/file3


# DEBUG log level is enabled only for V0 so no debug message should be available
# in log specific to file2 creation except for server xlator, server xlator is
# common xlator in brick mulitplex
nofdlog=$(cat $brick_log_file | grep file3 | grep -v server | wc -l)
TEST [ $((nofdlog)) -eq 0 ]

# Unmount V1
TEST umount $M0

cleanup;
