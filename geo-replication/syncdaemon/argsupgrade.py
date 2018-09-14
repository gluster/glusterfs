# Converts old style args into new style args

from __future__ import print_function
import sys
from argparse import ArgumentParser
import socket
import os

from syncdutils import GsyncdError
from conf import GLUSTERD_WORKDIR


def gethostbyname(hnam):
    """gethostbyname wrapper"""
    try:
        return socket.gethostbyname(hnam)
    except socket.gaierror:
        ex = sys.exc_info()[1]
        raise GsyncdError("failed to resolve %s: %s" %
                          (hnam, ex.strerror))


def slave_url(urldata):
    urldata = urldata.replace("ssh://", "")
    host, vol = urldata.split("::")
    vol = vol.split(":")[0]
    return "%s::%s" % (host, vol)


def init_gsyncd_template_conf():
    path = GLUSTERD_WORKDIR + "/geo-replication/gsyncd_template.conf"
    dname = os.path.dirname(path)
    if not os.path.exists(dname):
        try:
            os.mkdir(dname)
        except OSError:
            pass

    if not os.path.exists(path):
        fd = os.open(path, os.O_CREAT | os.O_RDWR)
        os.close(fd)


def init_gsyncd_session_conf(master, slave):
    slave = slave_url(slave)
    master = master.strip(":")
    slavehost, slavevol = slave.split("::")
    slavehost = slavehost.split("@")[-1]

    # Session Config File
    path = "%s/geo-replication/%s_%s_%s/gsyncd.conf" % (
        GLUSTERD_WORKDIR, master, slavehost, slavevol)

    if os.path.exists(os.path.dirname(path)) and not os.path.exists(path):
        fd = os.open(path, os.O_CREAT | os.O_RDWR)
        os.close(fd)


def init_gsyncd_conf(path):
    dname = os.path.dirname(path)
    if not os.path.exists(dname):
        try:
            os.mkdir(dname)
        except OSError:
            pass

    if os.path.exists(dname) and not os.path.exists(path):
        fd = os.open(path, os.O_CREAT | os.O_RDWR)
        os.close(fd)


def upgrade():
    # Create dummy template conf(empty), hack to avoid glusterd
    # fail when it does stat to check the existence.
    init_gsyncd_template_conf()

    if "--monitor" in sys.argv:
        # python gsyncd.py --path=/bricks/b1
        # --monitor -c gsyncd.conf
        # --iprefix=/var :gv1
        # --glusterd-uuid=f26ac7a8-eb1b-4ea7-959c-80b27d3e43d0
        # f241::gv2
        p = ArgumentParser()
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("--glusterd-uuid")
        p.add_argument("-c")
        p.add_argument("--iprefix")
        p.add_argument("--path", action="append")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        # Overwrite the sys.argv after rearrange
        init_gsyncd_session_conf(pargs.master, pargs.slave)
        sys.argv = [
            sys.argv[0],
            "monitor",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            "--local-node-id",
            pargs.glusterd_uuid
        ]
    elif "--status-get" in sys.argv:
        # -c gsyncd.conf --iprefix=/var :gv1 f241::gv2
        # --status-get --path /bricks/b1
        p = ArgumentParser()
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("-c")
        p.add_argument("--path")
        p.add_argument("--iprefix")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        sys.argv = [
            sys.argv[0],
            "status",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            "--local-path",
            pargs.path
        ]
    elif "--canonicalize-url" in sys.argv:
        # This can accept multiple URLs and converts each URL to the
        # format ssh://USER@IP:gluster://127.0.0.1:VOLUME
        # This format not used in gsyncd, but added for glusterd compatibility
        p = ArgumentParser()
        p.add_argument("--canonicalize-url", nargs="+")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        for url in pargs.canonicalize_url:
            host, vol = url.split("::")
            host = host.replace("ssh://", "")
            remote_addr = host
            if "@" not in remote_addr:
                remote_addr = "root@" + remote_addr

            user, hname = remote_addr.split("@")

            print(("ssh://%s@%s:gluster://127.0.0.1:%s" % (
                user, gethostbyname(hname), vol)))

        sys.exit(0)
    elif "--normalize-url" in sys.argv:
        # Adds schema prefix as ssh://
        # This format not used in gsyncd, but added for glusterd compatibility
        p = ArgumentParser()
        p.add_argument("--normalize-url")
        pargs = p.parse_known_args(sys.argv[1:])[0]
        print(("ssh://%s" % slave_url(pargs.normalize_url)))
        sys.exit(0)
    elif "--config-get-all" in sys.argv:
        #  -c gsyncd.conf --iprefix=/var :gv1 f241::gv2 --config-get-all
        p = ArgumentParser()
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("-c")
        p.add_argument("--iprefix")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        sys.argv = [
            sys.argv[0],
            "config-get",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            "--show-defaults",
            "--use-underscore"
        ]
    elif "--verify" in sys.argv and "spawning" in sys.argv:
        # Just checks that able to spawn gsyncd or not
        sys.exit(0)
    elif "--slavevoluuid-get" in sys.argv:
        # --slavevoluuid-get f241::gv2
        p = ArgumentParser()
        p.add_argument("--slavevoluuid-get")
        p.add_argument("-c")
        p.add_argument("--iprefix")
        pargs = p.parse_known_args(sys.argv[1:])[0]
        host, vol = pargs.slavevoluuid_get.split("::")

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "voluuidget",
            host,
            vol
        ]
    elif "--config-set-rx" in sys.argv:
        # Not required since default conf is not generated
        # and custom conf generated only when required
        # -c gsyncd.conf --config-set-rx remote-gsyncd
        # /usr/local/libexec/glusterfs/gsyncd . .
        # Touch the gsyncd.conf file and create session
        # directory if required
        p = ArgumentParser()
        p.add_argument("-c", dest="config_file")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        # If not template conf then it is trying to create
        # session config, create a empty file instead
        if pargs.config_file.endswith("gsyncd.conf"):
            init_gsyncd_conf(pargs.config_file)
        sys.exit(0)
    elif "--create" in sys.argv:
        # To update monitor status file
        # --create Created -c gsyncd.conf
        # --iprefix=/var :gv1 f241::gv2
        p = ArgumentParser()
        p.add_argument("--create")
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("-c")
        p.add_argument("--iprefix")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "monitor-status",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            pargs.create
        ]
    elif "--config-get" in sys.argv:
        # -c gsyncd.conf --iprefix=/var :gv1 f241::gv2 --config-get pid-file
        p = ArgumentParser()
        p.add_argument("--config-get")
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("-c")
        p.add_argument("--iprefix")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "config-get",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            "--only-value",
            "--show-defaults",
            "--name",
            pargs.config_get.replace("_", "-")
        ]
    elif "--config-set" in sys.argv:
        # ignore session-owner
        if "session-owner" in sys.argv:
            sys.exit(0)

        # --path=/bricks/b1  -c gsyncd.conf :gv1 f241::gv2
        # --config-set log_level DEBUG
        p = ArgumentParser()
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("--config-set", action='store_true')
        p.add_argument("name")
        p.add_argument("--value")
        p.add_argument("-c")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "config-set",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            "--name=%s" % pargs.name,
            "--value=%s" % pargs.value
        ]
    elif "--config-check" in sys.argv:
        # --config-check georep_session_working_dir
        p = ArgumentParser()
        p.add_argument("--config-check")
        p.add_argument("-c")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "config-check",
            pargs.config_check.replace("_", "-")
        ]
    elif "--config-del" in sys.argv:
        # -c gsyncd.conf --iprefix=/var :gv1 f241::gv2 --config-del log_level
        p = ArgumentParser()
        p.add_argument("--config-del")
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("-c")
        p.add_argument("--iprefix")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "config-reset",
            pargs.master.strip(":"),
            slave_url(pargs.slave),
            pargs.config_del.replace("_", "-")
        ]
    elif "--delete" in sys.argv:
        # --delete -c gsyncd.conf --iprefix=/var
        # --path-list=--path=/bricks/b1  :gv1 f241::gv2
        p = ArgumentParser()
        p.add_argument("--reset-sync-time", action="store_true")
        p.add_argument("--path-list")
        p.add_argument("master")
        p.add_argument("slave")
        p.add_argument("--iprefix")
        p.add_argument("-c")
        pargs = p.parse_known_args(sys.argv[1:])[0]

        init_gsyncd_session_conf(pargs.master, pargs.slave)

        paths = pargs.path_list.split("--path=")
        paths = ["--path=%s" % x.strip() for x in paths if x.strip() != ""]

        # Modified sys.argv
        sys.argv = [
            sys.argv[0],
            "delete",
            pargs.master.strip(":"),
            slave_url(pargs.slave)
        ]
        sys.argv += paths

        if pargs.reset_sync_time:
            sys.argv.append("--reset-sync-time")
