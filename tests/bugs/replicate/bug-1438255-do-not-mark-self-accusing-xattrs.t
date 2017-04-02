#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

NEW_USER=bug1438255
NEW_UID=1438255
NEW_GID=1438255

TEST groupadd -o -g ${NEW_GID} ${NEW_USER}-${NEW_GID}
TEST useradd -o -M -u ${NEW_UID} -g ${NEW_GID} -K MAIL_DIR=/dev/null ${NEW_USER}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

TEST touch $M0/FILE
TEST kill_brick $V0 $H0 $B0/${V0}2
chown $NEW_UID:$NEW_GID $M0/FILE
EXPECT "000000000000000100000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}0/FILE
EXPECT "000000000000000100000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}1/FILE
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

# setfattr done as NEW_USER fails on 3rd brick with EPERM but suceeds on
# the first 2 and hence on the mount.
su -m bug1438255 -c "setfattr -n user.myattr -v myvalue  $M0/FILE"
TEST [ $? -eq 0 ]
EXPECT "000000000000000200000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}0/FILE
EXPECT "000000000000000200000000" get_hex_xattr trusted.afr.$V0-client-2 $B0/${V0}1/FILE
# Brick 3 does not have any self-blaming pending xattr.
TEST ! getfattr -n trusted.afr.$V0-client-2 $B0/${V0}2/FILE

TEST userdel --force ${NEW_USER}
TEST groupdel ${NEW_USER}-${NEW_GID}
cleanup


