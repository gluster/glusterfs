#!/usr/bin/python2

from __future__ import absolute_import
from __future__ import division
from __future__ import unicode_literals
from __future__ import print_function
import re
import sys
import fcntl
import base64
import threading
import socket
import os
import shlex
import argparse
import subprocess
import time
import SimpleXMLRPCServer
import xmlrpclib
import md5
import httplib
import uuid

DEFAULT_PORT = 9999
TEST_TIMEOUT_S = 15 * 60
CLIENT_CONNECT_TIMEOUT_S = 10
CLIENT_TIMEOUT_S = 60
PATCH_FILE_UID = str(uuid.uuid4())
SSH_TIMEOUT_S = 10
MAX_ATTEMPTS = 3


def patch_file():
    return "/tmp/%s-patch.tar.gz" % PATCH_FILE_UID

# ..............................................................................
# SimpleXMLRPCServer IPv6 Wrapper
# ..............................................................................


class IPv6SimpleXMLRPCServer(SimpleXMLRPCServer.SimpleXMLRPCServer):
    def __init__(self, addr):
        SimpleXMLRPCServer.SimpleXMLRPCServer.__init__(self, addr)

    def server_bind(self):
        if self.socket:
            self.socket.close()
        self.socket = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        SimpleXMLRPCServer.SimpleXMLRPCServer.server_bind(self)


class IPv6HTTPConnection(httplib.HTTPConnection):
    def __init__(self, host):
        self.host = host
        httplib.HTTPConnection.__init__(self, host)

    def connect(self):
        old_timeout = socket.getdefaulttimeout()
        self.sock = socket.create_connection((self.host, self.port),
                                             timeout=CLIENT_CONNECT_TIMEOUT_S)
        self.sock.settimeout(old_timeout)


class IPv6Transport(xmlrpclib.Transport):
    def __init__(self, *args, **kwargs):
        xmlrpclib.Transport.__init__(self, *args, **kwargs)

    def make_connection(self, host):
        return IPv6HTTPConnection(host)


# ..............................................................................
# Common
# ..............................................................................


class Timer:
    def __init__(self):
        self.start = time.time()

    def elapsed_s(self):
        return int(time.time() - self.start)

    def reset(self):
        ret = self.elapsed_s()
        self.start = time.time()
        return ret


def encode(buf):
    return base64.b16encode(buf)


def decode(buf):
    return base64.b16decode(buf)


def get_file_content(path):
    with open(path, "r") as f:
        return f.read()


def write_to_file(path, data):
    with open(path, "w") as f:
        f.write(data)


def failsafe(fn, args=()):
    try:
        return (True, fn(*args))
    except (xmlrpclib.Fault, xmlrpclib.ProtocolError, xmlrpclib.ResponseError,
            Exception) as err:
        Log.debug(str(err))
    return (False, None)


class LogLevel:
    DEBUG = 2
    ERROR = 1
    CLI = 0


class Log:
    LOGLEVEL = LogLevel.ERROR

    @staticmethod
    def _normalize(msg):
        return msg[:100]

    @staticmethod
    def debug(msg):
        if Log.LOGLEVEL >= LogLevel.DEBUG:
            sys.stdout.write("<debug> %s\n" % Log._normalize(msg))
            sys.stdout.flush()

    @staticmethod
    def error(msg):
        sys.stderr.write("<error> %s\n" % Log._normalize(msg))

    @staticmethod
    def header(msg):
        sys.stderr.write("* %s *\n" % Log._normalize(msg))

    @staticmethod
    def cli(msg):
        sys.stderr.write("%s\n" % msg)


class Shell:
    def __init__(self, cwd=None, logpath=None):
        self.cwd = cwd
        self.shell = True
        self.redirect = open(os.devnull if not logpath else logpath, "wr+")

    def __del__(self):
        self.redirect.close()

    def cd(self, cwd):
        Log.debug("cd %s" % cwd)
        self.cwd = cwd

    def truncate(self):
        self.redirect.truncate(0)

    def read_logs(self):
        self.redirect.seek(0)
        return self.redirect.read()

    def check_call(self, cmd):
        status = self.call(cmd)
        if status:
            raise Exception("Error running command %s. status=%s"
                            % (cmd, status))

    def call(self, cmd):
        if isinstance(cmd, list):
            return self._calls(cmd)

        return self._call(cmd)

    def ssh(self, hostname, cmd, id_rsa=None):
        flags = "" if not id_rsa else "-i " + id_rsa
        return self.call("timeout %s ssh %s root@%s \"%s\"" %
                            (SSH_TIMEOUT_S, flags, hostname, cmd))

    def scp(self, hostname, src, dest, id_rsa=None):
        flags = "" if not id_rsa else "-i " + id_rsa
        return self.call("timeout %s scp %s %s root@%s:%s" %
                            (SSH_TIMEOUT_S, flags, src, hostname, dest))

    def output(self, cmd, cwd=None):
        Log.debug("%s> %s" % (cwd, cmd))
        return subprocess.check_output(shlex.split(cmd), cwd=self.cwd)

    def _calls(self, cmds):
        Log.debug("Running commands. %s" % cmds)
        for c in cmds:
            status = self.call(c)
            if status:
                Log.error("Commands failed with %s" % status)
                return status
        return 0

    def _call(self, cmd):
        if not self.shell:
            cmd = shlex.split(cmd)

        Log.debug("%s> %s" % (self.cwd, cmd))

        status = subprocess.call(cmd, cwd=self.cwd, shell=self.shell,
                                 stdout=self.redirect, stderr=self.redirect)

        Log.debug("return %s" % status)
        return status


# ..............................................................................
# Server role
# ..............................................................................

class TestServer:
    def __init__(self, port, scratchdir):
        self.port = port
        self.scratchdir = scratchdir
        self.shell = Shell()
        self.rpc = None
        self.pidf = None

        self.shell.check_call("mkdir -p %s" % self.scratchdir)
        self._process_lock()

    def __del__(self):
        if self.pidf:
            self.pidf.close()

    def init(self):
        Log.debug("Starting xmlrpc server on port %s" % self.port)
        self.rpc = IPv6SimpleXMLRPCServer(("", self.port))
        self.rpc.register_instance(Handlers(self.scratchdir))

    def serve(self):
        (status, _) = failsafe(self.rpc.serve_forever)
        Log.cli("== End ==")

    def _process_lock(self):
        pid_filename = os.path.basename(__file__).replace("/", "-")
        pid_filepath = "%s/%s.pid" % (self.scratchdir, pid_filename)
        self.pidf = open(pid_filepath, "w")
        try:
            fcntl.lockf(self.pidf, fcntl.LOCK_EX | fcntl.LOCK_NB)
            # We have the lock, kick anybody listening on this port
            self.shell.call("kill $(lsof -t -i:%s)" % self.port)
        except IOError:
            Log.error("Another process instance is running")
            sys.exit(0)

#
# Server Handler
#


handler_lock = threading.Lock()
handler_serving_since = Timer()


def synchronized(func):
    def decorator(*args, **kws):
        handler_lock.acquire()
        h = args[0]
        try:
            h.shell.truncate()
            ret = func(*args, **kws)
            return ret
        except Exception() as err:
            Log.error(str(err))
            Log.error(decode(h._log_content()))
            raise
        finally:
            handler_lock.release()
            handler_serving_since.reset()

    return decorator


class Handlers:
    def __init__(self, scratchdir):
        self.client_id = None
        self.scratchdir = scratchdir
        self.gluster_root = "%s/glusterfs" % self.scratchdir
        self.shell = Shell(logpath="%s/test-handlers.log" % self.scratchdir)

    def hello(self, id):
        if not handler_lock.acquire(False):
            return False
        try:
            return self._hello_locked(id)
        finally:
            handler_lock.release()

    def _hello_locked(self, id):
        if handler_serving_since.elapsed_s() > CLIENT_TIMEOUT_S:
            Log.debug("Disconnected client %s" % self.client_id)
            self.client_id = None

        if not self.client_id:
            self.client_id = id
            handler_serving_since.reset()
            return True

        return (id == self.client_id)

    @synchronized
    def ping(self, id=None):
        if id:
            return id == self.client_id
        return True

    @synchronized
    def bye(self, id):
        assert id == self.client_id
        self.client_id = None
        handler_serving_since.reset()
        return True

    @synchronized
    def cleanup(self, id):
        assert id == self.client_id
        self.shell.cd(self.gluster_root)
        self.shell.check_call("PATH=.:$PATH; sudo ./clean_gfs_devserver.sh")
        return True

    @synchronized
    def copy(self, id, name, content):
        with open("%s/%s" % (self.scratchdir, name), "w+") as f:
            f.write(decode(content))
        return True

    @synchronized
    def copygzip(self, id, content):
        assert id == self.client_id
        gzipfile = "%s/tmp.tar.gz" % self.scratchdir
        tarfile = "%s/tmp.tar" % self.scratchdir
        self.shell.check_call("rm -f %s" % gzipfile)
        self.shell.check_call("rm -f %s" % tarfile)
        write_to_file(gzipfile, decode(content))

        self.shell.cd(self.scratchdir)
        self.shell.check_call("rm -r -f %s" % self.gluster_root)
        self.shell.check_call("mkdir -p %s" % self.gluster_root)

        self.shell.cd(self.gluster_root)
        cmds = [
            "gunzip -f -q %s" % gzipfile,
            "tar -xvf %s" % tarfile
        ]
        return self.shell.call(cmds) == 0

    @synchronized
    def build(self, id, asan=False):
        assert id == self.client_id
        self.shell.cd(self.gluster_root)
        self.shell.call("make clean")
        env = "ASAN_ENABLED=1" if asan else ""
        return self.shell.call(
		"%s ./extras/distributed-testing/distributed-test-build.sh" % env) == 0

    @synchronized
    def install(self, id):
        assert id == self.client_id
        self.shell.cd(self.gluster_root)
        return self.shell.call("make install") == 0

    @synchronized
    def prove(self, id, test, timeout, valgrind=False, asan_noleaks=True):
        assert id == self.client_id
        self.shell.cd(self.gluster_root)
        env = "DEBUG=1 "
        if valgrind:
            cmd = "valgrind"
            cmd += " --tool=memcheck --leak-check=full --track-origins=yes"
            cmd += " --show-leak-kinds=all -v prove -v"
        elif asan_noleaks:
            cmd = "prove -v"
            env += "ASAN_OPTIONS=detect_leaks=0 "
        else:
            cmd = "prove -v"

        status = self.shell.call(
		"%s timeout %s %s %s" % (env, timeout, cmd, test))

        if status != 0:
            return (False, self._log_content())
        return (True, "")

    def _log_content(self):
        return encode(self.shell.read_logs())

# ..............................................................................
# Cli role
# ..............................................................................


class RPCConnection((threading.Thread)):
    def __init__(self, host, port, path, cb):
        threading.Thread.__init__(self)
        self.host = host
        self.port = port
        self.path = path
        self.shell = Shell()
        self.cb = cb
        self.stop = False
        self.proxy = None
        self.logid = "%s:%s" % (self.host, self.port)

    def connect(self):
        (status, ret) = failsafe(self._connect)
        return (status and ret)

    def _connect(self):
        url = "http://%s:%s" % (self.host, self.port)
        self.proxy = xmlrpclib.ServerProxy(url, transport=IPv6Transport())
        return self.proxy.hello(self.cb.id)

    def disconnect(self):
        self.stop = True

    def ping(self):
        return self.proxy.ping()

    def init(self):
        return self._copy() and self._compile_and_install()

    def run(self):
        (status, ret) = failsafe(self.init)
        if not status:
            self.cb.note_lost_connection(self)
            return
        elif not ret:
            self.cb.note_setup_failed(self)
            return

        while not self.stop:
            (status, ret) = failsafe(self._run)
            if not status or not ret:
                self.cb.note_lost_connection(self)
                break
            time.sleep(0)

        failsafe(self.proxy.bye, (self.cb.id,))
        Log.debug("%s connection thread stopped" % self.host)

    def _run(self):
        test = self.cb.next_test()
        (status, _) = failsafe(self._execute_next, (test,))
        if not status:
            self.cb.note_retry(test)
            return False
        return True

    def _execute_next(self, test):
        if not test:
            time.sleep(1)
            return

        (status, error) = self.proxy.prove(self.cb.id, test,
                                           self.cb.test_timeout,
                                           self.cb.valgrind,
                                           self.cb.asan_noleaks)
        if status:
            self.cb.note_done(test)
        else:
            self.cb.note_error(test, error)

    def _compile_and_install(self):
        Log.debug("<%s> Build " % self.logid)
        asan = self.cb.asan or self.cb.asan_noleaks
        return (self.proxy.build(self.cb.id, asan) and
                self.proxy.install(self.cb.id))

    def _copy(self):
        return self._copy_gzip()

    def _copy_gzip(self):
        Log.cli("<%s> copying and compiling %s to remote" %
                 (self.logid, self.path))
        data = encode(get_file_content(patch_file()))
        Log.debug("GZIP size = %s B" % len(data))
        return self.proxy.copygzip(self.cb.id, data)


class RPCConnectionPool:
    def __init__(self, gluster_path, hosts, n, id_rsa):
        self.gluster_path = gluster_path
        self.hosts = hosts
        self.conns = []
        self.faulty = []
        self.n = int(len(hosts) / 2) + 1 if not n else n
        self.id_rsa = id_rsa
        self.stop = False
        self.scanner = threading.Thread(target=self._scan_hosts_loop)
        self.kicker = threading.Thread(target=self._kick_hosts_loop)
        self.shell = Shell()
        self.since_start = Timer()

        self.shell.check_call("rm -f %s" % patch_file())
        self.shell.check_call("tar -zcvf %s ." % patch_file())
        self.id = md5.new(get_file_content(patch_file())).hexdigest()
        Log.cli("client UID %s" % self.id)
        Log.cli("patch UID %s" % PATCH_FILE_UID)

    def __del__(self):
        self.shell.check_call("rm -f %s" % patch_file())

    def pool_status(self):
        elapsed_m = int(self.since_start.elapsed_s() / 60)
        return "%s/%s connected, %smin elapsed" % (len(self.conns), self.n,
                                                   elapsed_m)

    def connect(self):
        Log.debug("Starting scanner")
        self.scanner.start()
        self.kicker.start()

    def disconnect(self):
        self.stop = True
        for conn in self.conns:
            conn.disconnect()

    def note_lost_connection(self, conn):
        Log.cli("lost connection to %s" % conn.host)
        self.conns.remove(conn)
        self.hosts.append((conn.host, conn.port))

    def note_setup_failed(self, conn):
        Log.error("Setup failed on %s:%s" % (conn.host, conn.port))
        self.conns.remove(conn)
        self.faulty.append((conn.host, conn.port))

    def _scan_hosts_loop(self):
        Log.debug("Scanner thread started")
        while not self.stop:
            failsafe(self._scan_hosts)
            time.sleep(5)

    def _scan_hosts(self):
        if len(self.hosts) == 0 and len(self.conns) == 0:
            Log.error("no more hosts available to loadbalance")
            sys.exit(1)

        for (host, port) in self.hosts:
            if (len(self.conns) >= self.n) or self.stop:
                break
            self._scan_host(host, port)

    def _scan_host(self, host, port):
        Log.debug("scanning %s:%s" % (host, port))
        c = RPCConnection(host, port, self.gluster_path, self)
        (status, result) = failsafe(c.connect)
        if status and result:
            self.hosts.remove((host, port))
            Log.debug("Connected to %s:%s" % (host, port))
            self.conns.append(c)
            c.start()
            Log.debug("%s / %s connected" % (len(self.conns), self.n))
        else:
            Log.debug("Failed to connect to %s:%s" % (host, port))

    def _kick_hosts_loop(self):
        Log.debug("Kick thread started")
        while not self.stop:
            time.sleep(10)
            failsafe(self._kick_hosts)

        Log.debug("Kick thread stopped")

    def _is_pingable(self, host, port):
        c = RPCConnection(host, port, self.gluster_path, self)
        failsafe(c.connect)
        (status, result) = failsafe(c.ping)
        return status and result

    def _kick_hosts(self):
        # Do not kick hosts if we have the optimal number of connections
        if (len(self.conns) >= self.n) or self.stop:
            Log.debug("Skip kicking hosts")
            return

        # Check and if dead kick all hosts
        for (host, port) in self.hosts:
            if self.stop:
                Log.debug("Break kicking hosts")
                break

            if self._is_pingable(host, port):
                Log.debug("Host=%s is alive. Won't kick" % host)
                continue

            Log.debug("Kicking %s" % host)
            mypath = sys.argv[0]
            myname = os.path.basename(mypath)
            destpath = "/tmp/%s" % myname
            sh = Shell()
            sh.scp(host, mypath, destpath, self.id_rsa)
            sh.ssh(host, "nohup %s --server &>> %s.log &" %
                         (destpath, destpath), self.id_rsa)

    def join(self):
        self.scanner.join()
        self.kicker.join()
        for c in self.conns:
            c.join()


# ..............................................................................
# test role
# ..............................................................................

class TestRunner(RPCConnectionPool):
    def __init__(self, gluster_path, hosts, n, tests, flaky_tests, valgrind,
                 asan, asan_noleaks, id_rsa, test_timeout):
        RPCConnectionPool.__init__(self, gluster_path, self._parse_hosts(hosts),
                                   n, id_rsa)
        self.flaky_tests = flaky_tests.split(" ")
        self.pending = []
        self.done = []
        self.error = []
        self.retry = {}
        self.error_logs = []
        self.stats_timer = Timer()
        self.valgrind = valgrind
        self.asan = asan
        self.asan_noleaks = asan_noleaks
        self.test_timeout = test_timeout

        self.tests = self._get_tests(tests)

        Log.debug("tests: %s" % self.tests)

    def _get_tests(self, tests):
        if not tests or tests == "all":
            return self._not_flaky(self._all())
        elif tests == "flaky":
            return self.flaky_tests
        else:
            return self._not_flaky(tests.strip().split(" "))

    def run(self):
        self.connect()
        self.join()
        return len(self.error)

    def _pretty_print(self, data):
        if isinstance(data, list):
            str = ""
            for i in data:
                str = "%s %s" % (str, i)
            return str
        return "%s" % data

    def print_result(self):
        Log.cli("== RESULTS ==")
        Log.cli("SUCCESS  : %s" % len(self.done))
        Log.cli("ERRORS   : %s" % len(self.error))
        Log.cli("== ERRORS ==")
        Log.cli(self._pretty_print(self.error))
        Log.cli("== LOGS ==")
        Log.cli(self._pretty_print(self.error_logs))
        Log.cli("== END ==")

    def next_test(self):
        if len(self.tests):
            test = self.tests.pop()
            self.pending.append(test)
            return test

        if not len(self.pending):
            self.disconnect()

        return None

    def _pct_completed(self):
        total = len(self.tests) + len(self.pending) + len(self.done)
        total += len(self.error)
        completed = len(self.done) + len(self.error)
        return 0 if not total else int(completed / total * 100)

    def note_done(self, test):
        Log.cli("%s PASS (%s%% done) (%s)" % (test, self._pct_completed(),
                                              self.pool_status()))
        self.pending.remove(test)
        self.done.append(test)
        if test in self.retry:
            del self.retry[test]

    def note_error(self, test, errstr):
        Log.cli("%s FAIL" % test)
        self.pending.remove(test)
        if test not in self.retry:
            self.retry[test] = 1

        if errstr:
            path = "%s/%s-%s.log" % ("/tmp", test.replace("/", "-"),
                                     self.retry[test])
            failsafe(write_to_file, (path, decode(errstr),))
            self.error_logs.append(path)

        if self.retry[test] < MAX_ATTEMPTS:
            self.retry[test] += 1
            Log.debug("retry test %s attempt %s" % (test, self.retry[test]))
            self.tests.append(test)
        else:
            Log.debug("giveup attempt test %s" % test)
            del self.retry[test]
            self.error.append(test)

    def note_retry(self, test):
        Log.cli("retry %s on another host" % test)
        self.pending.remove(test)
        self.tests.append(test)

    #
    # test classifications
    #
    def _all(self):
        return self._list_tests(["tests"], recursive=True)

    def _not_flaky(self, tests):
        for t in self.flaky_tests:
            if t in tests:
                tests.remove(t)
        return tests

    def _list_tests(self, prefixes, recursive=False, ignore_ifnotexist=False):
        tests = []
        for prefix in prefixes:
            real_path = "%s/%s" % (self.gluster_path, prefix)
            if not os.path.exists(real_path) and ignore_ifnotexist:
                continue
            for f in os.listdir(real_path):
                if os.path.isdir(real_path + "/" + f):
                    if recursive:
                        tests += self._list_tests([prefix + "/" + f], recursive)
                else:
                    if re.match(r".*\.t$", f):
                        tests += [prefix + "/" + f]
        return tests

    def _parse_hosts(self, hosts):
        ret = []
        for h in args.hosts.split(" "):
            ret.append((h, DEFAULT_PORT))
        Log.debug(ret)
        return ret

# ..............................................................................
# Roles entry point
# ..............................................................................


def run_as_server(args):
    if not args.server_path:
        Log.error("please provide server path")
        return 1

    server = TestServer(args.port, args.server_path)
    server.init()
    server.serve()
    return 0


def run_as_tester(args):
    Log.header("GLUSTER TEST CLI")

    Log.debug("args = %s" % args)

    tests = TestRunner(args.gluster_path, args.hosts, args.n, args.tests,
                       args.flaky_tests, valgrind=args.valgrind,
                       asan=args.asan, asan_noleaks=args.asan_noleaks,
                       id_rsa=args.id_rsa, test_timeout=args.test_timeout)
    result = tests.run()
    tests.print_result()
    return result

# ..............................................................................
# main
# ..............................................................................


def main(args):
    if args.v:
        Log.LOGLEVEL = LogLevel.DEBUG

    if args.server and args.tester:
        Log.error("Invalid arguments. More than one role specified")
        sys.exit(1)

    if args.server:
        sys.exit(run_as_server(args))
    elif args.tester:
        sys.exit(run_as_tester(args))
    else:
        Log.error("please specify a mode for CI")
        parser.print_help()
        sys.exit(1)


parser = argparse.ArgumentParser(description="Gluster CI")

# server role
parser.add_argument("--server", help="start server", action="store_true")
parser.add_argument("--server_path", help="server scratch space",
                    default="/tmp/gluster-test")
parser.add_argument("--host", help="server address to listen", default="")
parser.add_argument("--port", help="server port to listen",
                    type=int, default=DEFAULT_PORT)
# test role
parser.add_argument("--tester", help="start tester", action="store_true")
parser.add_argument("--valgrind", help="run tests under valgrind",
                    action="store_true")
parser.add_argument("--asan", help="test with asan enabled",
                    action="store_true")
parser.add_argument("--asan-noleaks", help="test with asan but no mem leaks",
                    action="store_true")
parser.add_argument("--tests", help="all/flaky/list of tests", default=None)
parser.add_argument("--flaky_tests", help="list of flaky tests", default=None)
parser.add_argument("--n", help="max number of machines to use", type=int,
                    default=0)
parser.add_argument("--hosts", help="list of worker machines")
parser.add_argument("--gluster_path", help="gluster path to test",
                    default=os.getcwd())
parser.add_argument("--id-rsa", help="private key to use for ssh",
                    default=None)
parser.add_argument("--test-timeout",
                    help="test timeout in sec (default 15min)",
                    default=TEST_TIMEOUT_S)
# general
parser.add_argument("-v", help="verbose", action="store_true")

args = parser.parse_args()

main(args)
