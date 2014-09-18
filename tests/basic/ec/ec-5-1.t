#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks basic dispersed volume functionality and cli interface

DISPERSE=5
REDUNDANCY=1

# This must be equal to 36 * $DISPERSE + 109
TESTS_EXPECTED_IN_LOOP=289

. $(dirname $0)/ec-common
