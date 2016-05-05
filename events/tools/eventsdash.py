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

from argparse import ArgumentParser, RawDescriptionHelpFormatter
import logging
from datetime import datetime

from flask import Flask, request

app = Flask(__name__)
app.logger.disabled = True
log = logging.getLogger('werkzeug')
log.disabled = True


def human_time(ts):
    return datetime.fromtimestamp(float(ts)).strftime("%Y-%m-%d %H:%M:%S")


@app.route("/")
def home():
    return "OK"


@app.route("/listen", methods=["POST"])
def listen():
    data = request.json
    if data is None:
        return "OK"

    message = []
    for k, v in data.get("message", {}).items():
        message.append("{0}={1}".format(k, v))

    print ("{0:20s} {1:20s} {2:36} {3}".format(
        human_time(data.get("ts")),
        data.get("event"),
        data.get("nodeid"),
        " ".join(message)))

    return "OK"


def main():
    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter,
                            description=__doc__)
    parser.add_argument("--port", type=int, help="Port", default=9000)
    parser.add_argument("--debug", help="Run Server in debug mode",
                        action="store_true")
    args = parser.parse_args()

    print ("{0:20s} {1:20s} {2:36} {3}".format(
        "TIMESTAMP", "EVENT", "NODE ID", "MESSAGE"
    ))
    print ("{0:20s} {1:20s} {2:36} {3}".format(
        "-"*20, "-"*20, "-"*36, "-"*20
    ))
    if args.debug:
        app.debug = True

    app.run(host="0.0.0.0", port=args.port)


if __name__ == "__main__":
    main()
