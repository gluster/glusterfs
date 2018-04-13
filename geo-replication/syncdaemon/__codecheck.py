#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

from __future__ import print_function
import os
import os.path
import sys
import tempfile
import shutil

ipd = tempfile.mkdtemp(prefix='codecheck-aux')

try:
    # add a fake ipaddr module, we don't want to
    # deal with the real one (just test our code)
    f = open(os.path.join(ipd, 'ipaddr.py'), 'w')
    f.write("""
class IPAddress(object):
    pass
class IPNetwork(list):
    pass
""")
    f.close()
    sys.path.append(ipd)

    fl = os.listdir(os.path.dirname(sys.argv[0]) or '.')
    fl.sort()
    for f in fl:
        if f[-3:] != '.py' or f[0] == '_':
            continue
        m = f[:-3]
        sys.stdout.write('importing %s ...' % m)
        __import__(m)
        print(' OK.')

    def sys_argv_set(a):
        sys.argv = sys.argv[:1] + a

    gsyncd = sys.modules['gsyncd']
    for a in [['--help'], ['--version'],
              ['--canonicalize-escape-url', '/foo']]:
        print(('>>> invoking program with args: %s' % ' '.join(a)))
        pid = os.fork()
        if not pid:
            sys_argv_set(a)
            gsyncd.main()
        _, r = os.waitpid(pid, 0)
        if r:
            raise RuntimeError('invocation failed')
finally:
    shutil.rmtree(ipd)
