import os

class GConf(object):
    """singleton class to store globals
       shared between gsyncd modules"""

    ssh_ctl_dir = None
    ssh_ctl_args = None
    cpid = None
    pid_file_owned = False
    log_exit = False
    permanent_handles = []
    log_metadata = {}

gconf = GConf()
