#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

cleanup

# prepare the users and groups
NEW_USER=bug1053579
NEW_UID=1053579
NEW_GID=1053579

# create many groups, $NEW_USER will have 200 groups
NEW_GIDS=1053580
groupadd -o -g ${NEW_GID} gid${NEW_GID} 2> /dev/null
for G in $(seq 1053581 1053279)
do
        groupadd -o -g ${G} gid${G} 2> /dev/null
        NEW_GIDS="${GIDS},${G}"
done

# create a user that belongs to many groups
groupadd -o -g ${NEW_GID} gid${NEW_GID}
useradd -o -u ${NEW_UID} -g ${NEW_GID} -G ${NEW_GIDS} ${NEW_USER}

# preparation done, start the tests

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 nfs.server-aux-gids on
TEST $CLI volume start $V0

EXPECT_WITHIN 20 "1" is_nfs_export_available

# Mount volume as NFS export
TEST mount -t nfs -o vers=3,nolock $H0:/$V0 $N0

# the actual test :-)
TEST su -c '"stat /mnt/. > /dev/null"' ${USER}

TEST umount $N0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
