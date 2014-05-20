#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

cleanup

# prepare the users and groups
NEW_USER=bug1053579
NEW_UID=1053579
NEW_GID=1053579
LAST_GID=1053779
NEW_GIDS=${NEW_GID}

# create a user that belongs to many groups
for GID in $(seq ${NEW_GID} ${LAST_GID})
do
        groupadd -o -g ${GID} ${NEW_USER}-${GID}
        NEW_GIDS="${NEW_GIDS},${NEW_USER}-${GID}"
done
TEST useradd -o -M -u ${NEW_UID} -g ${NEW_GID} -G ${NEW_USER}-${NEW_GIDS} ${NEW_USER}

# preparation done, start the tests

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 nfs.server-aux-gids on
TEST $CLI volume start $V0

EXPECT_WITHIN 20 "1" is_nfs_export_available

# mount the volume
TEST mount -t nfs -o vers=3,nolock $H0:/$V0 $N0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

# the actual test, this used to crash
su -c "stat $N0/. > /dev/null" ${NEW_USER}
TEST [ $? -eq 0 ]

# create a file that only a user in a high-group can access
echo 'Hello World!' > $N0/README
chgrp ${LAST_GID} $N0/README
chmod 0640 $N0/README

su -c "cat $N0/README 2>&1 > /dev/null" ${NEW_USER}
TEST [ $? -ne 0 ]
# This passes only on build.gluster.org, not reproducible on other machines?!
#su -c "cat $M0/README 2>&1 > /dev/null" ${NEW_USER}
#TEST [ $? -ne 0 ]

# we need to unmount before we can enable the server.manage-gids option
TEST umount $M0

# enable server.manage-gids and things should work
TEST $CLI volume set $V0 server.manage-gids on

# mount the volume again
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

su -c "cat $N0/README 2>&1 > /dev/null" ${NEW_USER}
TEST [ $? -eq 0 ]
su -c "cat $M0/README 2>&1 > /dev/null" ${NEW_USER}
TEST [ $? -eq 0 ]

# cleanup
userdel --force ${NEW_USER}
for GID in $(seq ${NEW_GID} ${LAST_GID})
do
        groupdel ${NEW_USER}-${GID}
done

rm -f $N0/README
TEST umount $N0
TEST umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
