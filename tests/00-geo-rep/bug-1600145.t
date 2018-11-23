#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

### Basic Tests with Distribute Replicate volumes

##Cleanup and start glusterd
cleanup;
SCRIPT_TIMEOUT=600
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
TEST $CLI volume create $GMV0 replica 2 $H0:$B0/${GMV0}{1,2};
gluster v set all cluster.brick-multiplex on
TEST $CLI volume start $GMV0

##create_and_start_slave_volume
TEST $CLI volume create $GSV0 replica 2 $H0:$B0/${GSV0}{1,2};
TEST $CLI volume start $GSV0

##Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

############################################################
#BASIC GEO-REPLICATION TESTS
############################################################

#Create geo-rep session
TEST create_georep_session $master $slave

#Config gluster-command-dir
TEST $GEOREP_CLI $master $slave config gluster-command-dir ${GLUSTER_CMD_DIR}

#Config gluster-command-dir
TEST $GEOREP_CLI $master $slave config slave-gluster-command-dir ${GLUSTER_CMD_DIR}

#Enable_metavolume
TEST $GEOREP_CLI $master $slave config use_meta_volume true

#Wait for common secret pem file to be created
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file

#Verify the keys are distributed

EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Count no. of changelog socket
brick_pid=`ps -aef | grep glusterfsd | grep -v "shared_storage" | grep -v grep | awk -F " " '{print $2}'`
n=$(grep -Fc "changelog" /proc/$brick_pid/net/unix)

#Start_georep
TEST $GEOREP_CLI $master $slave start

EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Passive"

#Count no. of changelog socket
brick_pid=`ps -aef | grep glusterfsd | grep -v "shared_storage" | grep -v grep | awk -F " " '{print $2}'`
c=$(grep -Fc "changelog" /proc/$brick_pid/net/unix)
let expected=n+2
TEST [ "$c" -eq "$expected" ]

#Kill the "Active" brick
brick=$($GEOREP_CLI $master $slave status | grep -F "Active" | awk {'print $3'})
cat /proc/$brick_pid/net/unix | grep "changelog"
TEST kill_brick $GMV0 $H0 $brick
#Expect geo-rep status to be "Faulty"
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Faulty"
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"

#Count no. of changelog socket
brick_pid=`ps -aef | grep glusterfsd | grep -v "shared_storage" | grep -v grep | awk -F " " '{print $2}'`
cat /proc/$brick_pid/net/unix | grep "changelog"
ls -lrth /proc/$brick_pid/fd | grep "socket"
c=$(grep -Fc "changelog" /proc/$brick_pid/net/unix)
TEST [ "$c" -eq "$n" ]

#Stop Geo-rep
TEST $GEOREP_CLI $master $slave stop

#Delete Geo-rep
TEST $GEOREP_CLI $master $slave delete

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
