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
master=$GMV0
SH0="127.0.0.1"
slave=${SH0}::${GSV0}
num_active=2
num_passive=2
master_mnt=$M0
slave_mnt=$M1

############################################################
#SETUP VOLUMES AND GEO-REPLICATION
############################################################

##create_and_start_master_volume
TEST $CLI volume create $GMV0 replica 2 $H0:$B0/${GMV0}{1,2,3,4};
TEST $CLI volume start $GMV0

##create_and_start_slave_volume
TEST $CLI volume create $GSV0 replica 2 $H0:$B0/${GSV0}{1,2,3,4};
TEST $CLI volume start $GSV0
TEST $CLI volume set $GSV0 performance.stat-prefetch off
TEST $CLI volume set $GSV0 performance.quick-read off
TEST $CLI volume set $GSV0 performance.readdir-ahead off
TEST $CLI volume set $GSV0 performance.read-ahead off

##Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

##Mount master
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

##Mount slave
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1

############################################################
#BASIC GEO-REPLICATION TESTS
############################################################

#Check Hybrid Crawl
TEST create_data "hybrid"
TEST create_georep_session $master $slave
EXPECT_WITHIN $GEO_REP_TIMEOUT 4 check_status_num_rows "Created"

#Config gluster-command-dir
TEST $GEOREP_CLI $master $slave config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $master $slave config slave-gluster-command-dir ${GLUSTER_CMD_DIR}

#Enable_metavolume
TEST $GEOREP_CLI $master $slave config use_meta_volume true

#Set changelog roll-over time to 3 secs
TEST $CLI volume set $GMV0 changelog.rollover-time 3

#Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Verify the keys are distributed
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Start_georep
TEST $GEOREP_CLI $master $slave start

EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Passive"

#data_tests "hybrid"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 regular_file_ok ${slave_mnt}/hybrid_f1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/hybrid_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_file_ok ${slave_mnt}/hybrid_f3 ${slave_mnt}/hybrid_f4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_dir_ok ${slave_mnt}/hybrid_d3 ${slave_mnt}/hybrid_d4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 symlink_ok hybrid_f1 ${slave_mnt}/hybrid_sl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_file_ok ${slave_mnt}/hybrid_f1 ${slave_mnt}/hybrid_hl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${slave_mnt}/hybrid_f2
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${slave_mnt}/hybrid_d2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 data_ok ${slave_mnt}/hybrid_f1 "HelloWorld!"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 chown_file_ok ${slave_mnt}/hybrid_chown_f1

#Check History Crawl.
TEST $GEOREP_CLI $master $slave stop
TEST create_data "history"
TEST $GEOREP_CLI $master $slave start
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Passive"

#data_tests "history"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 regular_file_ok ${slave_mnt}/history_f1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/history_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_file_ok ${slave_mnt}/history_f3 ${slave_mnt}/history_f4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_dir_ok ${slave_mnt}/history_d3 ${slave_mnt}/history_d4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 symlink_ok history_f1 ${slave_mnt}/history_sl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_file_ok ${slave_mnt}/history_f1 ${slave_mnt}/history_hl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${slave_mnt}/history_f2
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${slave_mnt}/history_d2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 data_ok ${slave_mnt}/history_f1 "HelloWorld!"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 chown_file_ok ${slave_mnt}/history_chown_f1

#Check Changelog Crawl.
EXPECT_WITHIN $GEO_REP_TIMEOUT 2 check_status_num_rows "Changelog Crawl"
TEST create_data "changelog"

# logrotate test
logrotate_file=${master_mnt}/logrotate/lg_test_file
TEST mkdir -p ${master_mnt}/logrotate
logrotate_simulate $logrotate_file 2
logrotate_simulate $logrotate_file 2
logrotate_simulate $logrotate_file 2
logrotate_simulate $logrotate_file 2

# CREATE + RENAME
create_rename ${master_mnt}/rename_test_file

# hard-link rename
hardlink_rename ${master_mnt}/hardlink_rename_test_file

#SYNC CHECK
#data_tests "changelog"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 regular_file_ok ${slave_mnt}/changelog_f1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/changelog_d1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_file_ok ${slave_mnt}/changelog_f3 ${slave_mnt}/changelog_f4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 rename_dir_ok ${slave_mnt}/changelog_d3 ${slave_mnt}/changelog_d4
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 symlink_ok changelog_f1 ${slave_mnt}/changelog_sl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_file_ok ${slave_mnt}/changelog_f1 ${slave_mnt}/changelog_hl1
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${slave_mnt}/changelog_f2
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 unlink_ok ${slave_mnt}/changelog_d2
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 data_ok ${slave_mnt}/changelog_f1 "HelloWorld!"
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 chown_file_ok ${slave_mnt}/changelog_chown_f1

#logrotate
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 directory_ok ${slave_mnt}/logrotate
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 arequal_checksum ${master_mnt}/logrotate ${slave_mnt}/logrotate

#CREATE+RENAME
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 create_rename_ok ${slave_mnt}/create_rename_test_file

#hardlink rename
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_rename_ok ${slave_mnt}/hardlink_rename_test_file

#Stop Geo-rep
TEST $GEOREP_CLI $master $slave stop

#Symlink testcase: Rename symlink and create dir with same name
TEST create_symlink_rename_mkdir_data

#hardlink-rename-unlink usecase. Sonatype Nexus3 Usecase. BUG:1512483
TEST create_hardlink_rename_data

#rsnapshot usecase
TEST create_rsnapshot_data

#Start Geo-rep
TEST $GEOREP_CLI $master $slave start

#Wait for geo-rep to come up
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Passive"

#Check for hardlink rename case. BUG: 1296174
#It should not create src file again on changelog reprocessing
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 hardlink_rename_ok ${slave_mnt}/hardlink_rename_test_file

#Symlink testcase: Rename symlink and create dir with same name
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_symlink_rename_mkdir_data ${slave_mnt}/symlink_test1

#hardlink-rename-unlink usecase. Sonatype Nexus3 Usecase. BUG:1512483
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_hardlink_rename_data ${slave_mnt}

#rsnapshot usecase
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 verify_rsnapshot_data ${slave_mnt}

#Test rsync-options set BUG:1629561
TEST gluster volume geo-rep $master $slave config rsync-options "--whole-file"
TEST "echo sampledata > $master_mnt/rsync_option_test_file"

#Verify arequal for whole volume
EXPECT_WITHIN $GEO_REP_TIMEOUT 0 arequal_checksum ${master_mnt} ${slave_mnt}

#Stop Geo-rep
TEST $GEOREP_CLI $master $slave stop

#Delete Geo-rep
TEST $GEOREP_CLI $master $slave delete

#Cleanup are-equal binary
TEST rm $AREQUAL_PATH/arequal-checksum

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
