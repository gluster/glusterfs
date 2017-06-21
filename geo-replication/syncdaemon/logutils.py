import logging
from logging import Logger, handlers
import sys
import time


class GLogger(Logger):

    """Logger customizations for gsyncd.

    It implements a log format similar to that of glusterfs.
    """

    def makeRecord(self, name, level, *a):
        rv = Logger.makeRecord(self, name, level, *a)
        rv.nsecs = (rv.created - int(rv.created)) * 1000000
        fr = sys._getframe(4)
        callee = fr.f_locals.get('self')
        if callee:
            ctx = str(type(callee)).split("'")[1].split('.')[-1]
        else:
            ctx = '<top>'
        if not hasattr(rv, 'funcName'):
            rv.funcName = fr.f_code.co_name
        rv.lvlnam = logging.getLevelName(level)[0]
        rv.ctx = ctx
        return rv


LOGFMT = ("[%(asctime)s.%(nsecs)d] %(lvlnam)s [%(module)s{0}"
          ":%(lineno)s:%(funcName)s] %(ctx)s: %(message)s")


def setup_logging(level="INFO", label="", log_file=""):
    if label:
        label = "(" + label + ")"

    filename = None
    stream = None
    if log_file:
        if log_file in ('-', '/dev/stderr'):
            stream = sys.stderr
        elif log_file == '/dev/stdout':
            stream = sys.stdout
        else:
            filename = log_file

    datefmt = "%Y-%m-%d %H:%M:%S"
    fmt = LOGFMT.format(label)
    logging.root = GLogger("root", level)
    logging.setLoggerClass(GLogger)
    logging.Formatter.converter = time.gmtime  # Log in GMT/UTC time
    logging.getLogger().handlers = []
    logging.getLogger().setLevel(level)

    if filename is not None:
        logging_handler = handlers.WatchedFileHandler(filename)
        formatter = logging.Formatter(fmt=fmt,
                                      datefmt=datefmt)
        logging_handler.setFormatter(formatter)
        logging.getLogger().addHandler(logging_handler)
    else:
        logging.basicConfig(stream=stream,
                            format=fmt,
                            datefmt=datefmt,
                            level=level)
