#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup

function _init() {
# Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

# Lets create volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};

## Verify volume is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

#Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

#Enable Quota
TEST $CLI volume quota $V0 enable

#As quotad consumes some time to connect to brick process we invoke sleep
sleep 10;

#set limit of 1GB of quota on root
TEST $CLI volume quota $V0 limit-usage / 1GB
}

function get_hardlimit()
{
        VOLUME=$1

        $CLI volume quota $VOLUME list | tail -1 | sed "s/ \{1,\}/ /g" |
                                                cut -d' ' -f 2
}

function check_fattrs {

touch $M0/file1;

#This confirms that pgfid is also filtered
TEST ! "getfattr -d -e hex -m . $M0/file1 | grep pgfid ";

#just check for quota xattr are visible or not
TEST ! "getfattr -d -e hex -m . $M0 | grep quota";

#setfattr should fail
TEST ! setfattr -n trusted.glusterfs.quota.limit-set -v 10 $M0;

#remove xattr should fail
TEST ! setfattr -x trusted.glusterfs.quota.limit-set $M0;

#check if list command still shows the correct value or not

EXPECT "1.0GB" get_hardlimit $V0

}

_init;
check_fattrs;
cleanup




