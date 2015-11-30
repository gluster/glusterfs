#!/bin/bash

#
# https://bugzilla.redhat.com/show_bug.cgi?id=1283103
#
# Test that it is possible to set and get security.*
# xattrs other thatn security.selinux irrespective of
# whether the mount was done with --selinux. This is
# for example important for Samba to be able to store
# the Windows-level acls in the security.NTACL xattr
# when the acl_xattr vfs module is used.
#

. $(dirname $0)/../../include.rc

cleanup;

#Basic checks
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

#Create a distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1..2};
TEST $CLI volume start $V0

# Mount FUSE without selinux:
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TESTFILE="$M0/testfile"
TEST touch ${TESTFILE}

TEST echo "setfattr -n security.foobar -v value ${TESTFILE}"
TEST setfattr -n security.foobar -v value ${TESTFILE}
TEST getfattr -n security.foobar ${TESTFILE}
TEST setfattr -x security.foobar ${TESTFILE}

# can not currently test the security.selinux xattrs
# since the kernel intercepts them.
# see https://bugzilla.redhat.com/show_bug.cgi?id=1272868
#TEST ! getfattr -n security.selinux ${TESTFILE}
#TEST ! setfattr -n security.selinux -v value ${TESTFILE}

TEST umount $M0

# Mount FUSE with selinux:
TEST glusterfs -s $H0 --volfile-id $V0 --selinux $M0

TEST setfattr -n security.foobar -v value ${TESTFILE}
TEST getfattr -n security.foobar ${TESTFILE}
TEST setfattr -x security.foobar ${TESTFILE}

# can not currently test the security.selinux xattrs
# since the kernel intercepts them.
# see https://bugzilla.redhat.com/show_bug.cgi?id=1272868
#TEST setfattr -n security.selinux -v value ${TESTFILE}
#TEST getfattr -n security.selinux ${TESTFILE}

cleanup;
