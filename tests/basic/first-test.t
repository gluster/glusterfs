#!/bin/bash

. $(dirname $0)/../include.rc

cat << EOF
This test should run first for http://review.gluster.org/#/c/13439/ and should
be removed once that patch has been merged.
EOF

TEST true
