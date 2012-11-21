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

""" Account Server for Gluster Swift UFO """

# Simply importing this monkey patches the constraint handling to fit our
# needs
import gluster.swift.common.constraints

from swift.account import server
from gluster.swift.common.DiskDir import DiskAccount


class AccountController(server.AccountController):
    def _get_account_broker(self, drive, part, account):
        """
        Overriden to provide the GlusterFS specific broker that talks to
        Gluster for the information related to servicing a given request
        instead of talking to a database.

        :param drive: drive that holds the container
        :param part: partition the container is in
        :param account: account name
        :returns: DiskDir object
        """
        return DiskAccount(self.root, drive, account, self.logger)


def app_factory(global_conf, **local_conf):
    """paste.deploy app factory for creating WSGI account server apps."""
    conf = global_conf.copy()
    conf.update(local_conf)
    return AccountController(conf)
