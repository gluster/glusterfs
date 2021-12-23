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

### Basic Tests with Distributed Disperse volumes

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
num_passive=10
primary_mnt=$M0
secondary_mnt=$M1

############################################################
#SETUP VOLUMES AND GEO-REPLICATION
############################################################

##create_and_start_primary_volume
TEST $CLI volume create $GMV0 disperse 3 redundancy 1 $H0:$B0/${GMV0}{0..5};
TEST $CLI volume start $GMV0

##create_and_start_secondary_volume
TEST $CLI volume create $GSV0 disperse 3 redundancy 1 $H0:$B0/${GSV0}{0..5};
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

#Check Hybrid Crawl
TEST create_data "hybrid"
TEST create_georep_session $primary $secondary
EXPECT_WITHIN $GEO_REP_TIMEOUT 6 check_status_num_rows "Created"

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $primary $secondary config secondary-gluster-command-dir ${GLUSTER_CMD_DIR}

#Enable_metavolume
TEST $GEOREP_CLI $primary $secondary config use_meta_volume true

#Set changelog roll-over time to 3 secs
TEST $CLI volume set $GMV0 changelog.rollover-time 3

#Config tarssh as sync-engine
TEST $GEOREP_CLI $primary $secondary config sync-method tarssh

#Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Verify the keys are distributed
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Verify "features.read-only" Option
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 check_secondary_read_only $GSV0

#Start_georep
TEST $GEOREP_CLI $primary $secondary start

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_status_num_rows "Passive"

#data_tests "hybrid"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 regular_file_ok ${secondary_mnt}/hybrid_f1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${secondary_mnt}/hybrid_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_file_ok ${secondary_mnt}/hybrid_f3 ${secondary_mnt}/hybrid_f4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_dir_ok ${secondary_mnt}/hybrid_d3 ${secondary_mnt}/hybrid_d4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 symlink_ok hybrid_f1 ${secondary_mnt}/hybrid_sl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_file_ok ${secondary_mnt}/hybrid_f1 ${secondary_mnt}/hybrid_hl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${secondary_mnt}/hybrid_f2
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${secondary_mnt}/hybrid_d2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 data_ok ${secondary_mnt}/hybrid_f1 "HelloWorld!"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 chown_file_ok ${secondary_mnt}/hybrid_chown_f1

#Check History Crawl.
TEST $GEOREP_CLI $primary $secondary stop
TEST create_data "history"
TEST $GEOREP_CLI $primary $secondary start
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_status_num_rows "Passive"

#data_tests "history"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 regular_file_ok ${secondary_mnt}/history_f1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${secondary_mnt}/history_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_file_ok ${secondary_mnt}/history_f3 ${secondary_mnt}/history_f4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_dir_ok ${secondary_mnt}/history_d3 ${secondary_mnt}/history_d4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 symlink_ok history_f1 ${secondary_mnt}/history_sl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_file_ok ${secondary_mnt}/history_f1 ${secondary_mnt}/history_hl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${secondary_mnt}/history_f2
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${secondary_mnt}/history_d2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 data_ok ${secondary_mnt}/history_f1 "HelloWorld!"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 chown_file_ok ${secondary_mnt}/history_chown_f1

#Check Changelog Crawl.
EXPECT_WITHIN $GEO_REP_TIMEOUT 2 check_status_num_rows "Changelog Crawl"
TEST create_data "changelog"

# logrotate test
logrotate_file=${primary_mnt}/logrotate/lg_test_file
TEST mkdir -p ${primary_mnt}/logrotate
logrotate_simulate $logrotate_file 2
logrotate_simulate $logrotate_file 2
logrotate_simulate $logrotate_file 2
logrotate_simulate $logrotate_file 2

# CREATE + RENAME
create_rename ${primary_mnt}/rename_test_file

# hard-link rename
hardlink_rename ${primary_mnt}/hardlink_rename_test_file

#SYNC CHECK
#data_tests "changelog"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 regular_file_ok ${secondary_mnt}/changelog_f1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${secondary_mnt}/changelog_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_file_ok ${secondary_mnt}/changelog_f3 ${secondary_mnt}/changelog_f4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_dir_ok ${secondary_mnt}/changelog_d3 ${secondary_mnt}/changelog_d4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 symlink_ok changelog_f1 ${secondary_mnt}/changelog_sl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_file_ok ${secondary_mnt}/changelog_f1 ${secondary_mnt}/changelog_hl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${secondary_mnt}/changelog_f2
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${secondary_mnt}/changelog_d2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 data_ok ${secondary_mnt}/changelog_f1 "HelloWorld!"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 chown_file_ok ${secondary_mnt}/changelog_chown_f1

#logrotate
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${secondary_mnt}/logrotate
EXPECT_WITHIN $GEO_REP_TIMEOUT "x0" arequal_checksum ${primary_mnt}/logrotate ${secondary_mnt}/logrotate

#CREATE+RENAME
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 create_rename_ok ${secondary_mnt}/create_rename_test_file

#hardlink rename
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_rename_ok ${secondary_mnt}/hardlink_rename_test_file

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Symlink testcase: Rename symlink and create dir with same name
TEST create_symlink_rename_mkdir_data

#hardlink-rename-unlink usecase. Sonatype Nexus3 Usecase. BUG:1512483
TEST create_hardlink_rename_data

#rsnapshot usecase
#TEST create_rsnapshot_data

#Start Geo-rep
TEST $GEOREP_CLI $primary $secondary start

#Wait for geo-rep to come up
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  4 check_status_num_rows "Passive"

#Check for hardlink rename case. BUG: 1296174
#It should not create src file again on changelog reprocessing
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_rename_ok ${secondary_mnt}/hardlink_rename_test_file

#Symlink testcase: Rename symlink and create dir with same name
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_symlink_rename_mkdir_data ${secondary_mnt}/symlink_test1

#hardlink-rename-unlink usecase. Sonatype Nexus3 Usecase. BUG:1512483
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_hardlink_rename_data ${secondary_mnt}

#rsnapshot usecase
#EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_rsnapshot_data ${secondary_mnt}

#rename with existing destination case BUG:1694820
#TEST create_rename_with_existing_destination ${primary_mnt}
#verify rename with existing destination case BUG:1694820
#EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_rename_with_existing_destination ${secondary_mnt}

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
