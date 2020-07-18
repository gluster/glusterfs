#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
#  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.
#

from __future__ import print_function
import sys
import signal
import threading
try:
    import socketserver
except ImportError:
    import SocketServer as socketserver
import socket
from argparse import ArgumentParser, RawDescriptionHelpFormatter

from eventtypes import all_events
import handlers
import utils
from eventsapiconf import SERVER_ADDRESSv4, SERVER_ADDRESSv6, PID_FILE
from eventsapiconf import AUTO_BOOL_ATTRIBUTES, AUTO_INT_ATTRIBUTES
from utils import logger, PidFile, PidFileLockFailed, boolify

# Subclass so that specifically IPv4 packets are captured
class UDPServerv4(socketserver.ThreadingUDPServer):
    address_family = socket.AF_INET

# Subclass so that specifically IPv6 packets are captured
class UDPServerv6(socketserver.ThreadingUDPServer):
    address_family = socket.AF_INET6

class GlusterEventsRequestHandler(socketserver.BaseRequestHandler):

    def handle(self):
        data = self.request[0].strip()
        if sys.version_info >= (3,):
            data = self.request[0].strip().decode("utf-8")

        logger.debug("EVENT: {0} from {1}".format(repr(data),
                                                  self.client_address[0]))
        try:
            # Event Format <TIMESTAMP> <TYPE> <DETAIL>
            ts, key, value = data.split(" ", 2)
        except ValueError:
            logger.warn("Invalid Event Format {0}".format(data))
            return

        data_dict = {}
        try:
            # Format key=value;key=value
            data_dict = dict(x.split('=') for x in value.split(';'))
        except ValueError:
            logger.warn("Unable to parse Event {0}".format(data))
            return

        for k, v in data_dict.items():
            try:
                if k in AUTO_BOOL_ATTRIBUTES:
                    data_dict[k] = boolify(v)
                if k in AUTO_INT_ATTRIBUTES:
                    data_dict[k] = int(v)
            except ValueError:
                # Auto Conversion failed, Retain the old value
                continue

        try:
            # Event Type to Function Map, Received event data will be in
            # the form <TIMESTAMP> <TYPE> <DETAIL>, Get Event name for the
            # received Type/Key and construct a function name starting with
            # handle_ For example: handle_event_volume_create
            func_name = "handle_" + all_events[int(key)].lower()
        except IndexError:
            # This type of Event is not handled?
            logger.warn("Unhandled Event: {0}".format(key))
            func_name = None

        if func_name is not None:
            # Get function from handlers module
            func = getattr(handlers, func_name, None)
            # If func is None, then handler unimplemented for that event.
            if func is not None:
                func(ts, int(key), data_dict)
            else:
                # Generic handler, broadcast whatever received
                handlers.generic_handler(ts, int(key), data_dict)


def signal_handler_sigusr2(sig, frame):
    utils.load_all()
    utils.restart_webhook_pool()


def UDP_server_thread(sock):
    sock.serve_forever()


def init_event_server():
    utils.setup_logger()
    utils.load_all()
    utils.init_webhook_pool()

    port = utils.get_config("port")
    if port is None:
        sys.stderr.write("Unable to get Port details from Config\n")
        sys.exit(1)

    # Creating the Eventing Server, UDP Server for IPv4 packets
    try:
        serverv4 = UDPServerv4((SERVER_ADDRESSv4, port),
                   GlusterEventsRequestHandler)
    except socket.error as e:
        sys.stderr.write("Failed to start Eventsd for IPv4: {0}\n".format(e))
        sys.exit(1)
    # Creating the Eventing Server, UDP Server for IPv6 packets
    try:
        serverv6 = UDPServerv6((SERVER_ADDRESSv6, port),
                   GlusterEventsRequestHandler)
    except socket.error as e:
        sys.stderr.write("Failed to start Eventsd for IPv6: {0}\n".format(e))
        sys.exit(1)
    server_thread1 = threading.Thread(target=UDP_server_thread,
                     args=(serverv4,))
    server_thread2 = threading.Thread(target=UDP_server_thread,
                     args=(serverv6,))
    server_thread1.start()
    server_thread2.start()


def get_args():
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description=__doc__)
    parser.add_argument("-p", "--pid-file", help="PID File",
                        default=PID_FILE)

    return parser.parse_args()


def main():
    args = get_args()
    try:
        with PidFile(args.pid_file):
            init_event_server()
    except PidFileLockFailed as e:
        sys.stderr.write("Failed to get lock for pid file({0}): {1}".format(
            args.pid_file, e))
    except KeyboardInterrupt:
        sys.exit(1)


if __name__ == "__main__":
    signal.signal(signal.SIGUSR2, signal_handler_sigusr2)
    main()
