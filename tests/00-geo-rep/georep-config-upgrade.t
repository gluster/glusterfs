#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=300
OLD_CONFIG_PATH=$(dirname $0)/gsyncd.conf.old
WORKING_DIR=/var/lib/glusterd/geo-replication/primary_127.0.0.1_secondary

##Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd

##Variables
GEOREP_CLI="$CLI volume geo-replication"
primary=$GMV0
SH0="127.0.0.1"
secondary=${SH0}::${GSV0}
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

TEST $GEOREP_CLI $primary $secondary config sync-method tarssh

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Copy old config file
mv -f $WORKING_DIR/gsyncd.conf $WORKING_DIR/gsyncd.conf.org
cp -p $OLD_CONFIG_PATH $WORKING_DIR/gsyncd.conf

#Check if config get all updates config_file
TEST ! grep "sync-method" $WORKING_DIR/gsyncd.conf
TEST $GEOREP_CLI $primary $secondary config
TEST grep "sync-method" $WORKING_DIR/gsyncd.conf

#Check if config get updates config_file
rm -f $WORKING_DIR/gsyncd.conf
cp -p $OLD_CONFIG_PATH $WORKING_DIR/gsyncd.conf
TEST ! grep "sync-method" $WORKING_DIR/gsyncd.conf
TEST $GEOREP_CLI $primary $secondary config sync-method
TEST grep "sync-method" $WORKING_DIR/gsyncd.conf

#Check if config set updates config_file
rm -f $WORKING_DIR/gsyncd.conf
cp -p $OLD_CONFIG_PATH $WORKING_DIR/gsyncd.conf
TEST ! grep "sync-method" $WORKING_DIR/gsyncd.conf
TEST $GEOREP_CLI $primary $secondary config sync-xattrs false
TEST grep "sync-method" $WORKING_DIR/gsyncd.conf

#Check if config reset updates config_file
rm -f $WORKING_DIR/gsyncd.conf
cp -p $OLD_CONFIG_PATH $WORKING_DIR/gsyncd.conf
TEST ! grep "sync-method" $WORKING_DIR/gsyncd.conf
TEST $GEOREP_CLI $primary $secondary config \!sync-xattrs
TEST grep "sync-method" $WORKING_DIR/gsyncd.conf

#Check if geo-rep start updates config_file
rm -f $WORKING_DIR/gsyncd.conf
cp -p $OLD_CONFIG_PATH $WORKING_DIR/gsyncd.conf
TEST ! grep "sync-method" $WORKING_DIR/gsyncd.conf
TEST $GEOREP_CLI $primary $secondary start
TEST grep "sync-method" $WORKING_DIR/gsyncd.conf

#Stop geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Delete Geo-rep
TEST $GEOREP_CLI $primary $secondary delete

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
