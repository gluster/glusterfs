#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc

cleanup;

>/var/log/glusterfs/nfs.log;


function check_connection_log ()
{
        if grep "$1" /var/log/glusterfs/nfs.log &> /dev/null; then
                echo "Y" 
        else
                echo "N"
        fi;
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume start $V0
TEST $CLI volume set $V0 nfs.client-max-idle-seconds 6;

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available;

TEST mount -overs=3,noac,noacl,noatime,nolock,timeo=200 $HOSTNAME:/$V0 $N0

EXPECT_WITHIN 25 "Y" check_connection_log "Found idle client connection";

TEST $CLI volume set $V0 nfs.close-idle-clients on

EXPECT_WITHIN 25 "Y" check_connection_log "Shutting down idle client connection";

cleanup;
