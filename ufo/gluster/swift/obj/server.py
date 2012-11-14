# Copyright (c) 2012 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.

""" Object Server for Gluster Swift UFO """

# Simply importing this monkey patches the constraint handling to fit our
# needs
import gluster.swift.common.constraints
import gluster.swift.common.utils

from swift.obj import server
from gluster.swift.common.DiskFile import Gluster_DiskFile

# Monkey patch the object server module to use Gluster's DiskFile definition
server.DiskFile = Gluster_DiskFile


def app_factory(global_conf, **local_conf):
    """paste.deploy app factory for creating WSGI object server apps"""
    conf = global_conf.copy()
    conf.update(local_conf)
    return server.ObjectController(conf)
