#!/usr/bin/env python
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
import asyncore
import socket
import os
from multiprocessing import Process, Queue
import sys
import signal

from eventtypes import all_events
import handlers
import utils
from eventsapiconf import SERVER_ADDRESS
from utils import logger

# Global Queue, EventsHandler will add items to the queue
# and process_event will gets each item and handles it
events_queue = Queue()
events_server_pid = None


def process_event():
    """
    Seperate process which handles all the incoming events from Gluster
    processes.
    """
    while True:
        data = events_queue.get()
        logger.debug("EVENT: {0}".format(repr(data)))
        try:
            # Event Format <TIMESTAMP> <TYPE> <DETAIL>
            ts, key, value = data.split(" ", 2)
        except ValueError:
            logger.warn("Invalid Event Format {0}".format(data))
            continue

        data_dict = {}
        try:
            # Format key=value;key=value
            data_dict = dict(x.split('=') for x in value.split(';'))
        except ValueError:
            logger.warn("Unable to parse Event {0}".format(data))
            continue

        try:
            # Event Type to Function Map, Recieved event data will be in
            # the form <TIMESTAMP> <TYPE> <DETAIL>, Get Event name for the
            # recieved Type/Key and construct a function name starting with
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


def process_event_wrapper():
    try:
        process_event()
    except KeyboardInterrupt:
        return


class GlusterEventsHandler(asyncore.dispatcher_with_send):

    def handle_read(self):
        data = self.recv(8192)
        if data:
            events_queue.put(data)
            self.send(data)


class GlusterEventsServer(asyncore.dispatcher):

    def __init__(self):
        global events_server_pid
        asyncore.dispatcher.__init__(self)
        # Start the Events listener process which listens to
        # the global queue
        p = Process(target=process_event_wrapper)
        p.start()
        events_server_pid = p.pid

        # Create UNIX Domain Socket, bind to path
        self.create_socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.bind(SERVER_ADDRESS)
        self.listen(5)

    def handle_accept(self):
        pair = self.accept()
        if pair is not None:
            sock, addr = pair
            GlusterEventsHandler(sock)


def signal_handler_sigusr2(sig, frame):
    if events_server_pid is not None:
        os.kill(events_server_pid, signal.SIGUSR2)
    utils.load_all()


def init_event_server():
    utils.setup_logger()

    # Delete Socket file if Exists
    try:
        os.unlink(SERVER_ADDRESS)
    except OSError:
        if os.path.exists(SERVER_ADDRESS):
            print ("Failed to cleanup socket file {0}".format(SERVER_ADDRESS),
                   file=sys.stderr)
            sys.exit(1)

    utils.load_all()

    # Start the Eventing Server, UNIX DOMAIN SOCKET Server
    GlusterEventsServer()
    asyncore.loop()


def main():
    try:
        init_event_server()
    except KeyboardInterrupt:
        sys.exit(1)


if __name__ == "__main__":
    signal.signal(signal.SIGUSR2, signal_handler_sigusr2)
    main()
