#!/bin/bash

#  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.

#This script stops the glusterd running on the machine. Helpful for gluster sanity script

killall -9 glusterd

if [ $? -ne 0 ]; then
    echo "Error: Could not kill glusterd. Either glusterd is not running or kill it manually"
else
    echo "Killed glusterd"
fi
