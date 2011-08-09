#!/bin/sh

#   Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
#   This file is part of GlusterFS.

#   GlusterFS is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published
#   by the Free Software Foundation; either version 3 of the License,
#   or (at your option) any later version.

#   GlusterFS is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.

#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see
#   <http://www.gnu.org/licenses/>.

# Running gluster sanity test which starts glusterd and runs gluster commands, and exit at the first failure.
$PWD/gluster_commands.sh

if [ $? -ne 0 ]; then
    echo "sanity failed"
else
    echo "sanity passed"
fi

# Stopping glusterd
$PWD/stop_glusterd.sh

