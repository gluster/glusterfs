#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../fdl.rc

if [ x"$OSTYPE" = x"NetBSD" ]; then
        CREAT_OFLAG="creat,"
else
        CREAT_OFLAG=""
fi

TEST rm -f $FDL_META_FILE $FDL_DATA_FILE
TEST glusterd
TEST pidof glusterd

# Get a simple volume set up and mounted with FDL active.
TEST $CLI volume create $V0 ${H0}:${B0}/${V0}-0
TEST $CLI volume set $V0 changelog.changelog off
TEST $CLI volume set $V0 features.fdl on
TEST $CLI volume start $V0
TEST $GFS -s $H0 --volfile-id $V0 $M0

# Generate some I/O and unmount.
TEST mkdir -p $M0/abc/def
TEST dd if=/dev/zero of=$M0/abc/def/ghi bs=128 count=2 \
     oflag=${CREAT_OFLAG}sync
TEST chmod 314 $M0/abc/def/ghi
TEST rm -rf $M0/abc
TEST umount $M0

# Check that gf_logdump works, and shows the ops we just issued.  There will be
# more SETATTR ops than the one corresponding to our chmod, because some are
# issued internally.  We have to guess a bit about where the log will be.
TEST check_logfile GF_FOP_MKDIR 2
TEST check_logfile GF_FOP_CREATE 1
TEST check_logfile GF_FOP_WRITE 2
TEST check_logfile GF_FOP_SETATTR 1
TEST check_logfile GF_FOP_UNLINK 1
TEST check_logfile GF_FOP_RMDIR 2

cleanup
#G_TESTDEF_TEST_STATUS_CENTOS6=KNOWN_ISSUE,BUG=1385758
#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=1385758
