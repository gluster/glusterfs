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

# Noop Middleware that simply allows us to monkey patch the constraints
import swift.plugins.constraints

class Gluster(object):
    """
    Noop middleware for use with the proxy server to get paste.deploy to load
    this middleware such that the plugin constraints monkey patch the common
    constraints ahead of their use.
    """
    def __init__(self, app, conf):
        self.app = app
        self.conf = conf

    def __call__(self, env, start_response):
        return self.app(env, start_response)


def filter_factory(global_conf, **local_conf):
    """Returns a WSGI filter app for use with paste.deploy."""
    conf = global_conf.copy()
    conf.update(local_conf)

    def gluster_filter(app):
        return Gluster(app, conf)
    return gluster_filter
