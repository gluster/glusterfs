#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd;
TEST pidof glusterd;

GDWD=$($CLI system getwd)

# glusterd.info file will be created on either first peer probe or volume
# creation, hence we expect file to be not present in this case
TEST ! -e $GDWD/glusterd.info

cleanup;
