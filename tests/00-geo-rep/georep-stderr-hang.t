#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=500

AREQUAL_PATH=$(dirname $0)/../utils
test "`uname -s`" != "Linux" && {
    CFLAGS="$CFLAGS -lintl";
}
build_tester $AREQUAL_PATH/arequal-checksum.c $CFLAGS

### Basic Tests with Distribute Replicate volumes

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
TEST $CLI volume create $GMV0 $H0:$B0/${GMV0}1;
TEST $CLI volume start $GMV0

##create_and_start_secondary_volume
TEST $CLI volume create $GSV0 $H0:$B0/${GSV0}1;
TEST $CLI volume start $GSV0
TEST $CLI volume set $GSV0 performance.stat-prefetch off
TEST $CLI volume set $GSV0 performance.quick-read off
TEST $CLI volume set $GSV0 performance.readdir-ahead off
TEST $CLI volume set $GSV0 performance.read-ahead off

##Mount primary
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

##Mount secondary
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1

############################################################
#BASIC GEO-REPLICATION TESTS
############################################################

TEST create_georep_session $primary $secondary
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 check_status_num_rows "Created"

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config secondary-gluster-command-dir ${GLUSTER_CMD_DIR}

#Set changelog roll-over time to 45 secs
TEST $CLI volume set $GMV0 changelog.rollover-time 45

#Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Verify the keys are distributed
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Set sync-jobs to 1
TEST $GEOREP_CLI $primary $secondary config sync-jobs 1

#Start_georep
TEST $GEOREP_CLI $primary $secondary start

touch $M0
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Changelog Crawl"

#Check History Crawl.
TEST $GEOREP_CLI $primary $secondary stop
TEST create_data_hang "rsync_hang"
TEST create_data "history_rsync"
TEST $GEOREP_CLI $primary $secondary start
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"

#Verify arequal for whole volume
EXPECT_WITHIN $GEO_REP_TIMEOUT "x0" arequal_checksum ${primary_mnt} ${secondary_mnt}

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Config tarssh as sync-engine
TEST $GEOREP_CLI $primary $secondary config sync-method tarssh

#Create tarssh hang data
TEST create_data_hang "tarssh_hang"
TEST create_data "history_tar"

TEST $GEOREP_CLI $primary $secondary start
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"

#Verify arequal for whole volume
EXPECT_WITHIN $GEO_REP_TIMEOUT "x0" arequal_checksum ${primary_mnt} ${secondary_mnt}

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Delete Geo-rep
TEST $GEOREP_CLI $primary $secondary delete

#Cleanup are-equal binary
TEST rm $AREQUAL_PATH/arequal-checksum

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
