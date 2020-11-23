#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=300

#Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd


#Variables
GEOREP_CLI="$CLI volume geo-replication"
primary=$GMV0
SH0="127.0.0.1"
secondary=${SH0}::${GSV0}
secondary1=root@${SH0}::${GSV1}
num_active=2
num_passive=2
primary_mnt=$M0
secondary_mnt=$M1

############################################################
#SETUP VOLUMES AND GEO-REPLICATION
############################################################

#create_and_start_primary_volume
TEST $CLI volume create $GMV0 replica 3 $H0:$B0/${GMV0}{1,2,3};

#Negative testase: Create geo-rep session, primary is not started
TEST ! $GEOREP_CLI $primary $secondary create push-pem

TEST $CLI volume start $GMV0

#create_and_start_secondary_volume
TEST $CLI volume create $GSV0 replica 3 $H0:$B0/${GSV0}{1,2,3};

#Negative testcase: Create geo-rep session, secondary is not started
TEST ! $GEOREP_CLI $primary $secondary create push-pem

TEST $CLI volume start $GSV0

#create_and_start_secondary1_volume
TEST $CLI volume create $GSV1 replica 3 $H0:$B0/${GSV1}{1,2,3};
TEST $CLI volume start $GSV1

#Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

#Mount primary
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

#Mount secondary
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1

############################################################
#BASIC GEO-REPLICATION GLUSTERD TESTS WITH FANOUT SETUP
############################################################

#Negative testcase: Test invalid primary
TEST ! $GEOREP_CLI primary1 ${SH0}::${GSV0} create push-pem

#Negatvie testcase: Test invalid secondary
TEST ! $GEOREP_CLI $primary ${SH0}::secondary3 create push-pem

##------------------- Session 1 Creation Begin-----------------##
#Create geo-rep session
TEST create_georep_session $primary $secondary

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config secondary-gluster-command-dir ${GLUSTER_CMD_DIR}

#Enable_metavolume
TEST $GEOREP_CLI $primary $secondary config use_meta_volume true
##------------------- Session 1 Creation End-----------------##

##------------------- Session 2 Creation Begin-----------------##
#Create geo-rep session2
TEST $GEOREP_CLI $primary $secondary1 create ssh-port 22 no-verify

#Config gluster-command-dir for session2
TEST $GEOREP_CLI $primary $secondary1 config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir for session2
TEST $GEOREP_CLI $primary $secondary1 config secondary-gluster-command-dir ${GLUSTER_CMD_DIR}

#Enable_metavolume for session2
TEST $GEOREP_CLI $primary $secondary1 config use_meta_volume true
##------------------- Session 2 Creation End-----------------##

#Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Verify the keys are distributed
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Start_georep
TEST $GEOREP_CLI $primary $secondary start

#check geo-rep status without specifying primary and secondary volumes
TEST $GEOREP_CLI status

#Start_georep force
TEST $GEOREP_CLI $primary $secondary1 start force

#Negative testcase: Create the same session after start, fails
#With root@ prefix
TEST ! $GEOREP_CLI $primary $secondary1 create push-pem
#Without root@ prefix
TEST ! $GEOREP_CLI $primary ${SH0}::${GSV1} create push-pem
TEST $GEOREP_CLI $primary $secondary1 create push-pem force

##------------------- Fanout status testcases Begin --------------##
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Passive"

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_fanout_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_fanout_status_num_rows "Passive"

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_fanout_status_detail_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_fanout_status_detail_num_rows "Passive"

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_all_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_all_status_num_rows "Passive"

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_all_status_detail_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_all_status_detail_num_rows "Passive"

##------------------- Fanout status testcases End --------------##

##------Checkpoint Testcase Begin---------------##
#Write I/O
echo "test data" > $M0/file1
TEST $GEOREP_CLI $primary $secondary config checkpoint now
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_checkpoint_met $primary $secondary
touch $M0
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 verify_checkpoint_met $primary $secondary
##------Checkpoint Testcase End---------------##

##------------------ Geo-rep config testcases Begin--------------------##
TEST $GEOREP_CLI $primary $secondary config
TEST ! $GEOREP_CLI $primary $secondary config arsync-options '-W'
TEST $GEOREP_CLI $primary $secondary config rsync-options '-W'
TEST $GEOREP_CLI $primary $secondary config rsync-options
TEST $GEOREP_CLI $primary $secondary config \!rsync-options
TEST $GEOREP_CLI $primary $secondary config sync-xattrs false
##------------------ Geo-rep config testcases End --------------------##

##---------------- Pause/Resume testcase Begin-------------##
#Negative testcase: Resume geo-replication session when not paused
TEST ! $GEOREP_CLI  $primary $secondary1 resume
TEST $GEOREP_CLI  $primary $secondary1 resume force

#Pause geo-replication session with root@
TEST $GEOREP_CLI $primary $secondary1 pause force

#Resume geo-replication session with root@
TEST $GEOREP_CLI $primary $secondary1 resume force

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary1 stop force

#Negative testcase: Resume geo-replication session after geo-rep stop
TEST ! $GEOREP_CLI  $primary $secondary1 resume
##---------------- Pause/Resume testcase End-------------##

##-----------------glusterd secondary key/value upgrade testcase Begin ---------##
#Upgrade test of secondary key stored in glusterd info file
src=$(grep secondary2 /var/lib/glusterd/vols/$primary/info)
#Remove secondary uuuid (last part after divided by : )
dst=${src%:*}

#Update glusterd info file with old secondary format
sed -i "s|$src|$dst|g" /var/lib/glusterd/vols/$primary/info
TEST ! grep $src /var/lib/glusterd/vols/$primary/info

#Restart glusterd to update in-memory volinfo
TEST pkill glusterd
TEST glusterd;
TEST pidof glusterd

#Start geo-rep and validate secondary format is updated
TEST $GEOREP_CLI $primary $secondary1 start force
TEST grep $src /var/lib/glusterd/vols/$primary/info
##-----------------glusted secondary key/value upgrade testcase End ---------##

#Negative testcase: Delete Geo-rep 2 fails as geo-rep is running
TEST ! $GEOREP_CLI $primary $secondary1 delete

#Stop and Delete Geo-rep 2
TEST $GEOREP_CLI $primary $secondary1 stop force
TEST $GEOREP_CLI $primary $secondary1 delete reset-sync-time

#Stop and Delete Geo-rep 1
TEST $GEOREP_CLI $primary $secondary stop
TEST $GEOREP_CLI $primary $secondary delete

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
