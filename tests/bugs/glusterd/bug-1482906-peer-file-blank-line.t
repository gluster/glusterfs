#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../cluster.rc

#Tests for add new line in peers file
function add_new_line_to_peer_file  {
	UUID_NAME=$($CLI_1 peer status  | grep Uuid)
	PEER_ID=$(echo $UUID_NAME | cut -c 7-)
	GD_WD=$($CLI_1 system getwd)
	GD_WD+=/peers/
	PATH_TO_PEER_FILE=$GD_WD$PEER_ID
	sed -i '1s/^/\n/gm; $s/$/\n/gm' $PATH_TO_PEER_FILE
}

cleanup;

TEST launch_cluster 2;

TEST $CLI_1 peer probe $H2;

EXPECT_WITHIN $PROBE_TIMEOUT 1 peer_count

add_new_line_to_peer_file

TEST kill_glusterd 1
TEST  $glusterd_1

cleanup;
