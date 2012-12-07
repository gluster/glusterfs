""" Gluster Swift Unit Tests """

import logging
from collections import defaultdict
from test import get_config
from swift.common.utils import TRUE_VALUES


class NullLoggingHandler(logging.Handler):

    def emit(self, record):
        pass


class FakeLogger(object):
    # a thread safe logger

    def __init__(self, *args, **kwargs):
        self._clear()
        self.level = logging.NOTSET
        if 'facility' in kwargs:
            self.facility = kwargs['facility']

    def _clear(self):
        self.log_dict = defaultdict(list)

    def _store_in(store_name):
        def stub_fn(self, *args, **kwargs):
            self.log_dict[store_name].append((args, kwargs))
        return stub_fn

    error = _store_in('error')
    info = _store_in('info')
    warning = _store_in('warning')
    debug = _store_in('debug')

    def exception(self, *args, **kwargs):
        self.log_dict['exception'].append((args, kwargs, str(exc_info()[1])))

    # mock out the StatsD logging methods:
    increment = _store_in('increment')
    decrement = _store_in('decrement')
    timing = _store_in('timing')
    timing_since = _store_in('timing_since')
    update_stats = _store_in('update_stats')
    set_statsd_prefix = _store_in('set_statsd_prefix')

    def setFormatter(self, obj):
        self.formatter = obj

    def close(self):
        self._clear()

    def set_name(self, name):
        # don't touch _handlers
        self._name = name

    def acquire(self):
        pass

    def release(self):
        pass

    def createLock(self):
        pass

    def emit(self, record):
        pass

    def handle(self, record):
        pass

    def flush(self):
        pass

    def handleError(self, record):
        pass


original_syslog_handler = logging.handlers.SysLogHandler


def fake_syslog_handler():
    for attr in dir(original_syslog_handler):
        if attr.startswith('LOG'):
            setattr(FakeLogger, attr,
                    copy.copy(getattr(logging.handlers.SysLogHandler, attr)))
    FakeLogger.priority_map = \
        copy.deepcopy(logging.handlers.SysLogHandler.priority_map)

    logging.handlers.SysLogHandler = FakeLogger


if get_config('unit_test').get('fake_syslog', 'False').lower() in TRUE_VALUES:
    fake_syslog_handler()
