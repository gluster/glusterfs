#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import os
import ConfigParser

config = ConfigParser.ConfigParser()
config.read(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "tool.conf"))


def list_change_detectors():
    return dict(config.items("change_detectors")).keys()


def get_opt(opt):
    return config.get("vars", opt)


def get_change_detector(opt):
    return config.get("change_detectors", opt)
