#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc
. $(dirname $0)/../env.rc

SCRIPT_TIMEOUT=300

##Cleanup and start glusterd
cleanup;
TEST glusterd;
TEST pidof glusterd


##Variables
GEOREP_CLI="gluster volume geo-replication"
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

##Mount primary
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0

##Mount secondary
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1

#Create geo-rep session
TEST create_georep_session $primary $secondary

echo n | $GEOREP_CLI $primary $secondary config ignore-deletes true >/dev/null 2>&1
EXPECT "false" echo $($GEOREP_CLI $primary $secondary config ignore-deletes)
echo y | $GEOREP_CLI $primary $secondary config ignore-deletes true
EXPECT "true" echo $($GEOREP_CLI $primary $secondary config ignore-deletes)

#Stop Geo-rep
TEST $GEOREP_CLI $primary $secondary stop

#Delete Geo-rep
TEST $GEOREP_CLI $primary $secondary delete

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
