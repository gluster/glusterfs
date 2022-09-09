#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd

TEST ${CLI} volume create ${V0} ${H0}:${B0}/brick

BRICK="${B0//\//-}-brick"
BRICK="${BRICK:1}"

VOLFILE_CLNT="$(gluster system:: getwd)/vols/${V0}/${V0}.tcp-fuse.vol"
VOLFILE_SRVR="$(gluster system:: getwd)/vols/${V0}/${V0}.${H0}.${BRICK}.vol"

EXPECT "^$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume set ${V0} send-gids false
EXPECT "^$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^false$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume set ${V0} send-gids true
EXPECT "^$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^true$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume reset ${V0} send-gids
EXPECT "^$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume set ${V0} manage-gids true
EXPECT "^true$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^false$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume set ${V0} manage-gids false
EXPECT "^false$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume set ${V0} send-gids false
EXPECT "^false$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^false$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

TEST gluster volume set ${V0} send-gids true
EXPECT "^false$" volgen_volume_option ${VOLFILE_SRVR} ${V0}-server protocol server manage-gids
EXPECT "^true$" volgen_volume_option ${VOLFILE_CLNT} ${V0}-client-0 protocol client send-gids

cleanup
