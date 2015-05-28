#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc

#################################################
# BASIC TESTS WITH DISTRIBUTE REPLICATE VOLUMES
#################################################

##Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd


##Variables
GEOREP_CLI="$CLI volume geo-replication"
master=$GMV0
slave=${H0}::${GSV0}
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
TEST $CLI volume create $GSV0 replica 2 $H0:$B0/${GSV0}{1,2,3,4};   #5
TEST $CLI volume start $GSV0

##Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT     #10

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
EXPECT 4 check_status_num_rows "Created"             #15

#Enable_metavolume
TEST $GEOREP_CLI $master $slave config use_meta_volume true

#Config tarssh as sync-engine
TEST $GEOREP_CLI $master $slave config use_tarssh true

#Start_georep
TEST $GEOREP_CLI $master $slave start

sleep 10
EXPECT 2 check_status_num_rows "Active"
EXPECT 2 check_status_num_rows "Passive"            #20
TEST data_tests "hybrid"

#Check History Crawl.
TEST $GEOREP_CLI $master $slave stop
TEST create_data "history"
TEST $GEOREP_CLI $master $slave start
sleep 10
EXPECT 2 check_status_num_rows "Active"            #25
EXPECT 2 check_status_num_rows "Passive"
TEST data_tests "history"

#Check History Crawl.
TEST create_data "changelog"
sleep 15
TEST check_status "Changelog Crawl"
TEST data_tests "changelog"                         #30

#Stop Geo-rep
TEST $GEOREP_CLI $master $slave stop

#Delete Geo-rep
TEST $GEOREP_CLI $master $slave delete

cleanup;
