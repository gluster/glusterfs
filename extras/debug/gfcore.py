#!/usr/bin/env python3

def launch():
    if len(sys.argv) < 3:
        sys.stderr.write("Syntax: {} <pid> <count> [<dir>]\n".format(os.path.basename(sys.argv[0])))
        sys.exit(1)

    pid = int(sys.argv[1])
    count = int(sys.argv[2])
    base = os.getcwd()
    if len(sys.argv) > 3:
        base = sys.argv[3]
    base = os.path.realpath(base)

    subprocess.run([
        "gdb", "-batch",
        "-p", str(pid),
        "-ex", "py arg_count = {}".format(count),
        "-ex", "py arg_dir = '{}'".format(base),
        "-x", __file__
    ])

class GFCore(object):
    def __init__(self, count, base):
        self.count = count
        self.base = base
        gdb.execute('set pagination off')
        gdb.execute('set gf_signal_on_assert = 1')
        gdb.events.stop.connect(self.gf_stop)

        self.cont()

    def cont(self, quit = False):
        if not(quit) and (self.count > 0):
            gdb.execute('continue')
        else:
            gdb.execute('set gf_signal_on_assert = 0')
            gdb.execute('quit')

    def gf_stop(self, event):
        quit = False

        if isinstance(event, gdb.SignalEvent):
            if event.stop_signal == 'SIGCONT':
                now = datetime.utcnow().isoformat()
                pid = gdb.selected_inferior().pid
                name = "{}/gfcore.{}.{}".format(self.base, pid, now)
                print("Generating coredump '{}'".format(name))
                gdb.execute('gcore {}'.format(name))
                self.count -= 1

            elif event.stop_signal == 'SIGINT':
                print("SIGINT received. Exiting")
                quit = True

            else:
                print("Ignoring signal {}".format(event.stop_signal))
        else:
            print("Unexpected event {}".format(type(event)))

        self.cont(quit)

# Module 'gdb' is not available when running outside gdb.
try:
    import gdb
    from datetime import datetime

    GFCore(arg_count, arg_dir)
except ModuleNotFoundError:
    import sys
    import os
    import subprocess

    try:
        launch()
    except KeyboardInterrupt:
        pass
