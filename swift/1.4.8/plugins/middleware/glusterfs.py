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

# Monkey patch constraints
import swift.plugins.constraints
from swift.plugins.Glusterfs import Glusterfs
from ConfigParser import ConfigParser

fs_conf = ConfigParser()
if fs_conf.read('/etc/swift/fs.conf'):
    try:
        mount_path = fs_conf.get ('DEFAULT', 'mount_path')
    except NoSectionError, NoOptionError:
        # FIXME - How to log during module initialization
        logger.exception(_('ERROR mount_path not present'))
        mount_path = ''
else:
    mount_path = ''


class Gluster(object):
    """
    Update the environment with keys that reflect GlusterFS middleware enabled
    """
    def __init__(self, app, conf):
        self.app = app
        self.conf = conf

    def __call__(self, env, start_response):
        env['Gluster_enabled'] = True
        env['fs_object'] = Glusterfs()
        env['root'] = mount_path
        return self.app(env, start_response)


def filter_factory(global_conf, **local_conf):
    """Returns a WSGI filter app for use with paste.deploy."""
    conf = global_conf.copy()
    conf.update(local_conf)

    def gluster_filter(app):
        return Gluster(app, conf)
    return gluster_filter
