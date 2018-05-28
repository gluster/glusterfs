#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Why?

Converts this

GlusterFS Changelog | version: v1.1 | encoding : 2
E0b99ef11-4b79-4cd0-9730-b5a0e8c4a8c0^@4^@16877^@0^@0^@00000000-0000-0000-0000-
000000000001/dir1^@Ec5250af6-720e-4bfe-b938-827614304f39^@23^@33188^@0^@0^@0b99
ef11-4b79-4cd0-9730-b5a0e8c4a8c0/hello.txt^@Dc5250af6-720e-4bfe-b938-827614304f
39^@Dc5250af6-720e-4bfe-b938-827614304f39^@


to human readable :)

E 0b99ef11-4b79-4cd0-9730-b5a0e8c4a8c0 MKDIR 16877 0 000000000-0000-0000-0000
  -000000000001/dir1
E c5250af6-720e-4bfe-b938-827614304f39 CREATE 33188 0 0 0b99ef11-4b79-4cd0-9730
  -b5a0e8c4a8c0/hello.txt
D c5250af6-720e-4bfe-b938-827614304f39
D c5250af6-720e-4bfe-b938-827614304f39


"""
import sys
import codecs

ENTRY = 'E'
META = 'M'
DATA = 'D'
SEP = "\x00"

GF_FOP = [
    "NULL", "STAT", "READLINK", "MKNOD", "MKDIR", "UNLINK",
    "RMDIR", "SYMLINK", "RENAME", "LINK", "TRUNCATE", "OPEN",
    "READ", "WRITE", "STATFS", "FLUSH", "FSYNC", "SETXATTR",
    "GETXATTR", "REMOVEXATTR", "OPENDIR", "FSYNCDIR", "ACCESS",
    "CREATE", "FTRUNCATE", "FSTAT", "LK", "LOOKUP", "READDIR",
    "INODELK", "FINODELK", "ENTRYLK", "FENTRYLK", "XATTROP",
    "FXATTROP", "FSETXATTR", "FGETXATTR", "RCHECKSUM", "SETATTR",
    "FSETATTR", "READDIRP", "GETSPEC", "FORGET", "RELEASE",
    "RELEASEDIR", "FREMOVEXATTR", "FALLOCATE", "DISCARD", "ZEROFILL"]


class NumTokens_V11(object):
    E = 7
    M = 3
    D = 2
    NULL = 3
    MKNOD = 7
    MKDIR = 7
    UNLINK = 4
    RMDIR = 4
    SYMLINK = 4
    RENAME = 5
    LINK = 4
    SETXATTR = 3
    REMOVEXATTR = 3
    CREATE = 7
    SETATTR = 3
    FTRUNCATE = 3
    FXATTROP = 3


class NumTokens_V12(NumTokens_V11):
    UNLINK = 5
    RMDIR = 5


class Version:
    V11 = "v1.1"
    V12 = "v1.2"


class Record(object):
    def __init__(self, **kwargs):
        self.ts = kwargs.get("ts", None)
        self.fop_type = kwargs.get("fop_type", None)
        self.gfid = kwargs.get("gfid", None)
        self.path = kwargs.get("path", None)
        self.fop = kwargs.get("fop", None)
        self.path1 = kwargs.get("path1", None)
        self.path2 = kwargs.get("path2", None)
        self.mode = kwargs.get("mode", None)
        self.uid = kwargs.get("uid", None)
        self.gid = kwargs.get("gid", None)

    def create_mknod_mkdir(self, **kwargs):
        self.path = kwargs.get("path", None)
        self.fop = kwargs.get("fop", None)
        self.mode = kwargs.get("mode", None)
        self.uid = kwargs.get("uid", None)
        self.gid = kwargs.get("gid", None)

    def metadata(self, **kwargs):
        self.fop = kwargs.get("fop", None)

    def rename(self, **kwargs):
        self.fop = kwargs.get("fop", None)
        self.path1 = kwargs.get("path1", None)
        self.path2 = kwargs.get("path2", None)

    def link_symlink_unlink_rmdir(self, **kwargs):
        self.path = kwargs.get("path", None)
        self.fop = kwargs.get("fop", None)

    def __unicode__(self):
        if self.fop_type == "D":
            return u"{ts} {fop_type} {gfid}".format(**self.__dict__)
        elif self.fop_type == "M":
            return u"{ts} {fop_type} {gfid} {fop}".format(**self.__dict__)
        elif self.fop_type == "E":
            if self.fop in ["CREATE", "MKNOD", "MKDIR"]:
                return (u"{ts} {fop_type} {gfid} {fop} "
                        u"{path} {mode} {uid} {gid}".format(**self.__dict__))
            elif self.fop == "RENAME":
                return (u"{ts} {fop_type} {gfid} {fop} "
                        u"{path1} {path2}".format(**self.__dict__))
            elif self.fop in ["LINK", "SYMLINK", "UNLINK", "RMDIR"]:
                return (u"{ts} {fop_type} {gfid} {fop} "
                        u"{path}".format(**self.__dict__))
            else:
                return repr(self.__dict__)
        else:
            return repr(self.__dict__)

    def __str__(self):
        return unicode(self).encode('utf-8')


def get_num_tokens(data, tokens, version=Version.V11):
    if version == Version.V11:
        cls_numtokens = NumTokens_V11
    elif version == Version.V12:
        cls_numtokens = NumTokens_V12
    else:
        sys.stderr.write("Unknown Changelog Version\n")
        sys.exit(1)

    if data[tokens[0]] in [ENTRY, META]:
        if len(tokens) >= 3:
            return getattr(cls_numtokens, GF_FOP[int(data[tokens[2]])])
        else:
            return None
    else:
        return getattr(cls_numtokens, data[tokens[0]])


def process_record(data, tokens, changelog_ts, callback):
    if data[tokens[0]] in [ENTRY, META]:
        try:
            tokens[2] = GF_FOP[int(data[tokens[2]])]
        except ValueError:
            tokens[2] = "NULL"

    if not changelog_ts:
        ts1 = int(changelog_ts)
    else:
        ts1=""
    record = Record(ts=ts1, fop_type=data[tokens[0]],
                    gfid=data[tokens[1]])
    if data[tokens[0]] == META:
        record.metadata(fop=tokens[2])
    elif data[tokens[0]] == ENTRY:
        if tokens[2] in ["CREATE", "MKNOD", "MKDIR"]:
            record.create_mknod_mkdir(fop=tokens[2],
                                      path=data[tokens[6]],
                                      mode=int(data[tokens[3]]),
                                      uid=int(data[tokens[4]]),
                                      gid=int(data[tokens[5]]))
        elif tokens[2] == "RENAME":
            record.rename(fop=tokens[2],
                          path1=data[tokens[3]],
                          path2=data[tokens[4]])
        if tokens[2] in ["LINK", "SYMLINK", "UNLINK", "RMDIR"]:
            record.link_symlink_unlink_rmdir(fop=tokens[2],
                                             path=data[tokens[3]])
    callback(record)


def default_callback(record):
    sys.stdout.write(u"{0}\n".format(record))


def parse(filename, callback=default_callback):
    data = None
    tokens = []
    changelog_ts = filename.rsplit(".")[-1]
    with codecs.open(filename, mode="rb", encoding="utf-8") as f:
        # GlusterFS Changelog | version: v1.1 | encoding : 2
        header = f.readline()
        version = header.split()[4]

        data = f.readline()

        slice_start = 0
        in_record = False

        prev_char = ""
        next_char = ""
        for i, c in enumerate(data):
            next_char = ""
            if len(data) >= (i + 2):
                next_char = data[i+1]

            if not in_record and c in [ENTRY, META, DATA]:
                tokens.append(slice(slice_start, i+1))
                slice_start = i+1
                in_record = True
                continue

            if c == SEP and ((prev_char != SEP and next_char == SEP) or
                             (prev_char == SEP and next_char != SEP) or
                             (prev_char != SEP and next_char != SEP)):
                tokens.append(slice(slice_start, i))
                slice_start = i+1

                num_tokens = get_num_tokens(data, tokens, version)

                if num_tokens == len(tokens):
                    process_record(data, tokens, changelog_ts, callback)
                    in_record = False
                    tokens = []

            prev_char = c

        # process last record
        if slice_start < (len(data) - 1):
            tokens.append(slice(slice_start, len(data)))
            process_record(data, tokens, changelog_ts, callback)
            tokens = []

parse(sys.argv[1])
