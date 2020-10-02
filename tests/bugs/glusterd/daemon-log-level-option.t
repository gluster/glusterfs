#!/bin/bash

. $(dirname $0)/../../include.rc

function Info_messages_count() {
        local shd_log=$1
        cat $shd_log | grep " I " | wc -l
}

function Warning_messages_count() {
        local shd_log=$1
        cat $shd_log | grep " W " | wc -l
}

function Debug_messages_count() {
        local shd_log=$1
        cat $shd_log | grep " D " | wc -l
}

function Trace_messages_count() {
        local shd_log=$1
        cat $shd_log | grep " T " | wc -l
}

cleanup;

# Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

# set cluster.daemon-log-level option to DEBUG
TEST $CLI volume set all cluster.daemon-log-level DEBUG

#Create a 3X2 distributed-replicate volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1..6};
TEST $CLI volume start $V0

# log should not have any trace messages
EXPECT 0 Trace_messages_count "/var/log/glusterfs/glustershd.log"

# stop the volume and remove glustershd log
TEST $CLI volume stop $V0
rm -f /var/log/glusterfs/glustershd.log

# set cluster.daemon-log-level option to INFO and start the volume
TEST $CLI volume set all cluster.daemon-log-level INFO
TEST $CLI volume start $V0

# log should not have any debug messages
EXPECT 0 Debug_messages_count "/var/log/glusterfs/glustershd.log"

# log should not have any trace messages
EXPECT 0 Trace_messages_count "/var/log/glusterfs/glustershd.log"

# stop the volume and remove glustershd log
TEST $CLI volume stop $V0
rm -f /var/log/glusterfs/glustershd.log

# set cluster.daemon-log-level option to WARNING and start the volume
TEST $CLI volume set all cluster.daemon-log-level WARNING
TEST $CLI volume start $V0

# log does have 1 info message specific to configure ios_sample_buf_size in io-stats xlator
EXPECT 1 Info_messages_count "/var/log/glusterfs/glustershd.log"

# log should not have any debug messages
EXPECT 0 Debug_messages_count "/var/log/glusterfs/glustershd.log"

# log should not have any trace messages
EXPECT 0 Trace_messages_count "/var/log/glusterfs/glustershd.log"

# stop the volume and remove glustershd log
TEST $CLI volume stop $V0
rm -f /var/log/glusterfs/glustershd.log

# set cluster.daemon-log-level option to ERROR and start the volume
TEST $CLI volume set all cluster.daemon-log-level ERROR
TEST $CLI volume start $V0

# log does have 1 info message specific to configure ios_sample_buf_size in io-stats xlator
EXPECT 1 Info_messages_count "/var/log/glusterfs/glustershd.log"

# log should not have any warning messages
EXPECT 0 Warning_messages_count "/var/log/glusterfs/glustershd.log"

# log should not have any debug messages
EXPECT 0 Debug_messages_count "/var/log/glusterfs/glustershd.log"

# log should not have any trace messages
EXPECT 0 Trace_messages_count "/var/log/glusterfs/glustershd.log"

cleanup
