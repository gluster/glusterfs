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
import sys

import requests
from prettytable import PrettyTable

from gluster.cliutils import (Cmd, node_output_ok, node_output_notok,
                              sync_file_to_peers, GlusterCmdException,
                              output_error, execute_in_peers, runcli,
                              set_common_args_func)
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
                                  RESTART_CONFIGS,
                                  ERROR_INVALID_CONFIG,
                                  ERROR_WEBHOOK_NOT_EXISTS,
                                  ERROR_CONFIG_SYNC_FAILED,
                                  ERROR_WEBHOOK_ALREADY_EXISTS,
                                  ERROR_PARTIAL_SUCCESS,
                                  ERROR_ALL_NODES_STATUS_NOT_OK,
                                  ERROR_SAME_CONFIG,
                                  ERROR_WEBHOOK_SYNC_FAILED)


def handle_output_error(err, errcode=1, json_output=False):
    if json_output:
        print (json.dumps({
            "output": "",
            "error": err
            }))
        sys.exit(errcode)
    else:
        output_error(err, errcode)


def file_content_overwrite(fname, data):
    with open(fname + ".tmp", "w") as f:
        f.write(json.dumps(data))

    os.rename(fname + ".tmp", fname)


def create_custom_config_file_if_not_exists(args):
    try:
        config_dir = os.path.dirname(CUSTOM_CONFIG_FILE)
        mkdirp(config_dir)
    except OSError as e:
        handle_output_error("Failed to create dir %s: %s" % (config_dir, e),
                            json_output=args.json)

    if not os.path.exists(CUSTOM_CONFIG_FILE):
        with open(CUSTOM_CONFIG_FILE, "w") as f:
            f.write("{}")


def create_webhooks_file_if_not_exists(args):
    try:
        webhooks_dir = os.path.dirname(WEBHOOKS_FILE)
        mkdirp(webhooks_dir)
    except OSError as e:
        handle_output_error("Failed to create dir %s: %s" % (webhooks_dir, e),
                            json_output=args.json)

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
    except OSError as e:
        if e.errno != EEXIST or not os.path.isdir(path):
            raise


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


def rows_to_json(json_out, column_name, rows):
    num_ok_rows = 0
    for row in rows:
        num_ok_rows += 1 if row.ok else 0
        json_out.append({
            "node": row.hostname,
            "node_status": "UP" if row.node_up else "DOWN",
            column_name: "OK" if row.ok else "NOT OK",
            "error": row.error
        })
    return num_ok_rows


def rows_to_table(table, rows):
    num_ok_rows = 0
    for row in rows:
        num_ok_rows += 1 if row.ok else 0
        table.add_row([row.hostname,
                       "UP" if row.node_up else "DOWN",
                       "OK" if row.ok else "NOT OK: {1}".format(
                           row.error)])
    return num_ok_rows


def sync_to_peers(args):
    if os.path.exists(WEBHOOKS_FILE):
        try:
            sync_file_to_peers(WEBHOOKS_FILE_TO_SYNC)
        except GlusterCmdException as e:
            handle_output_error("Failed to sync Webhooks file: [Error: {0}]"
                                "{1}".format(e[0], e[2]),
                                errcode=ERROR_WEBHOOK_SYNC_FAILED,
                                json_output=args.json)

    if os.path.exists(CUSTOM_CONFIG_FILE):
        try:
            sync_file_to_peers(CUSTOM_CONFIG_FILE_TO_SYNC)
        except GlusterCmdException as e:
            handle_output_error("Failed to sync Config file: [Error: {0}]"
                                "{1}".format(e[0], e[2]),
                                errcode=ERROR_CONFIG_SYNC_FAILED,
                                json_output=args.json)

    out = execute_in_peers("node-reload")
    if not args.json:
        table = PrettyTable(["NODE", "NODE STATUS", "SYNC STATUS"])
        table.align["NODE STATUS"] = "r"
        table.align["SYNC STATUS"] = "r"

    json_out = []
    if args.json:
        num_ok_rows = rows_to_json(json_out, "sync_status", out)
    else:
        num_ok_rows = rows_to_table(table, out)

    ret = 0
    if num_ok_rows == 0:
        ret = ERROR_ALL_NODES_STATUS_NOT_OK
    elif num_ok_rows != len(out):
        ret = ERROR_PARTIAL_SUCCESS

    if args.json:
        print (json.dumps({
            "output": json_out,
            "error": ""
        }))
    else:
        print (table)

    # If sync status is not ok for any node set error code as partial success
    sys.exit(ret)


def node_output_handle(resp):
    rc, out, err = resp
    if rc == 0:
        node_output_ok(out)
    else:
        node_output_notok(err)


def action_handle(action, json_output=False):
    out = execute_in_peers("node-" + action)
    column_name = action.upper()
    if action == "status":
        column_name = EVENTSD.upper()

    if not json_output:
        table = PrettyTable(["NODE", "NODE STATUS", column_name + " STATUS"])
        table.align["NODE STATUS"] = "r"
        table.align[column_name + " STATUS"] = "r"

    json_out = []
    if json_output:
        rows_to_json(json_out, column_name.lower() + "_status", out)
    else:
        rows_to_table(table, out)

    return json_out if json_output else table


class NodeReload(Cmd):
    name = "node-reload"

    def run(self, args):
        node_output_handle(reload_service())


class ReloadCmd(Cmd):
    name = "reload"

    def run(self, args):
        out = action_handle("reload", args.json)
        if args.json:
            print (json.dumps({
                "output": out,
                "error": ""
            }))
        else:
            print (out)


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

        json_out = {"webhooks": [], "data": []}
        if args.json:
            json_out["webhooks"] = webhooks.keys()
        else:
            print ("Webhooks: " + ("" if webhooks else "None"))
            for w in webhooks:
                print (w)

            print ()

        out = action_handle("status", args.json)
        if args.json:
            json_out["data"] = out
            print (json.dumps({
                "output": json_out,
                "error": ""
            }))
        else:
            print (out)


class WebhookAddCmd(Cmd):
    name = "webhook-add"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")
        parser.add_argument("--bearer_token", "-t", help="Bearer Token",
                            default="")

    def run(self, args):
        create_webhooks_file_if_not_exists(args)

        with LockedOpen(WEBHOOKS_FILE, 'r+'):
            data = json.load(open(WEBHOOKS_FILE))
            if data.get(args.url, None) is not None:
                handle_output_error("Webhook already exists",
                                    errcode=ERROR_WEBHOOK_ALREADY_EXISTS,
                                    json_output=args.json)

            data[args.url] = args.bearer_token
            file_content_overwrite(WEBHOOKS_FILE, data)

        sync_to_peers(args)


class WebhookModCmd(Cmd):
    name = "webhook-mod"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")
        parser.add_argument("--bearer_token", "-t", help="Bearer Token",
                            default="")

    def run(self, args):
        create_webhooks_file_if_not_exists(args)

        with LockedOpen(WEBHOOKS_FILE, 'r+'):
            data = json.load(open(WEBHOOKS_FILE))
            if data.get(args.url, None) is None:
                handle_output_error("Webhook does not exists",
                                    errcode=ERROR_WEBHOOK_NOT_EXISTS,
                                    json_output=args.json)

            data[args.url] = args.bearer_token
            file_content_overwrite(WEBHOOKS_FILE, data)

        sync_to_peers(args)


class WebhookDelCmd(Cmd):
    name = "webhook-del"

    def args(self, parser):
        parser.add_argument("url", help="URL of Webhook")

    def run(self, args):
        create_webhooks_file_if_not_exists(args)

        with LockedOpen(WEBHOOKS_FILE, 'r+'):
            data = json.load(open(WEBHOOKS_FILE))
            if data.get(args.url, None) is None:
                handle_output_error("Webhook does not exists",
                                    errcode=ERROR_WEBHOOK_NOT_EXISTS,
                                    json_output=args.json)

            del data[args.url]
            file_content_overwrite(WEBHOOKS_FILE, data)

        sync_to_peers(args)


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

        if not args.json:
            table = PrettyTable(["NODE", "NODE STATUS", "WEBHOOK STATUS"])
            table.align["NODE STATUS"] = "r"
            table.align["WEBHOOK STATUS"] = "r"

        num_ok_rows = 0
        json_out = []
        if args.json:
            num_ok_rows = rows_to_json(json_out, "webhook_status", out)
        else:
            num_ok_rows = rows_to_table(table, out)

        ret = 0
        if num_ok_rows == 0:
            ret = ERROR_ALL_NODES_STATUS_NOT_OK
        elif num_ok_rows != len(out):
            ret = ERROR_PARTIAL_SUCCESS

        if args.json:
            print (json.dumps({
                "output": json_out,
                "error": ""
            }))
        else:
            print (table)

        sys.exit(ret)


class ConfigGetCmd(Cmd):
    name = "config-get"

    def args(self, parser):
        parser.add_argument("--name", help="Config Name")

    def run(self, args):
        data = json.load(open(DEFAULT_CONFIG_FILE))
        if os.path.exists(CUSTOM_CONFIG_FILE):
            data.update(json.load(open(CUSTOM_CONFIG_FILE)))

        if args.name is not None and args.name not in CONFIG_KEYS:
            handle_output_error("Invalid Config item",
                                errcode=ERROR_INVALID_CONFIG,
                                json_output=args.json)

        if args.json:
            json_out = {}
            if args.name is None:
                json_out = data
            else:
                json_out[args.name] = data[args.name]

            print (json.dumps({
                "output": json_out,
                "error": ""
            }))
        else:
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
            handle_output_error("Invalid Config item",
                                errcode=ERROR_INVALID_CONFIG,
                                json_output=args.json)

        create_custom_config_file_if_not_exists(args)

        with LockedOpen(CUSTOM_CONFIG_FILE, 'r+'):
            data = json.load(open(DEFAULT_CONFIG_FILE))
            if os.path.exists(CUSTOM_CONFIG_FILE):
                config_json = read_file_content_json(CUSTOM_CONFIG_FILE)
                data.update(config_json)

            # Do Nothing if same as previous value
            if data[args.name] == args.value:
                handle_output_error("Config value not changed. Same config",
                                    errcode=ERROR_SAME_CONFIG,
                                    json_output=args.json)

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

            if restart:
                print ("\nRestart glustereventsd in all nodes")

            sync_to_peers(args)


class ConfigResetCmd(Cmd):
    name = "config-reset"

    def args(self, parser):
        parser.add_argument("name", help="Config Name or all")

    def run(self, args):
        create_custom_config_file_if_not_exists(args)

        with LockedOpen(CUSTOM_CONFIG_FILE, 'r+'):
            changed_keys = []
            data = {}
            if os.path.exists(CUSTOM_CONFIG_FILE):
                data = read_file_content_json(CUSTOM_CONFIG_FILE)

            # If No data available in custom config or, the specific config
            # item is not available in custom config
            if not data or \
               (args.name != "all" and data.get(args.name, None) is None):
                handle_output_error("Config value not reset. Already "
                                    "set to default value",
                                    errcode=ERROR_SAME_CONFIG,
                                    json_output=args.json)

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

            if restart:
                print ("\nRestart glustereventsd in all nodes")

            sync_to_peers(args)


class SyncCmd(Cmd):
    name = "sync"

    def run(self, args):
        sync_to_peers(args)


def common_args(parser):
    parser.add_argument("--json", help="JSON Output", action="store_true")


if __name__ == "__main__":
    set_common_args_func(common_args)
    runcli()
