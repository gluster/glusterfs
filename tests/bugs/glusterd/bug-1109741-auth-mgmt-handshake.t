#! /bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

# The test will attempt to verify that management handshake requests to
# GlusterD are authenticated before being allowed to change a GlusterD's
# op-version
#
# 1. Launch 3 glusterds
# 2. Probe 2 of them to form a cluster. This should succeed.
# 3. Probe either of the first two GlusterD's from the 3rd GlusterD. This should fail.
# 4. a. Reduce the op-version of 3rd GlusterD and restart it.
#    b. Probe either of the first two GlusterD's from the 3rd GlusterD. This should fail.
# 5. Check current op-version of first two GlusterDs. It shouldn't have changed.
# 6. Probe third GlusterD from the cluster. This should succeed.


cleanup

TEST launch_cluster 3

TEST $CLI_1 peer probe $H2

TEST ! $CLI_3 peer probe $H1

GD1_WD=$($CLI_1 system getwd)
OP_VERS_ORIG=$(grep 'operating-version' ${GD1_WD}/glusterd.info | cut -d '=' -f 2)

TEST $CLI_3 system uuid get # Needed for glusterd.info to be created

GD3_WD=$($CLI_3 system getwd)
TEST sed -rnie "'s/(operating-version=)\w+/\130600/gip'" ${GD3_WD}/glusterd.info

TEST kill_glusterd 3
TEST start_glusterd 3

TEST ! $CLI_3 peer probe $H1

OP_VERS_NEW=$(grep 'operating-version' ${GD1_WD}/glusterd.info | cut -d '=' -f 2)
TEST [[ $OP_VERS_ORIG == $OP_VERS_NEW ]]

TEST $CLI_1 peer probe $H3

kill_node 1
kill_node 2
kill_node 3

cleanup;

