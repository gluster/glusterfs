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
#include <string.h>
#include <ctype.h>
#include <errno.h>

static void *
mem_alloc(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr == NULL) {
        error(ENOMEM, "Failed to allocate memory (%zu bytes)", size);
    }

    return ptr;
}

static void
mem_free(void *ptr)
{
    free(ptr);
}

static bool
buffer_create(context_t *ctx, size_t size)
{
    ctx->buffer.base = mem_alloc(size);
    if (ctx->buffer.base == NULL) {
        return false;
    }

    ctx->buffer.size = size;
    ctx->buffer.len = 0;
    ctx->buffer.pos = 0;

    return true;
}

static void
buffer_destroy(context_t *ctx)
{
    mem_free(ctx->buffer.base);
    ctx->buffer.size = 0;
    ctx->buffer.len = 0;
}

static int32_t
buffer_get(context_t *ctx)
{
    ssize_t len;

    if (ctx->buffer.pos >= ctx->buffer.len) {
        len = read(0, ctx->buffer.base, ctx->buffer.size);
        if (len < 0) {
            return error(errno, "read() failed");
        }
        if (len == 0) {
            return 0;
        }

        ctx->buffer.len = len;
        ctx->buffer.pos = 0;
    }

    return ctx->buffer.base[ctx->buffer.pos++];
}

static int32_t
str_skip_spaces(context_t *ctx, int32_t current)
{
    while ((current > 0) && (current != '\n') && isspace(current)) {
        current = buffer_get(ctx);
    }

    return current;
}

static int32_t
str_token(context_t *ctx, char *buffer, uint32_t size, int32_t current)
{
    uint32_t len;

    current = str_skip_spaces(ctx, current);

    len = 0;
    while ((size > 0) && (current > 0) && (current != '\n') &&
           !isspace(current)) {
        len++;
        *buffer++ = current;
        size--;
        current = buffer_get(ctx);
    }

    if (len == 0) {
        return error(ENODATA, "Expecting a token");
    }

    if (size == 0) {
        return error(ENOBUFS, "Token too long");
    }

    *buffer = 0;

    return current;
}

static int32_t
str_number(context_t *ctx, uint64_t min, uint64_t max, uint64_t *value,
           int32_t current)
{
    char text[32], *ptr;
    uint64_t num;

    current = str_token(ctx, text, sizeof(text), current);
    if (current > 0) {
        num = strtoul(text, &ptr, 0);
        if ((*ptr != 0) || (num < min) || (num > max)) {
            return error(ERANGE, "Invalid number");
        }
        *value = num;
    }

    return current;
}

static int32_t
str_eol(context_t *ctx, int32_t current)
{
    current = str_skip_spaces(ctx, current);
    if (current != '\n') {
        return error(EINVAL, "Expecting end of command");
    }

    return current;
}

static void
str_skip(context_t *ctx, int32_t current)
{
    while ((current > 0) && (current != '\n')) {
        current = buffer_get(ctx);
    }
}

static int32_t
cmd_parse_obj(context_t *ctx, arg_t *arg, int32_t current)
{
    obj_t *obj;
    uint64_t id;

    current = str_number(ctx, 0, ctx->obj_count, &id, current);
    if (current <= 0) {
        return current;
    }

    obj = &ctx->objs[id];
    if (obj->type != arg->obj.type) {
        if (obj->type != OBJ_TYPE_NONE) {
            return error(EBUSY, "Object is in use");
        }
        return error(ENOENT, "Object is not defined");
    }

    arg->obj.ref = obj;

    return current;
}

static int32_t
cmd_parse_num(context_t *ctx, arg_t *arg, int32_t current)
{
    return str_number(ctx, arg->num.min, arg->num.max, &arg->num.value,
                      current);
}

static int32_t
cmd_parse_str(context_t *ctx, arg_t *arg, int32_t current)
{
    return str_token(ctx, arg->str.data, arg->str.size, current);
}

static int32_t
cmd_parse_args(context_t *ctx, command_t *cmd, int32_t current)
{
    arg_t *arg;

    for (arg = cmd->args; arg->type != ARG_TYPE_NONE; arg++) {
        switch (arg->type) {
            case ARG_TYPE_OBJ:
                current = cmd_parse_obj(ctx, arg, current);
                break;
            case ARG_TYPE_NUM:
                current = cmd_parse_num(ctx, arg, current);
                break;
            case ARG_TYPE_STR:
                current = cmd_parse_str(ctx, arg, current);
                break;
            default:
                return error(EINVAL, "Unknown argument type");
        }
    }

    if (current < 0) {
        return current;
    }

    current = str_eol(ctx, current);
    if (current <= 0) {
        return error(EINVAL, "Syntax error");
    }

    return cmd->handler(ctx, cmd);
}

static int32_t
cmd_parse(context_t *ctx, command_t *cmds)
{
    char text[32];
    command_t *cmd;
    int32_t current;

    cmd = cmds;
    do {
        current = str_token(ctx, text, sizeof(text), buffer_get(ctx));
        if (current <= 0) {
            return current;
        }

        while (cmd->name != NULL) {
            if (strcmp(cmd->name, text) == 0) {
                if (cmd->handler != NULL) {
                    return cmd_parse_args(ctx, cmd, current);
                }
                cmd = cmd->cmds;
                break;
            }
            cmd++;
        }
    } while (cmd->name != NULL);

    str_skip(ctx, current);

    return error(ENOTSUP, "Unknown command");
}

static void
cmd_fini(context_t *ctx, command_t *cmds)
{
    command_t *cmd;
    arg_t *arg;

    for (cmd = cmds; cmd->name != NULL; cmd++) {
        if (cmd->handler == NULL) {
            cmd_fini(ctx, cmd->cmds);
        } else {
            for (arg = cmd->args; arg->type != ARG_TYPE_NONE; arg++) {
                switch (arg->type) {
                    case ARG_TYPE_STR:
                        mem_free(arg->str.data);
                        arg->str.data = NULL;
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

static bool
cmd_init(context_t *ctx, command_t *cmds)
{
    command_t *cmd;
    arg_t *arg;

    for (cmd = cmds; cmd->name != NULL; cmd++) {
        if (cmd->handler == NULL) {
            if (!cmd_init(ctx, cmd->cmds)) {
                return false;
            }
        } else {
            for (arg = cmd->args; arg->type != ARG_TYPE_NONE; arg++) {
                switch (arg->type) {
                    case ARG_TYPE_STR:
                        arg->str.data = mem_alloc(arg->str.size);
                        if (arg->str.data == NULL) {
                            return false;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    return true;
}

static bool
objs_create(context_t *ctx, uint32_t count)
{
    uint32_t i;

    ctx->objs = mem_alloc(sizeof(obj_t) * count);
    if (ctx->objs == NULL) {
        return false;
    }
    ctx->obj_count = count;

    for (i = 0; i < count; i++) {
        ctx->objs[i].type = OBJ_TYPE_NONE;
    }

    return true;
}

static int32_t
objs_destroy(context_t *ctx)
{
    uint32_t i;
    int32_t err;

    err = 0;
    for (i = 0; i < ctx->obj_count; i++) {
        if (ctx->objs[i].type != OBJ_TYPE_NONE) {
            err = error(ENOTEMPTY, "Objects not destroyed");
            break;
        }
    }

    mem_free(ctx->objs);
    ctx->objs = NULL;
    ctx->obj_count = 0;

    return err;
}

static context_t *
init(size_t size, uint32_t objs, command_t *cmds)
{
    context_t *ctx;

    ctx = mem_alloc(sizeof(context_t));
    if (ctx == NULL) {
        goto failed;
    }

    if (!buffer_create(ctx, size)) {
        goto failed_ctx;
    }

    if (!objs_create(ctx, objs)) {
        goto failed_buffer;
    }

    if (!cmd_init(ctx, cmds)) {
        goto failed_objs;
    }

    ctx->active = true;

    return ctx;

failed_objs:
    cmd_fini(ctx, cmds);
    objs_destroy(ctx);
failed_buffer:
    buffer_destroy(ctx);
failed_ctx:
    mem_free(ctx);
failed:
    return NULL;
}

static int32_t
fini(context_t *ctx, command_t *cmds)
{
    int32_t ret;

    cmd_fini(ctx, cmds);
    buffer_destroy(ctx);

    ret = objs_destroy(ctx);

    ctx->active = false;

    return ret;
}

static int32_t
exec_quit(context_t *ctx, command_t *cmd)
{
    ctx->active = false;

    return 0;
}

static command_t commands[] = {{"fd", NULL, CMD_SUB(fd_commands)},
                               {"quit", exec_quit, CMD_ARGS()},
                               CMD_END};

int32_t
main(int32_t argc, char *argv[])
{
    context_t *ctx;
    int32_t res;

    ctx = init(1024, 16, commands);
    if (ctx == NULL) {
        return 1;
    }

    do {
        res = cmd_parse(ctx, commands);
        if (res < 0) {
            out_err(-res);
        }
    } while (ctx->active);

    res = fini(ctx, commands);
    if (res >= 0) {
        out_ok();
        return 0;
    }

    out_err(-res);

    return 1;
}
