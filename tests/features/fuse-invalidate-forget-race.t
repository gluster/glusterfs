#!/bin/bash

. $(dirname $0)/../include.rc

TESTER=$(dirname $0)/fuse-invalidate-forget-race
source_root=$(cd $(dirname $0)/../.. && pwd)
host_os=$(sed -n 's/^GF_HOST_OS = //p' "$source_root/Makefile")

# Link the inode API directly so the test can control callback ordering
# without a FUSE mount, a running volume, or sleeps.
TEST build_tester "$TESTER.c" -I"$source_root" -I"$source_root/libglusterfs/src" \
    -include "$source_root/config.h" -include "$source_root/site.h" \
    -D"$host_os" -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -lglusterfs
TEST "$TESTER"

cleanup_tester "$TESTER"
