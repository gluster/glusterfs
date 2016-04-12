#  This file is part of DiSTAF
#  Copyright (C) 2015-2016  Red Hat, Inc. <http://www.redhat.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


from distaf.util import tc


def enable_quota(volname, server=''):
    """
        Enables quota on the specified volume

        Returns the output of quota cli command
    """
    if server == '':
        server = tc.nodes[0]
    cmd = "gluster volume quota %s enable" % volname
    return tc.run(server, cmd)


def set_quota_limit(volname, path='/', limit='100GB', server=''):
    """
        Sets limit-usage on the path of the specified volume to
        specified limit

        Returs the output of quota limit-usage command
    """
    if server == '':
        server = tc.nodes[0]
    cmd = "gluster volume quota %s limit-usage %s %s" % (volname, path, limit)
    return tc.run(server, cmd)
