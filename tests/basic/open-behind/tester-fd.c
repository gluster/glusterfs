/*
  Copyright (c) 2020 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "tester.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

static int32_t
fd_open(context_t *ctx, command_t *cmd)
{
    obj_t *obj;
    int32_t fd;

    obj = cmd->args[0].obj.ref;

    fd = open(cmd->args[1].str.data, O_RDWR);
    if (fd < 0) {
        return error(errno, "open() failed");
    }

    obj->type = OBJ_TYPE_FD;
    obj->fd = fd;

    out_ok("%d", fd);

    return 0;
}

static int32_t
fd_close(context_t *ctx, command_t *cmd)
{
    obj_t *obj;

    obj = cmd->args[0].obj.ref;
    obj->type = OBJ_TYPE_NONE;

    if (close(obj->fd) != 0) {
        return error(errno, "close() failed");
    }

    out_ok();

    return 0;
}

static int32_t
fd_write(context_t *ctx, command_t *cmd)
{
    ssize_t len, ret;

    len = strlen(cmd->args[1].str.data);
    ret = write(cmd->args[0].obj.ref->fd, cmd->args[1].str.data, len);
    if (ret < 0) {
        return error(errno, "write() failed");
    }

    out_ok("%zd", ret);

    return 0;
}

static int32_t
fd_read(context_t *ctx, command_t *cmd)
{
    char data[cmd->args[1].num.value + 1];
    ssize_t ret;

    ret = read(cmd->args[0].obj.ref->fd, data, cmd->args[1].num.value);
    if (ret < 0) {
        return error(errno, "read() failed");
    }

    data[ret] = 0;

    out_ok("%zd %s", ret, data);

    return 0;
}

command_t fd_commands[] = {
    {"open", fd_open, CMD_ARGS(ARG_VAL(OBJ_TYPE_NONE), ARG_STR(1024))},
    {"close", fd_close, CMD_ARGS(ARG_VAL(OBJ_TYPE_FD))},
    {"write", fd_write, CMD_ARGS(ARG_VAL(OBJ_TYPE_FD), ARG_STR(1024))},
    {"read", fd_read, CMD_ARGS(ARG_VAL(OBJ_TYPE_FD), ARG_NUM(0, 1024))},
    CMD_END};
