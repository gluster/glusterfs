#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import sys
from subprocess import PIPE, Popen
from errno import EEXIST, ENOENT
import xml.etree.cElementTree as etree
import logging
import os
from datetime import datetime
import urllib

ROOT_GFID = "00000000-0000-0000-0000-000000000001"
DEFAULT_CHANGELOG_INTERVAL = 15

ParseError = etree.ParseError if hasattr(etree, 'ParseError') else SyntaxError
cache_data = {}


class RecordType(object):
    NEW = "NEW"
    MODIFY = "MODIFY"
    RENAME = "RENAME"
    DELETE = "DELETE"


def cache_output(func):
    def wrapper(*args, **kwargs):
        global cache_data
        if cache_data.get(func.func_name, None) is None:
            cache_data[func.func_name] = func(*args, **kwargs)

        return cache_data[func.func_name]
    return wrapper


def handle_rm_error(func, path, exc_info):
    if exc_info[1].errno == ENOENT:
        return

    raise exc_info[1]


def find(path, callback_func=lambda x: True, filter_func=lambda x: True,
         ignore_dirs=[], subdirs_crawl=True):
    if path in ignore_dirs:
        return

    # Capture filter_func output and pass it to callback function
    filter_result = filter_func(path)
    if filter_result is not None:
        callback_func(path, filter_result)

    for p in os.listdir(path):
        full_path = os.path.join(path, p)

        if os.path.isdir(full_path):
            if subdirs_crawl:
                find(full_path, callback_func, filter_func, ignore_dirs)
            else:
                filter_result = filter_func(full_path)
                if filter_result is not None:
                    callback_func(full_path, filter_result)
        else:
            filter_result = filter_func(full_path)
            if filter_result is not None:
                callback_func(full_path, filter_result)


def output_write(f, path, prefix=".", encode=False, tag=""):
    if path == "":
        return

    if prefix != ".":
        path = os.path.join(prefix, path)

    if encode:
        path = urllib.quote_plus(path)

    # set the field separator
    FS = "" if tag == "" else " "

    f.write("%s%s%s\n" % (tag.strip(), FS, path))


def human_time(ts):
    return datetime.fromtimestamp(float(ts)).strftime("%Y-%m-%d %H:%M:%S")


def setup_logger(logger, path, debug=False):
    if debug:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.INFO)

    # create the logging file handler
    fh = logging.FileHandler(path)

    formatter = logging.Formatter("[%(asctime)s] %(levelname)s "
                                  "[%(module)s - %(lineno)s:%(funcName)s] "
                                  "- %(message)s")

    fh.setFormatter(formatter)

    # add handler to logger object
    logger.addHandler(fh)


def create_file(path, exit_on_err=False, logger=None):
    """
    If file exists overwrite. Print error to stderr and exit
    if exit_on_err is set, else raise the exception. Consumer
    should handle the exception.
    """
    try:
        open(path, 'w').close()
    except (OSError, IOError) as e:
        if exit_on_err:
            fail("Failed to create file %s: %s" % (path, e), logger=logger)
        else:
            raise


def mkdirp(path, exit_on_err=False, logger=None):
    """
    Try creating required directory structure
    ignore EEXIST and raise exception for rest of the errors.
    Print error in stderr and exit if exit_on_err is set, else
    raise exception.
    """
    try:
        os.makedirs(path)
    except (OSError, IOError) as e:
        if e.errno == EEXIST and os.path.isdir(path):
            pass
        else:
            if exit_on_err:
                fail("Fail to create dir %s: %s" % (path, e), logger=logger)
            else:
                raise


def fail(msg, code=1, logger=None):
    """
    Write error to stderr and exit
    """
    if logger:
        logger.error(msg)
    sys.stderr.write("%s\n" % msg)
    sys.exit(code)


def execute(cmd, exit_msg=None, logger=None):
    """
    If failure_msg is not None then return returncode, out and error.
    If failure msg is set, write to stderr and exit.
    """
    p = Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=PIPE, close_fds=True)

    (out, err) = p.communicate()
    if p.returncode != 0 and exit_msg is not None:
        fail("%s: %s" % (exit_msg, err), p.returncode, logger=logger)

    return (p.returncode, out, err)


def symlink_gfid_to_path(brick, gfid):
    """
    Each directories are symlinked to file named GFID
    in .glusterfs directory of brick backend. Using readlink
    we get PARGFID/basename of dir. readlink recursively till
    we get PARGFID as ROOT_GFID.
    """
    if gfid == ROOT_GFID:
        return ""

    out_path = ""
    while True:
        path = os.path.join(brick, ".glusterfs", gfid[0:2], gfid[2:4], gfid)
        path_readlink = os.readlink(path)
        pgfid = os.path.dirname(path_readlink)
        out_path = os.path.join(os.path.basename(path_readlink), out_path)
        if pgfid == "../../00/00/%s" % ROOT_GFID:
            break
        gfid = os.path.basename(pgfid)
    return out_path


@cache_output
def get_my_uuid():
    cmd = ["gluster", "system::", "uuid", "get", "--xml"]
    rc, out, err = execute(cmd)

    if rc != 0:
        return None

    tree = etree.fromstring(out)
    uuid_el = tree.find("uuidGenerate/uuid")
    return uuid_el.text


def is_host_local(host_uuid):
    # Get UUID only if it is not done previously
    # else Cache the UUID value
    my_uuid = get_my_uuid()
    if my_uuid == host_uuid:
        return True

    return False


def get_changelog_rollover_time(volumename):
    cmd = ["gluster", "volume", "get", volumename,
           "changelog.rollover-time", "--xml"]
    rc, out, err = execute(cmd)

    if rc != 0:
        return DEFAULT_CHANGELOG_INTERVAL

    try:
        tree = etree.fromstring(out)
        return int(tree.find('volGetopts/Opt/Value').text)
    except ParseError:
        return DEFAULT_CHANGELOG_INTERVAL


def output_path_prepare(path, args):
    """
    If Prefix is set, joins to Path, removes ending slash
    and encodes it.
    """
    if args.output_prefix != ".":
        path = os.path.join(args.output_prefix, path)
        if path.endswith("/"):
            path = path[0:len(path)-1]

    if args.no_encode:
        return path
    else:
        return urllib.quote_plus(path.encode("utf-8"))
