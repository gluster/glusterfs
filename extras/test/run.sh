#!/bin/sh

#  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.

# Running gluster sanity test which starts glusterd and runs gluster commands, and exit at the first failure.
$PWD/gluster_commands.sh

if [ $? -ne 0 ]; then
    echo "sanity failed"
else
    echo "sanity passed"
fi

# Stopping glusterd
$PWD/stop_glusterd.sh

