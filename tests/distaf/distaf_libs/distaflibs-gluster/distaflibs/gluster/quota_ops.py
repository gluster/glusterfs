#!/usr/bin/env python
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

"""
    Description: Library for gluster quota operations.
"""

from distaf.util import tc
import re
import time

try:
    import xml.etree.cElementTree as etree
except ImportError:
    import xml.etree.ElementTree as etree


def enable_quota(volname, mnode=None):
    """Enables quota on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        enable_quota(testvol)
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s enable" % volname
    ret = tc.run(mnode, cmd)
    return ret


def disable_quota(volname, mnode=None):
    """Disables quota on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        disable_quota(testvol)
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s disable --mode=script" % volname
    ret = tc.run(mnode, cmd)
    return ret


def is_quota_enabled(volname, mnode=None):
    """Checks if quota is enabled on given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        bool: True, if quota is enabled
            False, if quota is disabled

    Example:
        is_quota_enabled(testvol)
    """

    import distaflibs.gluster.volume_ops

    if mnode is None:
        mnode = tc.servers[0]

    output = distaflibs.gluster.volume_ops.get_volume_option(volname,
                                                             "features.quota",
                                                             mnode)
    if output is None:
        return False

    tc.logger.info("Quota Status in volume %s %s"
                   % (volname, output["features.quota"]))
    if output["features.quota"] != 'on':
        return False

    return True


def quota_list(volname, mnode=None):
    """Executes quota list command for given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        quota_list(testvol)
    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s list" % volname
    ret = tc.run(mnode, cmd)
    return ret


def set_quota_limit_usage(volname, path='/', limit='100GB', soft_limit='',
                          mnode=None):
    """Sets limit-usage on the path of the specified volume to
        specified limit

    Args:
        volname (str): volume name

    Kwargs:
        path (str): path to which quota limit usage is set.
            Defaults to /.
        limit (str): quota limit usage. defaults to 100GB
        soft_limit (str): quota soft limit to be set
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> set_quota_limit_usage("testvol")

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s limit-usage %s %s %s --mode=script"
           % (volname, path, limit, soft_limit))
    return tc.run(mnode, cmd)


def get_quota_list(volname, mnode=None):
    """Parse the output of 'gluster quota list' command.

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict on success.

    Examples:
        >>> get_quota_list("testvol", mnode = 'abc.lab.eng.xyz.com')
        {'/': {'used_space': '0', 'hl_exceeded': 'No', 'soft_limit_percent':
        '60%', 'avail_space': '2147483648', 'soft_limit_value': '1288490188',
        'sl_exceeded': 'No', 'hard_limit': '2147483648'}}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s list --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'quota list' on node %s. "
                        "Hence failed to get the quota list.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster quota list xml output.")
        return None

    quotalist = {}
    for path in root.findall("volQuota/limit"):
        for elem in path.getchildren():
            if elem.tag == "path":
                path = elem.text
                quotalist[path] = {}
            else:
                quotalist[path][elem.tag] = elem.text
    return quotalist


def set_quota_limit_objects(volname, path='/', limit='10', soft_limit='',
                            mnode=None):
    """Sets limit-objects on the path of the specified volume to
        specified limit

    Args:
        volname (str): volume name

    Kwargs:
        path (str): path to which quota limit usage is set.
            Defaults to /.
        limit (str): quota limit objects. defaults to 10.
        soft_limit (str): quota soft limit to be set
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> set_quota_limit_objects("testvol")

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s limit-objects %s %s %s --mode=script"
           % (volname, path, limit, soft_limit))
    return tc.run(mnode, cmd)


def quota_list_objects(volname, mnode=None):
    """Executes quota list command for given volume

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which cmd has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Example:
        quota_list_objects(testvol)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s list-objects" % volname
    ret = tc.run(mnode, cmd)
    return ret


def get_quota_list_objects(volname, mnode=None):
    """Parse the output of 'gluster quota list-objects' command.

    Args:
        volname (str): volume name

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        NoneType: None if command execution fails, parse errors.
        dict: dict of dict on success.

    Examples:
        >>> get_quota_list_objects("testvol", mnode = 'abc.lab.eng.xyz.com')
        {'/': {'available': '7', 'hl_exceeded': 'No', 'soft_limit_percent':
        '80%', 'soft_limit_value': '8', 'dir_count': '3', 'sl_exceeded':
        'No', 'file_count': '0', 'hard_limit': '10'}}
    """

    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s list-objects --xml" % volname
    ret, out, _ = tc.run(mnode, cmd, verbose=False)
    if ret != 0:
        tc.logger.error("Failed to execute 'quota list' on node %s. "
                        "Hence failed to get the quota list.", mnode)
        return None

    try:
        root = etree.XML(out)
    except etree.ParseError:
        tc.logger.error("Failed to parse the gluster quota list xml output.")
        return None

    quotalist = {}
    for path in root.findall("volQuota/limit"):
        for elem in path.getchildren():
            if elem.tag == "path":
                path = elem.text
                quotalist[path] = {}
            else:
                quotalist[path][elem.tag] = elem.text
    return quotalist


def set_quota_alert_time(volname, time, mnode=None):
    """Sets quota alert time

    Args:
        volname (str): volume name

    Kwargs:
        time (str): quota limit usage. defaults to 100GB
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> set_quota_alert_time("testvol", <alert time>)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s alert-time %s --mode=script"
           % (volname, time))
    return tc.run(mnode, cmd)


def set_quota_soft_timeout(volname, timeout, mnode=None):
    """Sets quota soft timeout

    Args:
        volname (str): volume name

    Kwargs:
        timeout (str): quota soft limit timeout value
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> set_quota_soft_timeout("testvol", <timeout-value>)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s soft-timeout %s --mode=script"
           % (volname, timeout))
    return tc.run(mnode, cmd)


def set_quota_hard_timeout(volname, timeout, mnode=None):
    """Sets quota hard timeout

    Args:
        volname (str): volume name
        timeout (str): quota hard limit timeout value

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> set_quota_hard_timeout("testvol", <timeout-value>)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s hard-timeout %s --mode=script"
           % (volname, timeout))
    return tc.run(mnode, cmd)


def set_quota_default_soft_limit(volname, timeout, mnode=None):
    """Sets quota default soft limit

    Args:
        volname (str): volume name
        timeout (str): quota soft limit timeout value

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> set_quota_default_soft_limit("testvol", <timeout-value>)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s default-soft-limit %s --mode=script"
           % (volname, timeout))
    return tc.run(mnode, cmd)


def remove_quota(volname, path, mnode=None):
    """Removes quota for the given path

    Args:
        volname (str): volume name
        path (str): path to which quota limit usage is set.
            Defaults to /.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> remove_quota("testvol", <path>)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = "gluster volume quota %s remove %s --mode=script" % (volname, path)
    return tc.run(mnode, cmd)


def remove_quota_objects(volname, path, mnode=None):
    """Removes quota objects for the given path

    Args:
        volname (str): volume name
        path (str): path to which quota limit usage is set.
            Defaults to /.

    Kwargs:
        mnode (str): Node on which command has to be executed.
            If None, defaults to servers[0].

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            The first element 'ret' is of type 'int' and is the return value
            of command execution.

            The second element 'out' is of type 'str' and is the stdout value
            of the command execution.

            The third element 'err' is of type 'str' and is the stderr value
            of the command execution.

    Examples:
        >>> remove_quota_objects("testvol", <path>)

    """
    if mnode is None:
        mnode = tc.servers[0]

    cmd = ("gluster volume quota %s remove-objects %s --mode=script"
           % (volname, path))
    return tc.run(mnode, cmd)
