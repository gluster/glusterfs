#!/bin/bash

. $(dirname $0)/../../include.rc

CHECKSUM_SOURCE=$(dirname $0)/../../../libglusterfs/src/checksum.c
CHECKSUM_INCLUDE=$(dirname $0)/../../../libglusterfs/src
CHECKSUM_TEST=$(dirname $0)/../../utils/checksum-failure

if openssl version | grep -q '^OpenSSL 3'; then
    build_tester $(dirname $0)/../../utils/checksum-failure.c \
        -I$CHECKSUM_INCLUDE $CHECKSUM_SOURCE -lcrypto -lz
    TEST $CHECKSUM_TEST
    TEST rm -f $CHECKSUM_TEST
else
    echo "Skipping checksum failure injection because OpenSSL 3 is not available."
fi

cleanup
