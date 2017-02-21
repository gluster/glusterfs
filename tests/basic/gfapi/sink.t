#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST build_tester $(dirname ${0})/gfapi-load-volfile.c -lgfapi
TEST ./$(dirname ${0})/gfapi-load-volfile $(dirname $0)/sink.vol

cleanup_tester $(dirname ${0})/gfapi-load-volfile

cleanup
