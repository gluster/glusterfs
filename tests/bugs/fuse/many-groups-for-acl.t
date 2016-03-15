#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

cleanup

# prepare the users and groups
NEW_USER=bug1246275
NEW_UID=1246275
NEW_GID=1246275
LAST_GID=1246403
NEW_GIDS=${NEW_GID}

# OS-specific overrides
case $OSTYPE in
NetBSD|Darwin)
        # no ACLs, and only NGROUPS_MAX=16 secondary groups are supported
        SKIP_TESTS
        exit 0
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
TEST $CLI volume create ${V0} ${H0}:${B0}/${V0}1
TEST $CLI volume set $V0 nfs.disable off
# disable manage-gids on the server-side for now, gets enabled later
TEST $CLI volume set ${V0} server.manage-gids off
TEST $CLI volume start ${V0}

EXPECT_WITHIN ${NFS_EXPORT_TIMEOUT} "1" is_nfs_export_available

# mount the volume with POSIX ACL support, without --resolve-gids
TEST glusterfs --acl --volfile-id=/${V0} --volfile-server=${H0} ${M0}

# create some directories for testing
TEST mkdir ${M0}/first-32-gids-1
TEST setfacl -m g:${NEW_UID}:rwx ${M0}/first-32-gids-1
TEST mkdir ${M0}/first-32-gids-2
TEST setfacl -m g:$[NEW_UID+16]:rwx ${M0}/first-32-gids-2
TEST mkdir ${M0}/gid-64
TEST setfacl -m g:$[NEW_UID+64]:rwx ${M0}/gid-64
TEST mkdir ${M0}/gid-120
TEST setfacl -m g:$[NEW_UID+120]:rwx ${M0}/gid-120

su -m ${NEW_USER} -c "touch ${M0}/first-32-gids-1/success > /dev/null"
TEST [ $? -eq 0 ]

su -m ${NEW_USER} -c "touch ${M0}/first-32-gids-2/success > /dev/null"
TEST [ $? -eq 0 ]

su -m ${NEW_USER} -c "touch ${M0}/gid-64/failure > /dev/null"
TEST [ $? -ne 0 ]

su -m ${NEW_USER} -c "touch ${M0}/gid-120/failure > /dev/null"
TEST [ $? -ne 0 ]

# unmount and remount with --resolve-gids
EXPECT_WITHIN ${UMOUNT_TIMEOUT} "Y" force_umount ${M0}
TEST glusterfs --acl --resolve-gids --volfile-id=/${V0} --volfile-server=${H0} ${M0}

su -m ${NEW_USER} -c "touch ${M0}/gid-64/success > /dev/null"
TEST [ $? -eq 0 ]

su -m ${NEW_USER} -c "touch ${M0}/gid-120/failure > /dev/null"
TEST [ $? -ne 0 ]

# enable server-side resolving of the groups
# stopping and starting is not really needed, but it prevents races
TEST $CLI volume stop ${V0}
TEST $CLI volume set ${V0} server.manage-gids on
TEST $CLI volume start ${V0}
EXPECT_WITHIN ${NFS_EXPORT_TIMEOUT} "1" is_nfs_export_available

# unmount and remount to prevent more race conditions on test systems
EXPECT_WITHIN ${UMOUNT_TIMEOUT} "Y" force_umount ${M0}
TEST glusterfs --acl --resolve-gids --volfile-id=/${V0} --volfile-server=${H0} ${M0}

su -m ${NEW_USER} -c "touch ${M0}/gid-120/success > /dev/null"
TEST [ $? -eq 0 ]

# cleanup
userdel --force ${NEW_USER}
for GID in $(seq  -f '%6.0f' ${NEW_GID} ${LAST_GID})
do
        groupdel ${NEW_USER}-${GID}
done

EXPECT_WITHIN ${UMOUNT_TIMEOUT} "Y" force_umount ${M0}

TEST ${CLI} volume stop ${V0}
TEST ${CLI} volume delete ${V0}

cleanup
