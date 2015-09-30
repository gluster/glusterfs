#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

function _init()
{
# Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

#Create a volume
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2};

#Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

#Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

#Enable Quota
TEST $CLI volume quota $V0 enable

##Wait for the auxiliary mount to comeup
sleep 3;
}

function get_aux()
{
##Check if a auxiliary mount is there
df -h | grep "/var/run/gluster/$V0" -

if [ $? -eq 0 ]
then
        echo "0"
else
        echo "1"
fi
}

function create_data()
{
#set some limit on the volume
TEST $CLI volume quota $V0 limit-usage / 50MB;

#Auxiliary mount should be there before stopping the volume
EXPECT "0"  get_aux;

TEST $CLI volume stop $V0;

#Aux mount should have been removed
EXPECT "1" get_aux;

}


_init;
create_data;
cleanup;
