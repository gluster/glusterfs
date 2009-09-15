#!/bin/sh

echo "Testing 32-bit offsets build"
LD_PRELOAD="$(pwd)/ld-preload-lib.so" $(pwd)/ld-preload-test32 --path /tmp/testfile
echo
echo "Testing 64-bit offsets build"
LD_PRELOAD="$(pwd)/ld-preload-lib.so" $(pwd)/ld-preload-test64 --path /tmp/testfile
