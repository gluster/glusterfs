#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../nfs.rc

SCRIPT_TIMEOUT=500

#G_TESTDEF_TEST_STATUS_CENTOS6=NFS_TEST

function create_files() {
    local first="${1}"
    local last="${2}"
    local prefix="${3}"
    local gid
    local file

    for gid in $(seq ${first} ${last}); do
        file="${prefix}${gid}"
        if ! echo 'Hello World!' >"${file}"; then
            return 1
        fi
        if ! chgrp ${gid} "${file}"; then
            return 1
        fi
        if ! chmod 0640 "${file}"; then
            return 1
        fi
    done

    return 0
}

function access_files() {
    local first="${1}"
    local last="${2}"
    local prefix="${3}"
    local user="${4}"
    local gid
    local file
    local good
    local bad

    good="0"
    bad="0"
    for gid in $(seq ${first} ${last}); do
        file="${prefix}${gid}"
        if su -m ${user} -c "cat ${prefix}${gid} >/dev/null 2>&1"; then
            good="$((${good} + 1))"
        else
            bad="$((${bad} + 1))"
        fi
    done

    echo "${good} ${bad}"
}

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

# create one file for each group of the user
TEST create_files "${NEW_GID}" "${LAST_GID}" "${N0}/file."

# try to access all files as the user
RES=($(access_files "${NEW_GID}" "${LAST_GID}" "${N0}/file." "${NEW_USER}"))

TEST [[ ${RES[0]} -gt 0 ]]

case $OSTYPE in
Linux)  # Linux NFS fails with big GID
        if [ ${RES[1]} -ne 0 ] ; then
            res="Y"
        else
            res="N"
        fi
        ;;
*)      # Other systems should cope better
        if [ ${RES[1]} -eq 0 ] ; then
            res="Y"
        else
            res="N"
        fi
        ;;
esac
TEST [ "x$res" = "xY" ]

# enable server.manage-gids and things should work
TEST $CLI volume set $V0 server.manage-gids on

RES=($(access_files "${NEW_GID}" "${LAST_GID}" "${N0}/file." "${NEW_USER}"))

TEST [[ ${RES[0]} -gt 0 ]]
TEST [[ ${RES[1]} -eq 0 ]]

RES=($(access_files "${NEW_GID}" "${LAST_GID}" "${M0}/file." "${NEW_USER}"))

TEST [[ ${RES[0]} -gt 0 ]]
TEST [[ ${RES[1]} -eq 0 ]]

# cleanup
userdel --force ${NEW_USER}
for GID in $(seq ${NEW_GID} ${LAST_GID})
do
        groupdel ${NEW_USER}-${GID}
done

rm -f $N0/README
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
