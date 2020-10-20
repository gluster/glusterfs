# -*- coding: utf-8 -*-

# Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
# This file is part of GlusterFS.
#
# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.

import sqlite3
import os
import pandas as pd

from utils import RecordType, unquote_plus_space_newline
from utils import output_path_prepare


class OutputMerger(object):
    """
    Class to merge the output files collected from
    different nodes
    """
    def __init__(self, db_path, all_dbs):
        self.conn = sqlite3.connect(db_path)
        self.cursor = self.conn.cursor()
        self.cursor_reader = self.conn.cursor()
        query = "DROP TABLE IF EXISTS finallist"
        self.cursor.execute(query)

        query = """
        CREATE TABLE finallist(
          id     INTEGER PRIMARY KEY AUTOINCREMENT,
          ts     VARCHAR,
          type   VARCHAR,
          gfid   VARCHAR,
          path1  VARCHAR,
          path2  VARCHAR,
          UNIQUE (type, path1, path2) ON CONFLICT IGNORE
        )
        """
        self.cursor.execute(query)

        # If node database exists, read each db and insert into
        # final table. Ignore if combination of TYPE PATH1 PATH2
        # already exists
        for node_db in all_dbs:
            if os.path.exists(node_db):
                conn = sqlite3.connect(node_db)
                cursor = conn.cursor()
                query = """
                SELECT   ts, type, gfid, path1, path2
                FROM     gfidpath
                WHERE    path1 != ''
                ORDER BY id ASC
                """
                for row in cursor.execute(query):
                    self.add_if_not_exists(row[0], row[1], row[2],
                                           row[3], row[4])

        self.conn.commit()

    def add_if_not_exists(self, ts, ty, gfid, path1, path2=""):
        # Adds record to finallist only if not exists
        query = """
        INSERT INTO finallist(ts, type, gfid, path1, path2)
        VALUES(?, ?, ?, ?, ?)
        """
        self.cursor.execute(query, (ts, ty, gfid, path1, path2))

    def get(self):
        query = """SELECT type, path1, path2 FROM finallist
        ORDER BY ts ASC, id ASC"""
        return self.cursor_reader.execute(query)

    def get_failures(self):
        query = """
        SELECT   gfid
        FROM     finallist
        WHERE path1 = '' OR (path2 = '' AND type = 'RENAME')
        """
        return self.cursor_reader.execute(query)


class ChangelogData(object):
    def __init__(self, dbpath, args):
        self.conn = sqlite3.connect(dbpath)
        self.cursor = self.conn.cursor()
        self.cursor_reader = self.conn.cursor()
        self._create_table_gfidpath()
        self._create_table_pgfid()
        self._create_table_inodegfid()
        self.args = args
        self.path_sep = "/"
        self.changelog_df = pd.DataFrame()
        self.mkdir_df = pd.DataFrame()
        self.meta_df = pd.DataFrame()
        self.symlink_df = pd.DataFrame()
        self.rename_df = pd.DataFrame()
        self.unlink_df = pd.DataFrame()

    def _create_table_gfidpath(self):
        drop_table = "DROP TABLE IF EXISTS gfidpath"
        self.cursor.execute(drop_table)

        create_table = """
        CREATE TABLE gfidpath(
            id     INTEGER PRIMARY KEY AUTOINCREMENT,
            ts     VARCHAR,
            type   VARCHAR,
            gfid   VARCHAR(40),
            pgfid1 VARCHAR(40) DEFAULT '',
            bn1    VARCHAR(500) DEFAULT '',
            pgfid2 VARCHAR(40) DEFAULT '',
            bn2    VARCHAR(500) DEFAULT '',
            path1  VARCHAR DEFAULT '',
            path2  VARCHAR DEFAULT ''
        )
        """
        self.cursor.execute(create_table)

        create_index = """
        CREATE INDEX gfid_index ON gfidpath(gfid);
        """
        self.cursor.execute(create_index)

    def _create_table_inodegfid(self):
        drop_table = "DROP TABLE IF EXISTS inodegfid"
        self.cursor.execute(drop_table)

        create_table = """
        CREATE TABLE inodegfid(
            inode     INTEGER PRIMARY KEY,
            gfid      VARCHAR(40),
            converted INTEGER DEFAULT 0,
            UNIQUE    (inode, gfid) ON CONFLICT IGNORE
        )
        """
        self.cursor.execute(create_table)

    def _create_table_pgfid(self):
        drop_table = "DROP TABLE IF EXISTS pgfid"
        self.cursor.execute(drop_table)

        create_table = """
        CREATE TABLE pgfid(
            pgfid  VARCHAR(40) PRIMARY KEY,
            UNIQUE (pgfid) ON CONFLICT IGNORE
        )
        """
        self.cursor.execute(create_table)

    def _get(self, tablename, filters):
        # SELECT * FROM <TABLENAME> WHERE <CONDITION>
        params = []
        query = "SELECT * FROM %s WHERE 1=1" % tablename

        for key, value in filters.items():
            query += " AND %s = ?" % key
            params.append(value)

        return self.cursor_reader.execute(query, params)

    def _get_distinct(self, tablename, distinct_field, filters):
        # SELECT DISTINCT <COL> FROM <TABLENAME> WHERE <CONDITION>
        params = []
        query = "SELECT DISTINCT %s FROM %s WHERE 1=1" % (distinct_field,
                                                          tablename)

        for key, value in filters.items():
            query += " AND %s = ?" % key
            params.append(value)

        return self.cursor_reader.execute(query, params)

    def _delete(self, tablename, filters):
        # DELETE FROM <TABLENAME> WHERE <CONDITIONS>
        query = "DELETE FROM %s WHERE 1=1" % tablename
        params = []

        for key, value in filters.items():
            query += " AND %s = ?" % key
            params.append(value)

        self.cursor.execute(query, params)

    def _add(self, tablename, data):
        # INSERT INTO <TABLENAME>(<col1>, <col2>..) VALUES(?,?..)
        query = "INSERT INTO %s(" % tablename
        fields = []
        params = []
        for key, value in data.items():
            fields.append(key)
            params.append(value)

        values_substitute = len(fields)*["?"]
        query += "%s) VALUES(%s)" % (",".join(fields),
                                     ",".join(values_substitute))
        self.cursor.execute(query, params)

    def _update(self, tablename, data, filters):
        # UPDATE <TABLENAME> SET col1 = ?,.. WHERE col1=? AND ..
        params = []
        update_fields = []
        for key, value in data.items():
            update_fields.append("%s = ?" % key)
            params.append(value)

        query = "UPDATE %s SET %s WHERE 1 = 1" % (tablename,
                                                  ", ".join(update_fields))

        for key, value in filters.items():
            query += " AND %s = ?" % key
            params.append(value)

        self.cursor.execute(query, params)

    def _exists(self, tablename, filters):
        if not filters:
            return False

        query = "SELECT COUNT(1) FROM %s WHERE 1=1" % tablename
        params = []

        for key, value in filters.items():
            query += " AND %s = ?" % key
            params.append(value)

        self.cursor.execute(query, params)
        row = self.cursor.fetchone()
        return True if row[0] > 0 else False

    def gfidpath_add(self, changelogfile, ty, gfid, pgfid1="", bn1="",
                     pgfid2="", bn2="", path1="", path2=""):
        self._add("gfidpath", {
            "ts": changelogfile.split(".")[-1],
            "type": ty,
            "gfid": gfid,
            "pgfid1": pgfid1,
            "bn1": bn1,
            "pgfid2": pgfid2,
            "bn2": bn2,
            "path1": path1,
            "path2": path2
        })

    def gfidpath_update(self, data, filters):
        self._update("gfidpath", data, filters)

    def gfidpath_delete(self, filters):
        self._delete("gfidpath", filters)

    def gfidpath_exists(self, filters):
        return self._exists("gfidpath", filters)

    def gfidpath_get(self, filters={}):
        return self._get("gfidpath", filters)

    def gfidpath_get_distinct(self, distinct_field, filters={}):
        return self._get_distinct("gfidpath", distinct_field, filters)

    def pgfid_add(self, pgfid):
        self._add("pgfid", {
            "pgfid": pgfid
        })

    def pgfid_update(self, data, filters):
        self._update("pgfid", data, filters)

    def pgfid_get(self, filters={}):
        return self._get("pgfid", filters)

    def pgfid_get_distinct(self, distinct_field, filters={}):
        return self._get_distinct("pgfid", distinct_field, filters)

    def pgfid_exists(self, filters):
        return self._exists("pgfid", filters)

    def inodegfid_add(self, inode, gfid, converted=0):
        self._add("inodegfid", {
            "inode": inode,
            "gfid": gfid,
            "converted": converted
        })

    def inodegfid_update(self, data, filters):
        self._update("inodegfid", data, filters)

    def inodegfid_get(self, filters={}):
        return self._get("inodegfid", filters)

    def inodegfid_get_distinct(self, distinct_field, filters={}):
        return self._get_distinct("inodegfid", distinct_field, filters)

    def inodegfid_exists(self, filters):
        return self._exists("inodegfid", filters)

    def append_path1(self, path, inode):
        # || is for concatenate in SQL
        query = """UPDATE gfidpath SET path1 = path1 || ',' || ?
        WHERE gfid IN (SELECT gfid FROM inodegfid WHERE inode = ?)"""
        self.cursor.execute(query, (path, inode))

    def gfidpath_set_path1(self, path1, pgfid1):
        # || is for concatenate in SQL
        if path1 == "":
            update_str1 = "? || bn1"
            update_str2 = "? || bn2"
        else:
            update_str1 = "? || '{0}' || bn1".format(self.path_sep)
            update_str2 = "? || '{0}' || bn2".format(self.path_sep)

        query = """UPDATE gfidpath SET path1 = %s
        WHERE pgfid1 = ?""" % update_str1
        self.cursor.execute(query, (path1, pgfid1))

        # Set Path2 if pgfid1 and pgfid2 are same
        query = """UPDATE gfidpath SET path2 = %s
        WHERE pgfid2 = ?""" % update_str2
        self.cursor.execute(query, (path1, pgfid1))

    def gfidpath_set_path2(self, path2, pgfid2):
        # || is for concatenate in SQL
        if path2 == "":
            update_str = "? || bn2"
        else:
            update_str = "? || '{0}' || bn2".format(self.path_sep)

        query = """UPDATE gfidpath SET path2 = %s
        WHERE pgfid2 = ?""" % update_str
        self.cursor.execute(query, (path2, pgfid2))

    def when_create_mknod_mkdir(self, changelogfile, data):
        # E <GFID> <MKNOD|CREATE|MKDIR> <MODE> <USER> <GRP> <PGFID>/<BNAME>
        # Add the Entry to DB
        pgfid1, bn1 = data[6].split("/", 1)

        if self.args.no_encode:
            bn1 = unquote_plus_space_newline(bn1).strip()

        self.gfidpath_add(changelogfile, RecordType.NEW, data[1], pgfid1, bn1)

    def when_rename(self, changelogfile, data):
        # E <GFID> RENAME <OLD_PGFID>/<BNAME> <PGFID>/<BNAME>
        pgfid1, bn1 = data[3].split("/", 1)
        pgfid2, bn2 = data[4].split("/", 1)

        if self.args.no_encode:
            bn1 = unquote_plus_space_newline(bn1).strip()
            bn2 = unquote_plus_space_newline(bn2).strip()

        if self.gfidpath_exists({"gfid": data[1], "type": "NEW",
                                 "pgfid1": pgfid1, "bn1": bn1}):
            # If <OLD_PGFID>/<BNAME> is same as CREATE, Update
            # <NEW_PGFID>/<BNAME> in NEW.
            self.gfidpath_update({"pgfid1": pgfid2, "bn1": bn2},
                                 {"gfid": data[1], "type": "NEW",
                                  "pgfid1": pgfid1, "bn1": bn1})
        elif self.gfidpath_exists({"gfid": data[1], "type": "RENAME",
                                   "pgfid2": pgfid1, "bn2": bn1}):
            # If we are renaming file back to original name then just
            # delete the entry since it will effectively be a no-op
            if self.gfidpath_exists({"gfid": data[1], "type": "RENAME",
                                     "pgfid2": pgfid1, "bn2": bn1,
                                     "pgfid1": pgfid2, "bn1": bn2}):
                self.gfidpath_delete({"gfid": data[1], "type": "RENAME",
                                      "pgfid2": pgfid1, "bn2": bn1})
            else:
                # If <OLD_PGFID>/<BNAME> is same as <PGFID2>/<BN2>
                # (may be previous RENAME)
                # then UPDATE <NEW_PGFID>/<BNAME> as <PGFID2>/<BN2>
                self.gfidpath_update({"pgfid2": pgfid2, "bn2": bn2},
                                     {"gfid": data[1], "type": "RENAME",
                                      "pgfid2": pgfid1, "bn2": bn1})
        else:
            # Else insert as RENAME
            self.gfidpath_add(changelogfile, RecordType.RENAME, data[1],
                              pgfid1, bn1, pgfid2, bn2)

        if self.gfidpath_exists({"gfid": data[1], "type": "MODIFY"}):
            # If MODIFY exists already for that GFID, remove it and insert
            # again so that MODIFY entry comes after RENAME entry
            # Output will have MODIFY <NEWNAME>
            self.gfidpath_delete({"gfid": data[1], "type": "MODIFY"})
            self.gfidpath_add(changelogfile, RecordType.MODIFY, data[1])

    def when_link_symlink(self, changelogfile, data):
        # E <GFID> <LINK|SYMLINK> <PGFID>/<BASENAME>
        # Add as New record in Db as Type NEW
        pgfid1, bn1 = data[3].split("/", 1)
        if self.args.no_encode:
            bn1 = unquote_plus_space_newline(bn1).strip()

        self.gfidpath_add(changelogfile, RecordType.NEW, data[1], pgfid1, bn1)

    def when_data_meta(self, changelogfile, data):
        # If GFID row exists, Ignore else Add to Db
        if not self.gfidpath_exists({"gfid": data[1], "type": "NEW"}) and \
           not self.gfidpath_exists({"gfid": data[1], "type": "MODIFY"}):
            self.gfidpath_add(changelogfile, RecordType.MODIFY, data[1])

    def when_unlink_rmdir(self, changelogfile, data):
        # E <GFID> <UNLINK|RMDIR> <PGFID>/<BASENAME>
        pgfid1, bn1 = data[3].split("/", 1)

        if self.args.no_encode:
            bn1 = unquote_plus_space_newline(bn1).strip()

        deleted_path = data[4] if len(data) == 5 else ""
        if deleted_path != "":
            deleted_path = unquote_plus_space_newline(deleted_path)
            deleted_path = output_path_prepare(deleted_path, self.args)

        if self.gfidpath_exists({"gfid": data[1], "type": "NEW",
                                 "pgfid1": pgfid1, "bn1": bn1}):
            # If path exists in table as NEW with same GFID
            # Delete that row
            self.gfidpath_delete({"gfid": data[1], "type": "NEW",
                                  "pgfid1": pgfid1, "bn1": bn1})
        else:
            # Else Record as DELETE
            self.gfidpath_add(changelogfile, RecordType.DELETE, data[1],
                              pgfid1, bn1, path1=deleted_path)

        # Update path1 as deleted_path if pgfid1 and bn1 is same as deleted
        self.gfidpath_update({"path1": deleted_path}, {"gfid": data[1],
                                                       "pgfid1": pgfid1,
                                                       "bn1": bn1})

        # Update path2 as deleted_path if pgfid2 and bn2 is same as deleted
        self.gfidpath_update({"path2": deleted_path}, {
            "type": RecordType.RENAME,
            "gfid": data[1],
            "pgfid2": pgfid1,
            "bn2": bn1})

        # If deleted directory is parent for somebody
        query1 = """UPDATE gfidpath SET path1 = ? || '{0}' || bn1
        WHERE pgfid1 = ? AND path1 != ''""".format(self.path_sep)
        self.cursor.execute(query1, (deleted_path, data[1]))

        query1 = """UPDATE gfidpath SET path2 = ? || '{0}' || bn1
        WHERE pgfid2 = ? AND path2 != ''""".format(self.path_sep)
        self.cursor.execute(query1, (deleted_path, data[1]))

    def collect_changelog_dump(self, filename):
        """
        The changelog file contents are completely dumped into a
        pandas dataframe.
        """
        name_list = ['0', 'gfid', '2', '3', '4', '5', '6']
        temporary_df = pd.read_csv(filename, encoding='utf-8', sep=" ",
                                   names=name_list, engine='python')
        temporary_df['ts'] = os.path.basename(filename).split(".")[-1]
        self.changelog_df = self.changelog_df.append(temporary_df)
        del(temporary_df)

    def df_to_db_ops(self, args):
        """
        The data is to be manipulated according to the underlying operation
        in changelog before pushing it into db. The operations for mkdir and
        symlink related ops are pretty straightforward. The DB related ops
        increae with metadata and unlink or rmdir related ops.
        """
        # Splitting th emain dataframe into sub frames.
        self.df_split_ops(args)

        # DB operations for mkdir, create and mknod.
        self.df_mknod_ops(args)

        # DB operations for symlink and link.
        self.df_symlink_ops(args)

    def df_split_ops(self, args):
        """
        The dataframe is split only different ops according to the type
        in the generic dataframe.
        """
        # Creating df for mkdir, mknod and create operations.
        self.mkdir_df = self.changelog_df[(self.changelog_df['0'] == "E") &
                                          ((self.changelog_df['2'] == "MKNOD")|
                                           (self.changelog_df['2'] == "MKDIR")|
                                           (self.changelog_df['2'] ==\
                                            "CREATE"))]

        # Creating df for link and symlink operations.
        self.symlink_df = self.changelog_df[(self.changelog_df['0'] == "E") &
                                            ((self.changelog_df['2'] ==\
                                              "LINK") |
                                             (self.changelog_df['2'] ==\
                                              "SYMLINK"))]

        # Creating df for rename operations.
        self.rename_df = self.changelog_df[(self.changelog_df['0'] == "E") &
                                           (self.changelog_df['2'] ==\
                                            "RENAME")]

        # Creating df for unlink and rmdir operations.
        self.unlink_df = self.changelog_df[(self.changelog_df['0'] == "E") &
                                           ((self.changelog_df['2'] ==\
                                             "UNLINK") |
                                            (self.changelog_df['2'] ==\
                                             "RMDIR"))]

        # Creating df for meta data operations.
        if not args.only_namespace_changes:
            self.meta_df = self.changelog_df[(self.changelog_df['0'] == "D") |
                                             (self.changelog_df['0'] == "M")]

        # Drop the main changelog df as it's not required now.
        del(self.changelog_df)

    def df_mknod_ops(self, args):
        """
        Further operations on data is done for ops related to mknod, mkdir
        and create, then pushed to DB.
        """
        self.mkdir_df['pgfid1'] = self.mkdir_df['6']\
                                     .apply(lambda x: x.split("/", 1)[0])
        self.mkdir_df['bn1'] = self.mkdir_df['6']\
                                   .apply(lambda x: x.split("/", 1)[1])

        # Dropping columns which aren;t required.
        self.mndir_df = self.mkdir_df.drop(['0', '2', '3', '4', '5',
                                            '6'], axis=1)

        if self.args.no_encode:
            self.mkdir_df['bn1'] = self.mkdir_df['bn1']\
                                       .apply(unquote_plus_space_newline_split)

        # Creating columns which would be required in futher ops.
        self.mkdir_df['type'] = RecordType.NEW
        self.mkdir_df['pgfid2'] = ""
        self.mkdir_df['bn2'] = ""
        self.mkdir_df['path1'] = ""
        self.mkdir_df['path2'] = ""

        # Push data into db
        self.mkdir_df.to_sql('gfidpath', self.conn, if_exists='append',
                             index=False)
    def df_symlink_ops(self):
        """
        Further operations on data is done for ops related to link and symlink
        and then pushed into db.
        """
        self.symlink_df['pgfid1'] = self.symlink_df['3']\
                                       .apply(lambda x: x.split("/", 1)[0])
        self.symlink_df['bn1'] = self.symlink_df['3']\
                                     .apply(lambda x: x.spit("/", 1)[1])

        # Dropping the columns which aren't required.
        self.symlink_df = self.symlink_df.drop(['0', '2', '3', '4', '5',
                                                '6'], axis=1)

        # No encoding condition
        if self.args.no_encode:
            self.symlink_df['bn1'] = self.symlink_df['bn1']\
                                     .apply(unquote_plus_space_newline_split)

        # Creating columns which would be required for further ops.
        self.symlink_df['type'] = RecordType.NEW
        self.symlink_df['pgfid2'] = ""
        self.symlink_df['bn2'] = ""
        self.symlink_df['path1'] = ""
        self.symlink_df['path2'] = ""

        # Push data into db
        self.symlink_df.to_sql('gfidpath', self.conn, if_exists='append',
                               index=False)

    def dump_pandas_into_excel(self):
        """
        This is an experimental file for debugging.
        """
        self.mkdir_df.to_csv("/home/mkdir_result.csv")

    def commit(self):
        self.conn.commit()
