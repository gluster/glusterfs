#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks basic dispersed volume functionality and cli interface

DISPERSE=12
REDUNDANCY=4

# This must be equal to 36 * $DISPERSE + 109
TESTS_EXPECTED_IN_LOOP=541

. $(dirname $0)/ec-common
