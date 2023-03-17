#!/bin/bash

. $(dirname ${0})/../../include.rc
. $(dirname ${0})/../../volume.rc

cleanup

TEST glusterd
TEST ${CLI} volume create ${V0} ${H0}:${B0}/${V0}
TEST ${CLI} volume start ${V0}
TEST ${GFS} -s ${H0} --volfile-id ${V0} ${M0}

TEST mkdir -p ${M0}/test/dir{1,2}
TEST touch ${M0}/test/dir{1,2}/file

gfid1="$(gf_get_gfid_backend_file_path ${B0}/${V0} /test/dir1)"
gfid2="$(gf_get_gfid_backend_file_path ${B0}/${V0} /test/dir2)"

TEST [[ -h "${gfid1}" ]]
EXPECT "${B0}/${V0}/test/dir1" realpath "${gfid1}"
TEST [[ -h "${gfid2}" ]]
EXPECT "${B0}/${V0}/test/dir2" realpath "${gfid2}"

TEST ! mv -T ${M0}/test/dir1 ${M0}/test/dir2

TEST [[ -h "${gfid1}" ]]
EXPECT "${B0}/${V0}/test/dir1" realpath "${gfid1}"
TEST [[ -h "${gfid2}" ]]
EXPECT "${B0}/${V0}/test/dir2" realpath "${gfid2}"

TEST rm -f ${M0}/test/dir2/file

TEST mv -T ${M0}/test/dir1 ${M0}/test/dir2

TEST [[ -h "${gfid1}" ]]
EXPECT "${B0}/${V0}/test/dir2" realpath "${gfid1}"
TEST [[ ! -e "${gfid2}" ]]

cleanup

