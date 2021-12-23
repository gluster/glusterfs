#!/bin/bash

. $(dirname "${0}")/../../include.rc
. $(dirname "${0}")/../../volume.rc

cleanup;
TEST gcc $(dirname "${0}")/issue-2232.c -o $(dirname "${0}")/issue-2232 -lgfapi
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create ${V0} replica 3 ${H0}:${B0}/${V0}{0..2}

# Create a fake .glusterfs-anonymous-inode-... entry
ANONINO=".glusterfs-anonymous-inode-aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
TEST mkdir ${B0}/${V0}{0..2}/${ANONINO}
gfid="$(uuidgen)"
hex="0x$(echo "${gfid}" | tr -d '-')"
TEST assign_gfid "${hex}" "${B0}/${V0}0/${ANONINO}"
TEST assign_gfid "${hex}" "${B0}/${V0}1/${ANONINO}"
TEST assign_gfid "${hex}" "${B0}/${V0}2/${ANONINO}"
TEST mkdir -p "${B0}/${V0}0/.glusterfs/${gfid:0:2}/${gfid:2:2}"
TEST mkdir -p "${B0}/${V0}1/.glusterfs/${gfid:0:2}/${gfid:2:2}"
TEST mkdir -p "${B0}/${V0}2/.glusterfs/${gfid:0:2}/${gfid:2:2}"
TEST ln -s "../../00/00/00000000-0000-0000-0000-000000000001/${ANONINO}" "${B0}/${V0}0/.glusterfs/${gfid:0:2}/${gfid:2:2}/${gfid}"
TEST ln -s "../../00/00/00000000-0000-0000-0000-000000000001/${ANONINO}" "${B0}/${V0}1/.glusterfs/${gfid:0:2}/${gfid:2:2}/${gfid}"
TEST ln -s "../../00/00/00000000-0000-0000-0000-000000000001/${ANONINO}" "${B0}/${V0}2/.glusterfs/${gfid:0:2}/${gfid:2:2}/${gfid}"

TEST $CLI volume start ${V0}

TEST $(dirname "${0}")/issue-2232 ${H0} ${V0}

TEST rm -f $(dirname $0)/issue-2232

cleanup
