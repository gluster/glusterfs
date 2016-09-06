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

function get_node_uuid {
        getfattr -n trusted.glusterfs.node-uuid --only-values $M0/FILE1 2>/dev/null
}

cleanup;

TEST launch_cluster 2

TEST $CLI_1 peer probe $H2;
EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count;

TEST $CLI_1 volume create $V0 replica 2 $H1:$B1 $H2:$B2
EXPECT 'Created' volinfo_field_1 $V0 'Status';

TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field_1 $V0 'Status';

uuid1=$($CLI_1 system:: uuid get | awk '{print $2}')
uuid2=$($CLI_2 system:: uuid get | awk '{print $2}')

##Mount $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H1 $M0

#Enable bitrot
TEST $CLI_1 volume bitrot $V0 enable
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count_2

#Create sample file
TEST `echo "1234" > $M0/FILE1`
TEST `echo "5678" > $M0/FILE2`
gfid1=$(getfattr -n glusterfs.gfid.string --only-values $M0/FILE1)
gfid2=$(getfattr -n glusterfs.gfid.string --only-values $M0/FILE2)

EXPECT "$uuid1" get_node_uuid;

#Corrupt file from back-end
TEST stat $B1/FILE1
TEST stat $B1/FILE2
echo "Corrupted data" >> $B1/FILE1
echo "Corrupted data" >> $B1/FILE2
#Manually set bad-file xattr since we can't wait for an hour for scrubber.
TEST setfattr -n trusted.bit-rot.bad-file -v 0x3100 $B1/FILE1
TEST setfattr -n trusted.bit-rot.bad-file -v 0x3100 $B1/FILE2
TEST touch "$B1/.glusterfs/quanrantine/$gfid1"
TEST chmod 000 "$B1/.glusterfs/quanrantine/$gfid1"
TEST touch "$B1/.glusterfs/quanrantine/$gfid2"
TEST chmod 000 "$B1/.glusterfs/quanrantine/$gfid2"
EXPECT "4" get_quarantine_count "$B1";

TEST $CLI_1 volume stop $V0
TEST $CLI_1 volume start $V0
EXPECT 'Started' volinfo_field_1 $V0 'Status';
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H1 $B1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status_1 $V0 $H2 $B2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count_1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" get_bitd_count_2
#Trigger lookup so that bitrot xlator marks file as bad in its inode context.
TEST stat $M0/FILE1
TEST stat $M0/FILE2

EXPECT "$uuid2" get_node_uuid;

#BUG 1308961
#Remove bad files from  mount, it should be removed from quarantine directory.
TEST rm -f $M0/FILE1
TEST ! stat "$B1/.glusterfs/quanrantine/$gfid1"

#BUG 1308961
#Set network.inode-lru-limit to 5 and exceed the limit by creating 10 other files.
#The bad entry from quarantine directory should not be removed.
TEST $CLI_1 volume set $V0 network.inode-lru-limit 5
for i in {1..10}
do
     echo "1234" > $M0/file_$i
done
TEST stat "$B1/.glusterfs/quanrantine/$gfid2"

cleanup;
