#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup

# prepare the users and groups
NEW_USER=bug1053579
NEW_UID=1053579
NEW_GID=1053579
LAST_GID=1053779
NEW_GIDS=${NEW_GID}

# OS-specific overrides
case $OSTYPE in
NetBSD|Darwin)
        # only NGROUPS_MAX=16 secondary groups are supported
        LAST_GID=1053593
        ;;
FreeBSD)
        # NGROUPS_MAX=1023 (FreeBSD>=8.0), we can afford 200 groups
        ;;
Linux)
        # NGROUPS_MAX=65536, we can afford 200 groups
        ;;
*)
        ;;
esac

# create a user that belongs to many groups
for GID in $(seq  -f '%6.0f' ${NEW_GID} ${LAST_GID})
do
        groupadd -o -g ${GID} ${NEW_USER}-${GID}
        NEW_GIDS="${NEW_GIDS},${NEW_USER}-${GID}"
done
TEST useradd -o -M -u ${NEW_UID} -g ${NEW_GID} -G ${NEW_USER}-${NEW_GIDS} ${NEW_USER}

# preparation done, start the tests

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1
TEST $CLI volume set $V0 nfs.disable false
TEST $CLI volume set $V0 nfs.server-aux-gids on
TEST $CLI volume start $V0

EXPECT_WITHIN $NFS_EXPORT_TIMEOUT "1" is_nfs_export_available

# mount the volume
TEST mount_nfs $H0:/$V0 $N0 nolock
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0

# the actual test, this used to crash
su -m ${NEW_USER} -c "stat $N0/. > /dev/null"
TEST [ $? -eq 0 ]

# create a file that only a user in a high-group can access
echo 'Hello World!' > $N0/README
chgrp ${LAST_GID} $N0/README
chmod 0640 $N0/README

#su -m ${NEW_USER} -c "cat $N0/README 2>&1 > /dev/null"
su -m ${NEW_USER} -c "cat $N0/README"
ret=$?

case $OSTYPE in
Linux)  # Linux NFS fails with big GID
        if [ $ret -ne 0 ] ; then
            res="Y"
        else
            res="N"
        fi
        ;;
*)      # Other systems should cope better
        if [ $ret -eq 0 ] ; then
            res="Y"
        else
            res="N"
        fi
        ;;
esac
TEST [ "x$res" = "xY" ]

# This passes only on build.gluster.org, not reproducible on other machines?!
#su -m ${NEW_USER}  -c "cat $M0/README 2>&1 > /dev/null"
#TEST [ $? -ne 0 ]

# enable server.manage-gids and things should work
TEST $CLI volume set $V0 server.manage-gids on

su -m ${NEW_USER} -c "cat $N0/README 2>&1 > /dev/null"
TEST [ $? -eq 0 ]
su -m ${NEW_USER} -c "cat $M0/README 2>&1 > /dev/null"
TEST [ $? -eq 0 ]

# cleanup
userdel --force ${NEW_USER}
for GID in $(seq  -f '%6.0f' ${NEW_GID} ${LAST_GID})
do
        groupdel ${NEW_USER}-${GID}
done

rm -f $N0/README
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
