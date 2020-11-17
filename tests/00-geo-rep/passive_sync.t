#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../geo-rep.rc

cleanup
TEST glusterd
TEST pidof glusterd

AREQUAL_PATH=$(dirname $0)/../utils
test "`uname -s`" != "Linux" && {
    CFLAGS="$CFLAGS -lintl";
}
build_tester $AREQUAL_PATH/arequal-checksum.c $CFLAGS

function volume_online_brick_count
{
        $CLI volume status $GMV0 | awk '$1 == "Brick" &&  $6 != "N/A" { print $6}' | wc -l;
}

##Variables
GEOREP_CLI="$CLI volume geo-replication"
master=$GMV0
SH0="127.0.0.1"
slave=${SH0}::${GSV0}
master_mnt=$M0
slave_mnt=$M1

##create_and_start_master_volume with self heal turned off
TEST $CLI volume create $GMV0 replica 3 $H0:$B0/${GMV0}{1,2,3};
TEST $CLI volume set $GMV0 cluster.self-heal-daemon off
TEST $CLI volume start $GMV0

##create_and_start_slave_volume
TEST $CLI volume create $GSV0 replica 3 $H0:$B0/${GSV0}{1,2,3};
TEST $CLI volume start $GSV0

##Create, start and mount meta_volume
TEST $CLI volume create $META_VOL replica 3 $H0:$B0/${META_VOL}{1,2,3};
TEST $CLI volume start $META_VOL
TEST mkdir -p $META_MNT
TEST glusterfs -s $H0 --volfile-id $META_VOL $META_MNT

##Mount master and slave and check health
TEST glusterfs -s $H0 --volfile-id $GMV0 $M0
TEST glusterfs -s $H0 --volfile-id $GSV0 $M1
TEST mkdir -p $M0/dir

#necessary changes for geo-rep session
TEST create_georep_session $master $slave
TEST $GEOREP_CLI $master $slave config gluster-command-dir ${GLUSTER_CMD_DIR}
TEST $GEOREP_CLI $master $slave config slave-gluster-command-dir ${GLUSTER_CMD_DIR}
TEST $GEOREP_CLI $master $slave config use_meta_volume true
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_common_secret_file
EXPECT_WITHIN $GEO_REP_TIMEOUT  0 check_keys_distributed

#Start_georep check if fine and stop it
TEST $GEOREP_CLI $master $slave start
TEST $CLI volume set $GMV0 changelog.rollover-time 1
EXPECT_WITHIN $GEO_REP_TIMEOUT  1 check_status_num_rows "Active"
EXPECT_WITHIN $GEO_REP_TIMEOUT  2 check_status_num_rows "Passive"
TEST $GEOREP_CLI $master $slave stop
EXPECT_WITHIN $GEO_REP_TIMEOUT  3 check_status_num_rows "Stopped"

#create files and kill a brick and create files to be healed.
for i in {1..10}; do echo "hey"  >> $M0/dir/file$i; done
TEST kill $(cat /var/run/gluster/vols/master/$H0-d-backends-master3.pid)
for i in {1..20}; do echo "bye"  >> $M0/dir/b3down$i; sleep 0.5; done

# bring all bricks up
TEST $CLI volume start $GMV0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" volume_online_brick_count

# start geo rep and make the brick3 active
TEST $GEOREP_CLI $master $slave start

#wait till b3 becomes active
EXPECT_WITHIN $GEO_REP_TIMEOUT 1 check_status_num_rows "Active"

# expect within is not enough if the master3 has become passive
# in this case we need to kill the active and try again.
i=0
until [ $i -gt "2" ]
do
        res=$(check_master3_active_status)
        if [ $res -eq 1 ]; then
                break
        fi
        #kill other two workers to make brick3 active
        worker1=$(ps aux | grep feedback | grep master1 | awk '{print $2}')
        worker2=$(ps aux | grep feedback | grep master2 | awk '{print $2}')
        worker3=$(ps aux | grep feedback | grep master3 | awk '{print $2}')
        kill $worker1
        kill $worker2

        EXPECT_WITHIN $GEO_REP_TIMEOUT 1 check_active_brick_status "master3"
        EXPECT_WITHIN $GEO_REP_TIMEOUT 1 check_status_num_rows "Active"
        EXPECT_WITHIN $GEO_REP_TIMEOUT 2 check_status_num_rows "Passive"
        i=$(($i+ 1))
        echo tries for making brick3 active: $i
done
$GEOREP_CLI $master $slave status

EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" volume_online_brick_count
for i in {1..20}; do echo "bye"  >> $M0/dir/allup$i; sleep 0.5; done
$GEOREP_CLI $master $slave status

TEST $CLI volume set $GMV0 cluster.self-heal-daemon on
sleep 3
i=0
until [ $i -gt "2" ]
do
        res=$(check_master3_passive_status)
        if [ $res -eq 1 ]; then
                break
        fi
        #kill other two workers to make brick3 active
        worker3=$(ps aux | grep feedback | grep master3 | awk '{print $2}')
        kill $worker3

        EXPECT_WITHIN $GEO_REP_TIMEOUT 1 check_passive_brick_status "master3"
        EXPECT_WITHIN $GEO_REP_TIMEOUT 1 check_status_num_rows "Active"
        EXPECT_WITHIN $GEO_REP_TIMEOUT 2 check_status_num_rows "Passive"
        i=$(($i + 1))
        echo tries for making brick3 active: $i
done

$GEOREP_CLI $master $slave status

for i in {1..20}; do echo "bye"  >> $M0/dir/b3passive$i; done

#Verify arequal for whole volume
EXPECT_WITHIN $GEO_REP_TIMEOUT "x0" arequal_checksum $M0/dir $M1/dir

#Stop_georep
TEST $GEOREP_CLI $master $slave stop

#Delete Geo-rep
TEST $GEOREP_CLI $master $slave delete

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M1

# Stop and delete the primary volume 
TEST $CLI volume stop $GMV0;
TEST $CLI volume delete $GMV0;

# Stop and delete the secndary volume
TEST $CLI volume stop $GSV0;
TEST $CLI volume delete $GSV0;

#Cleanup authorized keys
sed -i '/^command=.*SSH_ORIGINAL_COMMAND#.*/d' ~/.ssh/authorized_keys
sed -i '/^command=.*gsyncd.*/d' ~/.ssh/authorized_keys

cleanup
