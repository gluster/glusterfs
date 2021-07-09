#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST ${CLI} volume create ${V0} ${H0}:/$B0/${V0}_0
TEST ${CLI} volume set ${V0} readdir-ahead on
TEST ${CLI} volume set ${V0} parallel-readdir on
TEST ${CLI} volume start ${V0}

TEST ${GFS} --volfile-server ${H0} --volfile-id ${V0} ${M0}

TEST mkdir -p ${M0}/d/d.{000..999}

EXPECT_WITHIN ${UMOUNT_TIMEOUT} "Y" force_umount ${M0}

TEST ${CLI} volume add-brick ${V0} ${H0}:${B0}/${V0}_{1..7}

TEST ${GFS} --volfile-server ${H0} --volfile-id ${V0} ${M0}

ls -l ${M0}/d/ | wc -l

EXPECT_WITHIN ${UMOUNT_TIMEOUT} "Y" force_umount ${M0}
TEST ${GFS} --volfile-server ${H0} --volfile-id ${V0} ${M0}

ls -l ${M0}/d/ | wc -l

TEST ls ${M0}/d

cleanup
