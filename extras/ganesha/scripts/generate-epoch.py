#!/usr/bin/python
#
# Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#
# Generates unique epoch value on each gluster node to be used by
# nfs-ganesha service on that node.
#
# Configure 'EPOCH_EXEC' option to this script path in
# '/etc/sysconfig/ganesha' file used by nfs-ganesha service.
#
# Construct epoch as follows -
#        first 32-bit contains the now() time
#        rest 32-bit value contains the local glusterd node uuid

import time
import binascii

# Calculate the now() time into a 64-bit integer value
def epoch_now():
        epoch_time = int(time.mktime(time.localtime())) << 32
        return epoch_time

# Read glusterd UUID and extract first 32-bit of it
def epoch_uuid():
        file_name = '/var/lib/glusterd/glusterd.info'

        for line in open(file_name):
                if "UUID" in line:
                        glusterd_uuid = line.split('=')[1].strip()

        uuid_bin = binascii.unhexlify(glusterd_uuid.replace("-",""))

        epoch_uuid = int(uuid_bin.encode('hex'), 32) & 0xFFFF0000
        return epoch_uuid

# Construct epoch as follows -
#        first 32-bit contains the now() time
#        rest 32-bit value contains the local glusterd node uuid
epoch = (epoch_now() | epoch_uuid())
print str(epoch)

exit(0)
