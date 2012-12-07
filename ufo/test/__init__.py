# See http://code.google.com/p/python-nose/issues/detail?id=373
# The code below enables nosetests to work with i18n _() blocks

import __builtin__
import sys
import os
from ConfigParser import MissingSectionHeaderError
from StringIO import StringIO

from swift.common.utils import readconf

setattr(__builtin__, '_', lambda x: x)


# Work around what seems to be a Python bug.
# c.f. https://bugs.launchpad.net/swift/+bug/820185.
import logging
logging.raiseExceptions = False


def get_config(section_name=None, defaults=None):
    """
    Attempt to get a test config dictionary.

    :param section_name: the section to read (all sections if not defined)
    :param defaults: an optional dictionary namespace of defaults
    """
    config_file = os.environ.get('SWIFT_TEST_CONFIG_FILE',
                                 '/etc/swift/test.conf')
    config = {}
    if defaults is not None:
        config.update(defaults)

    try:
        config = readconf(config_file, section_name)
    except SystemExit:
        if not os.path.exists(config_file):
            print >>sys.stderr, \
                'Unable to read test config %s - file not found' \
                % config_file
        elif not os.access(config_file, os.R_OK):
            print >>sys.stderr, \
                'Unable to read test config %s - permission denied' \
                % config_file
        else:
            print >>sys.stderr, \
                'Unable to read test config %s - section %s not found' \
                % (config_file, section_name)
    return config
