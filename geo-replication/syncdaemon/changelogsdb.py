#
# Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import os
import sqlite3
from errno import ENOENT

conn = None
cursor = None


def db_commit():
    conn.commit()


def db_init(db_path):
    global conn, cursor
    # Remove Temp Db
    try:
        os.unlink(db_path)
        os.unlink(db_path + "-journal")
    except OSError as e:
        if e.errno != ENOENT:
            raise

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute("DROP TABLE IF EXISTS data")
    cursor.execute("DROP TABLE IF EXISTS meta")
    query = """CREATE TABLE IF NOT EXISTS data(
    gfid           VARCHAR(100) PRIMARY KEY ON CONFLICT IGNORE,
    changelog_time VARCHAR(100)
    )"""
    cursor.execute(query)

    query = """CREATE TABLE IF NOT EXISTS meta(
    gfid           VARCHAR(100) PRIMARY KEY ON CONFLICT IGNORE,
    changelog_time VARCHAR(100)
    )"""
    cursor.execute(query)


def db_record_data(gfid, changelog_time):
    query = "INSERT INTO data(gfid, changelog_time) VALUES(?, ?)"
    cursor.execute(query, (gfid, changelog_time))


def db_record_meta(gfid, changelog_time):
    query = "INSERT INTO meta(gfid, changelog_time) VALUES(?, ?)"
    cursor.execute(query, (gfid, changelog_time))


def db_remove_meta(gfid):
    query = "DELETE FROM meta WHERE gfid = ?"
    cursor.execute(query, (gfid, ))


def db_remove_data(gfid):
    query = "DELETE FROM data WHERE gfid = ?"
    cursor.execute(query, (gfid, ))


def db_get_data(start, end, limit, offset):
    query = """SELECT gfid FROM data WHERE changelog_time
    BETWEEN ? AND ? LIMIT ? OFFSET ?"""
    cursor.execute(query, (start, end, limit, offset))
    out = []
    for row in cursor:
        out.append(row[0])

    return out


def db_get_meta(start, end, limit, offset):
    query = """SELECT gfid FROM meta WHERE changelog_time
    BETWEEN ? AND ? LIMIT ? OFFSET ?"""
    cursor.execute(query, (start, end, limit, offset))
    out = []
    for row in cursor:
        out.append(row[0])

    return out


def db_delete_meta_if_exists_in_data():
    query = """
    DELETE FROM meta WHERE gfid in
    (SELECT M.gfid
     FROM meta M INNER JOIN data D
     ON M.gfid = D.gfid)
    """
    cursor.execute(query)


def db_get_data_count():
    query = "SELECT COUNT(gfid) FROM data"
    cursor.execute(query)
    return cursor.fetchone()[0]


def db_get_meta_count():
    query = "SELECT COUNT(gfid) FROM meta"
    cursor.execute(query)
    return cursor.fetchone()[0]
