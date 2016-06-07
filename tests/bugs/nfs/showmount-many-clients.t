#!/bin/bash
#
# The nfs.rpc-auth-allow volume option is used to generate the list of clients
# that are displayed as able to mount the export. The "group" in the export
# should be a list of all clients, identified by "name". In previous versions,
# the "name" was the copied string from nfs.rpc-auth-allow. This is not
# correct, as the volume option should be parsed and split into different
# groups.
#
# When the single string is passed, this testcase fails when the
# nfs.rpc-auth-allow volume option is longer than 256 characters. By splitting
# the groups into their own structures, this testcase passes.
#

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../nfs.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1
EXPECT 'Created' volinfo_field $V0 'Status'
TEST $CLI volume set $V0 nfs.disable false

CLIENTS=$(echo 127.0.0.{1..128} | tr ' ' ,)
TEST $CLI volume set $V0 nfs.rpc-auth-allow ${CLIENTS}
TEST $CLI volume set $V0 nfs.rpc-auth-reject all

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

# glusterfs/nfs needs some time to start up in the background
EXPECT_WITHIN $NFS_EXPORT_TIMEOUT 1 is_nfs_export_available

# showmount should not timeout (no reply is sent on error)
TEST showmount -e $H0

cleanup
