#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

# This test checks basic dispersed volume functionality and cli interface

DISPERSE=7
REDUNDANCY=3

# This must be equal to 44 * $DISPERSE + 106
TESTS_EXPECTED_IN_LOOP=414

. $(dirname $0)/ec-common
