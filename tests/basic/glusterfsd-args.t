#!/bin/bash

. $(dirname $0)/../include.rc

EXPECT $GLUSTER_LIBEXECDIR glusterfsd --print-libexecdir
