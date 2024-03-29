#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=400
GEO_REP_TIMEOUT=200

##Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd


##Variables
GEOREP_CLI="$CLI volume geo-replication"
primary=$GMV0
secondary=${H0}::${GSV0}
num_active=2
num_passive=2
primary_mnt=$M0
secondary_mnt=$M1

############################################################
#SETUP VOLUMES AND GEO-REPLICATION
############################################################

##create_and_start_primary_volume
TEST $CLI volume create $GMV0 replica 2 $H0:$B0/${GMV0}{1,2,3,4};
TEST $CLI volume start $GMV0

##create_and_start_secondary_volume
TEST $CLI volume create $GSV0 replica 2 $H0:$B0/${GSV0}{1,2,3,4};
TEST $CLI volume start $GSV0

##Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

##Mount primary
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

##Mount secondary
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1

############################################################
#BASIC GEO-REPLICATION TESTS
############################################################

#Test invalid secondary url
TEST ! $GEOREP_CLI $primary ${H0}:${GSV0} create push-pem
TEST ! $GEOREP_CLI $primary ${H0}:::${GSV0} create push-pem

#Create geo-rep session
TEST create_georep_session $primary $secondary

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config secondary-gluster-command-dir ${GLUSTER_CMD_DIR}

#Enable_metavolume
TEST $GEOREP_CLI $primary $secondary config use_meta_volume true

#Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Verify the keys are distributed
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Start_georep
TEST $GEOREP_CLI $primary $secondary start

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Passive"

#get-state commamd shouldn't crash glusterd when geo-rep session is configured
TEST $CLI get-state
TEST pidof glusterd

TEST $CLI get-state detail
TEST pidof glusterd

#Pause geo-replication session
TEST $GEOREP_CLI  $primary $secondary pause

#Resume geo-replication session
TEST $GEOREP_CLI  $primary $secondary resume

#Validate failure of volume stop when geo-rep is running
TEST ! $CLI volume stop $GMV0

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Delete Geo-rep
TEST $GEOREP_CLI $primary $secondary delete

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
