#!/bin/bash
#
# A failing glfs_*at() call (ENOTDIR, or renameat() of a missing source) must
# leave the caller's parent directory handle intact. Regression guard for the
# double GF_REF_PUT of the parent glfd in the glfs_*at() family, which freed
# the handle under the caller and crashed Samba's vfs_glusterfs on the next use.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/gfapi-at-parent-ref.c -lgfapi
TEST ./$(dirname $0)/gfapi-at-parent-ref $H0 $V0 $logdir/gfapi-at-parent-ref.log

cleanup_tester $(dirname $0)/gfapi-at-parent-ref

TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup;
