#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This script checks if use-readdirp option works as accepted in mount options

function get_use_readdirp_value {
        local vol=$1
        local statedump=$(generate_mount_statedump $vol)
        sleep 1
        local val=$(grep "use_readdirp=" $statedump | cut -f2 -d'=' | tail -1)
        rm -f $statedump
        echo $val
}
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}
TEST $CLI volume start $V0
#If readdirp is enabled statedump should reflect it
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --use-readdirp=yes
TEST cd $M0
EXPECT "1" get_use_readdirp_value $V0
TEST cd -
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#If readdirp is enabled statedump should reflect it
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --use-readdirp=no
TEST cd $M0
EXPECT "0" get_use_readdirp_value $V0
TEST cd -
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Since args are optional on this argument just specifying "--use-readdirp" should also turn it `on` not `off`
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --use-readdirp
TEST cd $M0
EXPECT "1" get_use_readdirp_value $V0
TEST cd -
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#By default it is enabled.
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST cd $M0
EXPECT "1" get_use_readdirp_value $V0
TEST cd -
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

#Invalid values for use-readdirp should not be accepted
TEST ! glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --use-readdirp=please-fail

cleanup
