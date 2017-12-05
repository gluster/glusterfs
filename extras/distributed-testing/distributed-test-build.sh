#!/bin/bash

set -e

EXTRA_CONFIGURE_ARGS="$@"
ASAN_REQUESTED=false
for arg in $EXTRA_CONFIGURE_ARGS; do
  if [ $arg == "--with-asan" ]; then
    echo "Requested ASAN, cleaning build first."
    make -j distclean || true
    touch .with_asan
    ASAN_REQUESTED=true
  fi
done

if [ $ASAN_REQUESTED == false ]; then
  if [ -f .with_asan ]; then
    echo "Previous build was with ASAN, cleaning build first."
    make -j distclean || true
    rm -v .with_asan
  fi
fi

source extras/distributed-testing/distributed-test-build-env
./autogen.sh
./configure $GF_CONF_OPTS $EXTRA_CONFIGURE_ARGS
make -j
