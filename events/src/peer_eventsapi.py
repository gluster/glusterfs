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
import os
import json
from errno import EEXIST
import fcntl
from errno import EACCES, EAGAIN
import signal

import requests
from prettytable import PrettyTable

from gluster.cliutils import (Cmd, execute, node_output_ok, node_output_notok,
                              sync_file_to_peers, GlusterCmdException,
                              output_error, execute_in_peers, runcli)
from events.utils import LockedOpen

from events.eventsapiconf import (WEBHOOKS_FILE_TO_SYNC,
                                  WEBHOOKS_FILE,
                                  DEFAULT_CONFIG_FILE,
                                  CUSTOM_CONFIG_FILE,
                                  CUSTOM_CONFIG_FILE_TO_SYNC,
                                  EVENTSD,
                                  CONFIG_KEYS,
                                  BOOL_CONFIGS,
                                  INT_CONFIGS,
                                  PID_FILE,
                                  RESTART_CONFIGS)


def file_content_overwrite(fname, data):
    with open(fname + ".tmp", "w") as f:
        f.write(json.dumps(data))

    os.rename(fname + ".tmp", fname)


def create_custom_config_file_if_not_exists():
    mkdirp(os.path.dirname(CUSTOM_CONFIG_FILE))
    if not os.path.exists(CUSTOM_CONFIG_FILE):
        with open(CUSTOM_CONFIG_FILE, "w") as f:
            f.write("{}")


def create_webhooks_file_if_not_exists():
    mkdirp(os.path.dirname(WEBHOOKS_FILE))
    if not os.path.exists(WEBHOOKS_FILE):
        with open(WEBHOOKS_FILE, "w") as f:
            f.write("{}")


def boolify(value):
    val = False
    if value.lower() in ["enabled", "true", "on", "yes"]:
        val = True
    return val


def mkdirp(path, exit_on_err=False, logger=None):
    """
    Try creating required directory structure
    ignore EEXIST and raise exception for rest of the errors.
    Print error in stderr and exit
    """
    try:
        os.makedirs(path)
    except (OSError, IOError) as e:
        if e.errno == EEXIST and os.path.isdir(path):
            pass
        else:
            output_error("Fail to create dir %s: %s" % (path, e))


def is_active():
    state = False
    try:
        with open(PID_FILE, "a+") as f:
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            state = False
    except (IOError, OSError) as e:
        if e.errno in (EACCES, EAGAIN):
            # cannot grab. so, process still running..move on
            state = True
        else:
            state = False
    return state


def reload_service():
    pid = None
    if is_active():
        with open(PID_FILE) as f:
            try:
                pid = int(f.read().strip())
            except ValueError:
                pid = None
        if pid is not None:
            os.kill(pid, signal.SIGUSR2)

    return (0, "", "")


def sync_to_peers():
    if os.path.exists(WEBHOOKS_FILE):
        try:
            sync_file_to_peers(WEBHOOKS_FILE_TO_SYNC)
        except GlusterCmdException as e:
            output_error("Failed to sync Webhooks file: [Error: {0}]"
                         "{1}".format(e[0], e[2]))

    if os.path.exists(CUSTOM_CONFIG_FILE):
        try:
            sync_file_to_peers(CUSTOM_CONFIG_FILE_TO_SYNC)
        except GlusterCmdException as e:
            output_error("Failed to sync Config file: [Error: {0}]"
                         "{1}".format(e[0], e[2]))

    out = execute_in_peers("node-reload")
    table = PrettyTable(["NODE", "NODE STATUS", "SYNC STATUS"])
    table.align["NODE STATUS"] = "r"
    table.align["SYNC STATUS"] = "r"

    for p in out:
        table.add_row([p.hostname,
                       "UP" if p.node_up else "DOWN",
                       "OK" if p.ok else "NOT OK: {0}".format(
                           p.error)])

    print (table)


def node_output_handle(resp):
    rc, out, err = resp
    if rc == 0:
        node_output_ok(out)
    else:
        node_output_notok(err)


def action_handle(action):
    out = execute_in_peers("node-" + action)
    column_name = action.upper()
    if action == "status":
        column_name = EVENTSD.upper()

    table = PrettyTable(["NODE", "NODE STATUS", column_name + " STATUS"])
    table.align["NODE STATUS"] = "r"
    table.align[column_name + " STATUS"] = "r"

    for p in out:
        status_col_val = "OK" if p.ok else "NOT OK: {0}".format(
            p.error)
        if action == "status":
            status_col_val = "DOWN"
            if p.ok:
                status_col_val = p.output

        table.add_row([p.hostname,
                       "UP" if p.node_up else "DOWN",
                       status_col_val])

    print (table)


class NodeReload(Cmd):
    name = "node-reload"

    def run(self, args):
        node_output_handle(reload_service())


class ReloadCmd(Cmd):
    name = "reload"

    def run(self, args):
        action_handle("reload")


class NodeStatus(Cmd):
    name = "node-status"

    def run(self, args):
        node_output_ok("UP" if is_active() else "DOWN")


class StatusCmd(Cmd):
    name = "status"

    def run(self, args):
        webhooks = {}
        if os.path.exists(WEBHOOKS_FILE):
            webhooks = json.load(open(WEBHOOKS_FILE))

        print ("Webhooks: " + ("" if webhooks else "None"))
        for w in webhooks:
            print (w)

        print ()
        action_handle("status")


class WebhookAddCmd(Cmd):
    name = "webhook-add"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")
        parser.add_argument("--bearer_token", "-t", help="Bearer Token",
                            default="")

    def run(self, args):
        create_webhooks_file_if_not_exists()

        with LockedOpen(WEBHOOKS_FILE, 'r+'):
            data = json.load(open(WEBHOOKS_FILE))
            if data.get(args.url, None) is not None:
                output_error("Webhook already exists")

            data[args.url] = args.bearer_token
            file_content_overwrite(WEBHOOKS_FILE, data)

        sync_to_peers()


class WebhookModCmd(Cmd):
    name = "webhook-mod"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")
        parser.add_argument("--bearer_token", "-t", help="Bearer Token",
                            default="")

    def run(self, args):
        create_webhooks_file_if_not_exists()

        with LockedOpen(WEBHOOKS_FILE, 'r+'):
            data = json.load(open(WEBHOOKS_FILE))
            if data.get(args.url, None) is None:
                output_error("Webhook does not exists")

            data[args.url] = args.bearer_token
            file_content_overwrite(WEBHOOKS_FILE, data)

        sync_to_peers()


class WebhookDelCmd(Cmd):
    name = "webhook-del"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")

    def run(self, args):
        create_webhooks_file_if_not_exists()

        with LockedOpen(WEBHOOKS_FILE, 'r+'):
            data = json.load(open(WEBHOOKS_FILE))
            if data.get(args.url, None) is None:
                output_error("Webhook does not exists")

            del data[args.url]
            file_content_overwrite(WEBHOOKS_FILE, data)

        sync_to_peers()


class NodeWebhookTestCmd(Cmd):
    name = "node-webhook-test"

    def args(self, parser):
        parser.add_argument("url")
        parser.add_argument("bearer_token")

    def run(self, args):
        http_headers = {}
        if args.bearer_token != ".":
            http_headers["Authorization"] = "Bearer " + args.bearer_token

        try:
            resp = requests.post(args.url, headers=http_headers)
        except requests.ConnectionError as e:
            node_output_notok("{0}".format(e))

        if resp.status_code != 200:
            node_output_notok("{0}".format(resp.status_code))

        node_output_ok()


class WebhookTestCmd(Cmd):
    name = "webhook-test"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")
        parser.add_argument("--bearer_token", "-t", help="Bearer Token")

    def run(self, args):
        url = args.url
        bearer_token = args.bearer_token
        if not args.url:
            url = "."
        if not args.bearer_token:
            bearer_token = "."

        out = execute_in_peers("node-webhook-test", [url, bearer_token])

        table = PrettyTable(["NODE", "NODE STATUS", "WEBHOOK STATUS"])
        table.align["NODE STATUS"] = "r"
        table.align["WEBHOOK STATUS"] = "r"

        for p in out:
            table.add_row([p.hostname,
                           "UP" if p.node_up else "DOWN",
                           "OK" if p.ok else "NOT OK: {0}".format(
                               p.error)])

        print (table)


class ConfigGetCmd(Cmd):
    name = "config-get"

    def args(self, parser):
        parser.add_argument("--name", help="Config Name")

    def run(self, args):
        data = json.load(open(DEFAULT_CONFIG_FILE))
        if os.path.exists(CUSTOM_CONFIG_FILE):
            data.update(json.load(open(CUSTOM_CONFIG_FILE)))

        if args.name is not None and args.name not in CONFIG_KEYS:
            output_error("Invalid Config item")

        table = PrettyTable(["NAME", "VALUE"])
        if args.name is None:
            for k, v in data.items():
                table.add_row([k, v])
        else:
            table.add_row([args.name, data[args.name]])

        print (table)


def read_file_content_json(fname):
    content = "{}"
    with open(fname) as f:
        content = f.read()
        if content.strip() == "":
            content = "{}"

    return json.loads(content)


class ConfigSetCmd(Cmd):
    name = "config-set"

    def args(self, parser):
        parser.add_argument("name", help="Config Name")
        parser.add_argument("value", help="Config Value")

    def run(self, args):
        if args.name not in CONFIG_KEYS:
            output_error("Invalid Config item")

        create_custom_config_file_if_not_exists()

        with LockedOpen(CUSTOM_CONFIG_FILE, 'r+'):
            data = json.load(open(DEFAULT_CONFIG_FILE))
            if os.path.exists(CUSTOM_CONFIG_FILE):
                config_json = read_file_content_json(CUSTOM_CONFIG_FILE)
                data.update(config_json)

            # Do Nothing if same as previous value
            if data[args.name] == args.value:
                return

            # TODO: Validate Value
            new_data = read_file_content_json(CUSTOM_CONFIG_FILE)

            v = args.value
            if args.name in BOOL_CONFIGS:
                v = boolify(args.value)

            if args.name in INT_CONFIGS:
                v = int(args.value)

            new_data[args.name] = v
            file_content_overwrite(CUSTOM_CONFIG_FILE, new_data)

            # If any value changed which requires restart of REST server
            restart = False
            if args.name in RESTART_CONFIGS:
                restart = True

            sync_to_peers()
            if restart:
                print ("\nRestart glustereventsd in all nodes")


class ConfigResetCmd(Cmd):
    name = "config-reset"

    def args(self, parser):
        parser.add_argument("name", help="Config Name or all")

    def run(self, args):
        create_custom_config_file_if_not_exists()

        with LockedOpen(CUSTOM_CONFIG_FILE, 'r+'):
            changed_keys = []
            data = {}
            if os.path.exists(CUSTOM_CONFIG_FILE):
                data = read_file_content_json(CUSTOM_CONFIG_FILE)

            if not data:
                return

            if args.name.lower() == "all":
                for k, v in data.items():
                    changed_keys.append(k)

                # Reset all keys
                file_content_overwrite(CUSTOM_CONFIG_FILE, {})
            else:
                changed_keys.append(args.name)
                del data[args.name]
                file_content_overwrite(CUSTOM_CONFIG_FILE, data)

            # If any value changed which requires restart of REST server
            restart = False
            for key in changed_keys:
                if key in RESTART_CONFIGS:
                    restart = True
                    break

            sync_to_peers()
            if restart:
                print ("\nRestart glustereventsd in all nodes")


class SyncCmd(Cmd):
    name = "sync"

    def run(self, args):
        sync_to_peers()


if __name__ == "__main__":
    runcli()
