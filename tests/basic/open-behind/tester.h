/*
  Copyright (c) 2020 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __TESTER_H__
#define __TESTER_H__

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

enum _obj_type;
typedef enum _obj_type obj_type_t;

enum _arg_type;
typedef enum _arg_type arg_type_t;

struct _buffer;
typedef struct _buffer buffer_t;

struct _obj;
typedef struct _obj obj_t;

struct _context;
typedef struct _context context_t;

struct _arg;
typedef struct _arg arg_t;

struct _command;
typedef struct _command command_t;

enum _obj_type { OBJ_TYPE_NONE, OBJ_TYPE_FD };

enum _arg_type { ARG_TYPE_NONE, ARG_TYPE_OBJ, ARG_TYPE_NUM, ARG_TYPE_STR };

struct _buffer {
    char *base;
    uint32_t size;
    uint32_t len;
    uint32_t pos;
};

struct _obj {
    obj_type_t type;
    union {
        int32_t fd;
    };
};

struct _context {
    obj_t *objs;
    buffer_t buffer;
    uint32_t obj_count;
    bool active;
};

struct _arg {
    arg_type_t type;
    union {
        struct {
            obj_type_t type;
            obj_t *ref;
        } obj;
        struct {
            uint64_t value;
            uint64_t min;
            uint64_t max;
        } num;
        struct {
            uint32_t size;
            char *data;
        } str;
    };
};

struct _command {
    const char *name;
    int32_t (*handler)(context_t *ctx, command_t *cmd);
    union {
        arg_t *args;
        command_t *cmds;
    };
};

#define msg(_stream, _fmt, _args...)                                           \
    do {                                                                       \
        fprintf(_stream, _fmt "\n", ##_args);                                  \
        fflush(_stream);                                                       \
    } while (0)

#define msg_out(_fmt, _args...) msg(stdout, _fmt, ##_args)
#define msg_err(_err, _fmt, _args...)                                          \
    ({                                                                         \
        int32_t __msg_err = (_err);                                            \
        msg(stderr, "[%4u:%-15s] " _fmt, __LINE__, __FUNCTION__, __msg_err,    \
            ##_args);                                                          \
        -__msg_err;                                                            \
    })

#define error(_err, _fmt, _args...) msg_err(_err, "E(%4d) " _fmt, ##_args)
#define warn(_err, _fmt, _args...) msg_err(_err, "W(%4d) " _fmt, ##_args)
#define info(_err, _fmt, _args...) msg_err(_err, "I(%4d) " _fmt, ##_args)

#define out_ok(_args...) msg_out("OK " _args)
#define out_err(_err) msg_out("ERR %d", _err)

#define ARG_END                                                                \
    {                                                                          \
        ARG_TYPE_NONE                                                          \
    }

#define CMD_ARGS1(_x, _args...)                                                \
    .args = (arg_t[]) { _args }
#define CMD_ARGS(_args...) CMD_ARGS1(, ##_args, ARG_END)

#define CMD_SUB(_cmds) .cmds = _cmds

#define CMD_END                                                                \
    {                                                                          \
        NULL, NULL, CMD_SUB(NULL)                                              \
    }

#define ARG_VAL(_type)                                                         \
    {                                                                          \
        ARG_TYPE_OBJ, .obj = {.type = _type }                                  \
    }
#define ARG_NUM(_min, _max)                                                    \
    {                                                                          \
        ARG_TYPE_NUM, .num = {.min = _min, .max = _max }                       \
    }
#define ARG_STR(_size)                                                         \
    {                                                                          \
        ARG_TYPE_STR, .str = {.size = _size }                                  \
    }

extern command_t fd_commands[];

#endif /* __TESTER_H__ */