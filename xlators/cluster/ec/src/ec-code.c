/*
  Copyright (c) 2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include "syscall.h"

#include "ec-mem-types.h"
#include "ec-code.h"
#include "ec-messages.h"
#include "ec-code-c.h"
#include "ec-helpers.h"

#ifdef USE_EC_DYNAMIC_X64
#include "ec-code-x64.h"
#endif

#ifdef USE_EC_DYNAMIC_SSE
#include "ec-code-sse.h"
#endif

#ifdef USE_EC_DYNAMIC_AVX
#include "ec-code-avx.h"
#endif

#define EC_CODE_SIZE (1024 * 64)
#define EC_CODE_ALIGN 4096

#define EC_CODE_CHUNK_MIN_SIZE 512

#define EC_PROC_BUFFER_SIZE 4096

#define PROC_CPUINFO "/proc/cpuinfo"

struct _ec_code_proc;
typedef struct _ec_code_proc ec_code_proc_t;

struct _ec_code_proc {
    int32_t      fd;
    gf_boolean_t eof;
    gf_boolean_t error;
    gf_boolean_t skip;
    ssize_t      size;
    ssize_t      pos;
    char         buffer[EC_PROC_BUFFER_SIZE];
};

static ec_code_gen_t *ec_code_gen_table[] = {
#ifdef USE_EC_DYNAMIC_AVX
    &ec_code_gen_avx,
#endif
#ifdef USE_EC_DYNAMIC_SSE
    &ec_code_gen_sse,
#endif
#ifdef USE_EC_DYNAMIC_X64
    &ec_code_gen_x64,
#endif
    NULL
};

static void
ec_code_arg_set(ec_code_arg_t *arg, uint32_t value)
{
    arg->value = value;
}

static void
ec_code_arg_assign(ec_code_builder_t *builder, ec_code_op_t *op,
                   ec_code_arg_t *arg, uint32_t reg)
{
    arg->value = reg;

    if (builder->regs <= reg) {
        builder->regs = reg + 1;
    }

}

static void
ec_code_arg_use(ec_code_builder_t *builder, ec_code_op_t *op,
                ec_code_arg_t *arg, uint32_t reg)
{
    arg->value = reg;
}

static void
ec_code_arg_update(ec_code_builder_t *builder, ec_code_op_t *op,
                   ec_code_arg_t *arg, uint32_t reg)
{
    arg->value = reg;
}

static ec_code_op_t *
ec_code_op_next(ec_code_builder_t *builder)
{
    ec_code_op_t *op;

    op = &builder->ops[builder->count++];
    memset(op, 0, sizeof(ec_code_op_t));

    return op;
}

static void
ec_code_load(ec_code_builder_t *builder, uint32_t bit, uint32_t offset)
{
    ec_code_op_t *op;

    op = ec_code_op_next(builder);

    op->op = EC_GF_OP_LOAD;
    ec_code_arg_assign(builder, op, &op->arg1, builder->map[bit]);
    ec_code_arg_set(&op->arg2, offset);
    ec_code_arg_set(&op->arg3, bit);
}

static void
ec_code_store(ec_code_builder_t *builder, uint32_t reg, uint32_t bit)
{
    ec_code_op_t *op;

    op = ec_code_op_next(builder);

    op->op = EC_GF_OP_STORE;
    ec_code_arg_use(builder, op, &op->arg1, builder->map[reg]);
    ec_code_arg_set(&op->arg2, 0);
    ec_code_arg_set(&op->arg3, bit);
}

static void
ec_code_copy(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    ec_code_op_t *op;

    op = ec_code_op_next(builder);

    op->op = EC_GF_OP_COPY;
    ec_code_arg_assign(builder, op, &op->arg1, builder->map[dst]);
    ec_code_arg_use(builder, op, &op->arg2, builder->map[src]);
    ec_code_arg_set(&op->arg3, 0);
}

static void
ec_code_xor2(ec_code_builder_t *builder, uint32_t dst, uint32_t src)
{
    ec_code_op_t *op;

    op = ec_code_op_next(builder);

    op->op = EC_GF_OP_XOR2;
    ec_code_arg_update(builder, op, &op->arg1, builder->map[dst]);
    ec_code_arg_use(builder, op, &op->arg2, builder->map[src]);
    ec_code_arg_set(&op->arg3, 0);
}

static void
ec_code_xor3(ec_code_builder_t *builder, uint32_t dst, uint32_t src1,
             uint32_t src2)
{
    ec_code_op_t *op;

    if (builder->code->gen->xor3 == NULL) {
        ec_code_copy(builder, dst, src1);
        ec_code_xor2(builder, dst, src2);

        return;
    }

    op = ec_code_op_next(builder);

    op->op = EC_GF_OP_XOR3;
    ec_code_arg_assign(builder, op, &op->arg1, builder->map[dst]);
    ec_code_arg_use(builder, op, &op->arg2, builder->map[src1]);
    ec_code_arg_use(builder, op, &op->arg3, builder->map[src2]);
}

static void
ec_code_xorm(ec_code_builder_t *builder, uint32_t bit, uint32_t offset)
{
    ec_code_op_t *op;

    op = ec_code_op_next(builder);

    op->op = EC_GF_OP_XORM;
    ec_code_arg_update(builder, op, &op->arg1, builder->map[bit]);
    ec_code_arg_set(&op->arg2, offset);
    ec_code_arg_set(&op->arg3, bit);
}

static void
ec_code_dup(ec_code_builder_t *builder, ec_gf_op_t *op)
{
    switch (op->op) {
    case EC_GF_OP_COPY:
        ec_code_copy(builder, op->arg1, op->arg2);
        break;
    case EC_GF_OP_XOR2:
        ec_code_xor2(builder, op->arg1, op->arg2);
        break;
    case EC_GF_OP_XOR3:
        ec_code_xor3(builder, op->arg1, op->arg2, op->arg3);
        break;
    default:
        break;
    }
}

static void
ec_code_gf_load(ec_code_builder_t *builder, uint32_t offset)
{
    uint32_t i;

    for (i = 0; i < builder->code->gf->bits; i++) {
        ec_code_load(builder, i, offset);
    }
}

static void
ec_code_gf_load_xor(ec_code_builder_t *builder, uint32_t offset)
{
    uint32_t i;

    for (i = 0; i < builder->code->gf->bits; i++) {
        ec_code_xorm(builder, i, offset);
    }
}

static void
ec_code_gf_store(ec_code_builder_t *builder)
{
    uint32_t i;

    for (i = 0; i < builder->code->gf->bits; i++) {
        ec_code_store(builder, i, i);
    }
}

static void
ec_code_gf_clear(ec_code_builder_t *builder)
{
    uint32_t i;

    ec_code_xor2(builder, 0, 0);
    for (i = 0; i < builder->code->gf->bits; i++) {
        ec_code_store(builder, 0, i);
    }
}

static void
ec_code_gf_mul(ec_code_builder_t *builder, uint32_t value)
{
    ec_gf_mul_t *mul;
    ec_gf_op_t *op;
    uint32_t map[EC_GF_MAX_REGS];
    int32_t i;

    mul = builder->code->gf->table[value];
    for (op = mul->ops; op->op != EC_GF_OP_END; op++) {
        ec_code_dup(builder, op);
    }

    for (i = 0; i < mul->regs; i++) {
        map[i] = builder->map[mul->map[i]];
    }
    memcpy(builder->map, map, sizeof(uint32_t) * mul->regs);
}

static ec_code_builder_t *
ec_code_prepare(ec_code_t *code, uint32_t count, uint32_t width,
                gf_boolean_t linear)
{
    ec_code_builder_t *builder;
    uint32_t i;

    count *= code->gf->bits + code->gf->max_ops;
    count += code->gf->bits;
    builder = GF_MALLOC(sizeof(ec_code_builder_t) +
                        sizeof(ec_code_op_t) * count, ec_mt_ec_code_builder_t);
    if (builder == NULL) {
        return EC_ERR(ENOMEM);
    }

    builder->address = 0;
    builder->code = code;
    builder->size = 0;
    builder->count = 0;
    builder->regs = 0;
    builder->error = 0;
    builder->bits = code->gf->bits;
    builder->width = width;
    builder->data = NULL;
    builder->linear = linear;
    builder->base = -1;

    for (i = 0; i < EC_GF_MAX_REGS; i++) {
        builder->map[i] = i;
    }

    return builder;
}

static size_t
ec_code_space_size(void)
{
    return (sizeof(ec_code_space_t) + 15) & ~15;
}

static size_t
ec_code_chunk_size(void)
{
    return (sizeof(ec_code_chunk_t) + 15) & ~15;
}

static ec_code_chunk_t *
ec_code_chunk_from_space(ec_code_space_t *space)
{
    return (ec_code_chunk_t *)((uintptr_t)space + ec_code_space_size());
}

static void *
ec_code_to_executable(ec_code_space_t *space, void *addr)
{
        return (void *)((uintptr_t)addr - (uintptr_t)space
                                        + (uintptr_t)space->exec);
}

static void *
ec_code_from_executable(ec_code_space_t *space, void *addr)
{
        return (void *)((uintptr_t)addr - (uintptr_t)space->exec
                                        + (uintptr_t)space);
}

static void *
ec_code_func_from_chunk(ec_code_chunk_t *chunk, void **exec)
{
    void *addr;

    addr = (void *)((uintptr_t)chunk + ec_code_chunk_size());

    *exec = ec_code_to_executable(chunk->space, addr);

    return addr;
}

static ec_code_chunk_t *
ec_code_chunk_from_func(ec_code_func_linear_t func)
{
    ec_code_chunk_t *chunk;

    chunk = (ec_code_chunk_t *)((uintptr_t)func - ec_code_chunk_size());

    return ec_code_from_executable(chunk->space, chunk);
}

static ec_code_chunk_t *
ec_code_chunk_split(ec_code_chunk_t *chunk, size_t size)
{
    ec_code_chunk_t *extra;
    ssize_t avail;

    avail = chunk->size - size - ec_code_chunk_size();
    if (avail > 0) {
        extra = (ec_code_chunk_t *)((uintptr_t)chunk + chunk->size - avail);
        extra->space = chunk->space;
        extra->size = avail;
        list_add(&extra->list, &chunk->list);
        chunk->size = size;
    }
    list_del_init(&chunk->list);

    return chunk;
}

static gf_boolean_t
ec_code_chunk_touch(ec_code_chunk_t *prev, ec_code_chunk_t *next)
{
    uintptr_t end;

    end = (uintptr_t)prev + ec_code_chunk_size() + prev->size;
    return (end == (uintptr_t)next);
}

static ec_code_space_t *
ec_code_space_create(ec_code_t *code, size_t size)
{
        char path[] = GLUSTERFS_LIBEXECDIR "/ec-code-dynamic.XXXXXX";
        ec_code_space_t *space;
        void *exec;
        int32_t fd, err;

        /* We need to create memory areas to store the generated dynamic code.
         * Obviously these areas need to be written to be able to create the
         * code and they also need to be executable to execute it.
         *
         * However it's a bad practice to have a memory region that is both
         * writable *and* executable. In fact, selinux forbids this and causes
         * attempts to do so to fail (unless specifically configured).
         *
         * To solve the problem we'll use two distinct memory areas mapped to
         * the same physical storage. One of the memory areas will have write
         * permission, and the other will have execute permission. Both areas
         * will have the same contents. The physical storage will be a regular
         * file that will be mmapped to both areas.
         */

        /* We need to create a temporary file as the backend storage for the
         * memory mapped areas. */
        fd = mkstemp(path);
        if (fd < 0) {
                err = errno;
                gf_msg(THIS->name, GF_LOG_ERROR, err, EC_MSG_DYN_CREATE_FAILED,
                       "Unable to create a temporary file for the ec dynamic "
                       "code");
                space = EC_ERR(err);
                goto done;
        }
        /* Once created we don't need to keep it in the file system. It will
         * still exist until we close the last file descriptor or unmap the
         * memory areas bound to the file. */
        sys_unlink(path);

        size = (size + EC_CODE_ALIGN - 1) & ~(EC_CODE_ALIGN - 1);
        if (sys_ftruncate(fd, size) < 0) {
                err = errno;
                gf_msg(THIS->name, GF_LOG_ERROR, err, EC_MSG_DYN_CREATE_FAILED,
                       "Unable to resize the file for the ec dynamic code");
                space = EC_ERR(err);
                goto done_close;
        }

        /* This creates an executable memory area to be able to run the
         * generated fragments of code. */
        exec = mmap(NULL, size, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
        if (exec == MAP_FAILED) {
                err = errno;
                gf_msg(THIS->name, GF_LOG_ERROR, err, EC_MSG_DYN_CREATE_FAILED,
                       "Unable to map the executable area for the ec dynamic "
                       "code");
                space = EC_ERR(err);
                goto done_close;
        }
        /* It's not important to check the return value of mlock(). If it fails
         * everything will continue to work normally. */
        mlock(exec, size);

        /* This maps a read/write memory area to be able to create the dynamici
         * code. */
        space = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (space == MAP_FAILED) {
                err = errno;
                gf_msg(THIS->name, GF_LOG_ERROR, err, EC_MSG_DYN_CREATE_FAILED,
                       "Unable to map the writable area for the ec dynamic "
                       "code");
                space = EC_ERR(err);

                munmap(exec, size);

                goto done_close;
        }

        space->exec = exec;
        space->size = size;
        space->code = code;
        list_add_tail(&space->list, &code->spaces);
        INIT_LIST_HEAD(&space->chunks);

done_close:
        /* If everything has succeeded, we already have the memory areas
         * mapped. We don't need the file descriptor anymore because the
         * backend storage will be there until the mmaped regions are
         * unmapped. */
        sys_close(fd);
done:
        return space;
}

static void
ec_code_space_destroy(ec_code_space_t *space)
{
        list_del_init(&space->list);

        munmap(space->exec, space->size);
        munmap(space, space->size);
}

static void
ec_code_chunk_merge(ec_code_chunk_t *chunk)
{
    ec_code_chunk_t *item, *tmp;

    list_for_each_entry_safe(item, tmp, &chunk->space->chunks, list) {
        if ((uintptr_t)item > (uintptr_t)chunk) {
            list_add_tail(&chunk->list, &item->list);
            if (ec_code_chunk_touch(chunk, item)) {
                chunk->size += item->size + ec_code_chunk_size();
                list_del_init(&item->list);
            }

            goto check;
        }
        if (ec_code_chunk_touch(item, chunk)) {
            item->size += chunk->size + ec_code_chunk_size();
            list_del_init(&item->list);
            chunk = item;
        }
    }
    list_add_tail(&chunk->list, &chunk->space->chunks);

check:
    if (chunk->size == chunk->space->size - ec_code_space_size() -
                       ec_code_chunk_size()) {
        ec_code_space_destroy(chunk->space);
    }
}

static ec_code_chunk_t *
ec_code_space_alloc(ec_code_t *code, size_t size)
{
    ec_code_space_t *space;
    ec_code_chunk_t *chunk;
    size_t map_size;

    /* To minimize fragmentation, we only allocate chunks of sizes multiples
     * of EC_CODE_CHUNK_MIN_SIZE. */
    size = ((size + ec_code_chunk_size() + EC_CODE_CHUNK_MIN_SIZE - 1) &
           ~(EC_CODE_CHUNK_MIN_SIZE - 1)) - ec_code_chunk_size();
    list_for_each_entry(space, &code->spaces, list) {
        list_for_each_entry(chunk, &space->chunks, list) {
            if (chunk->size >= size) {
                goto out;
            }
        }
    }

    map_size = EC_CODE_SIZE - ec_code_space_size() - ec_code_chunk_size();
    if (map_size < size) {
        map_size = size;
    }
    space = ec_code_space_create(code, map_size);
    if (EC_IS_ERR(space)) {
        return (ec_code_chunk_t *)space;
    }

    chunk = ec_code_chunk_from_space(space);
    chunk->size = map_size - ec_code_space_size() - ec_code_chunk_size();
    list_add(&chunk->list, &space->chunks);

out:
    chunk->space = space;

    return ec_code_chunk_split(chunk, size);
}

static ec_code_chunk_t *
ec_code_alloc(ec_code_t *code, uint32_t size)
{
    ec_code_chunk_t *chunk;

    LOCK(&code->lock);

    chunk = ec_code_space_alloc(code, size);

    UNLOCK(&code->lock);

    return chunk;
}

static void
ec_code_free(ec_code_chunk_t *chunk)
{
    gf_lock_t *lock;

    lock = &chunk->space->code->lock;
    LOCK(lock);

    ec_code_chunk_merge(chunk);

    UNLOCK(lock);
}

static int32_t
ec_code_write(ec_code_builder_t *builder)
{
    ec_code_gen_t *gen;
    ec_code_op_t *op;
    uint32_t i;

    builder->error = 0;
    builder->size = 0;
    builder->address = 0;
    builder->base = -1;

    gen = builder->code->gen;
    gen->prolog(builder);
    for (i = 0; i < builder->count; i++) {
        op = &builder->ops[i];
        switch (op->op) {
        case EC_GF_OP_LOAD:
            gen->load(builder, op->arg1.value, op->arg2.value, op->arg3.value);
            break;
        case EC_GF_OP_STORE:
            gen->store(builder, op->arg1.value, op->arg3.value);
            break;
        case EC_GF_OP_COPY:
            gen->copy(builder, op->arg1.value, op->arg2.value);
            break;
        case EC_GF_OP_XOR2:
            gen->xor2(builder, op->arg1.value, op->arg2.value);
            break;
        case EC_GF_OP_XOR3:
            gen->xor3(builder, op->arg1.value, op->arg2.value, op->arg3.value);
            break;
        case EC_GF_OP_XORM:
            gen->xorm(builder, op->arg1.value, op->arg2.value, op->arg3.value);
            break;
        default:
            break;
        }
    }
    gen->epilog(builder);

    return builder->error;
}

static void *
ec_code_compile(ec_code_builder_t *builder)
{
    ec_code_chunk_t *chunk;
    void *func;
    int32_t err;

    err = ec_code_write(builder);
    if (err != 0) {
        return EC_ERR(err);
    }

    chunk = ec_code_alloc(builder->code, builder->size);
    if (EC_IS_ERR(chunk)) {
        return chunk;
    }
    builder->data = ec_code_func_from_chunk(chunk, &func);

    err = ec_code_write(builder);
    if (err != 0) {
        ec_code_free(chunk);

        return EC_ERR(err);
    }

    GF_FREE(builder);

    return func;
}

ec_code_t *
ec_code_create(ec_gf_t *gf, ec_code_gen_t *gen)
{
    ec_code_t *code;

    code = GF_MALLOC(sizeof(ec_code_t), ec_mt_ec_code_t);
    if (code == NULL) {
        return EC_ERR(ENOMEM);
    }
    memset(code, 0, sizeof(ec_code_t));
    INIT_LIST_HEAD(&code->spaces);
    LOCK_INIT(&code->lock);

    code->gf = gf;
    code->gen = gen;

    return code;
}

void
ec_code_destroy(ec_code_t *code)
{
    if (!list_empty(&code->spaces)) {
    }

    LOCK_DESTROY(&code->lock);

    GF_FREE(code);
}

static uint32_t
ec_code_value_next(uint32_t *values, uint32_t count, uint32_t *offset)
{
    uint32_t i, next;

    next = 0;
    for (i = *offset + 1; i < count; i++) {
        next = values[i];
        if (next != 0) {
            break;
        }
    }
    *offset = i;

    return next;
}

static void *
ec_code_build_dynamic(ec_code_t *code, uint32_t width, uint32_t *values,
                      uint32_t count, gf_boolean_t linear)
{
        ec_code_builder_t *builder;
        uint32_t offset, val, next;

        builder = ec_code_prepare(code, count, width, linear);
        if (EC_IS_ERR(builder)) {
                return builder;
        }

        offset = -1;
        next = ec_code_value_next(values, count, &offset);
        if (next != 0) {
                ec_code_gf_load(builder, offset);
                do {
                        val = next;
                        next = ec_code_value_next(values, count, &offset);
                        if (next != 0) {
                                ec_code_gf_mul(builder, ec_gf_div(code->gf,
                                                                  val, next));
                                ec_code_gf_load_xor(builder, offset);
                        }
                } while (next != 0);
                ec_code_gf_mul(builder, val);
                ec_code_gf_store(builder);
        } else {
                ec_code_gf_clear(builder);
        }

        return ec_code_compile(builder);
}

static void *
ec_code_build(ec_code_t *code, uint32_t width, uint32_t *values,
              uint32_t count, gf_boolean_t linear)
{
        void *func;

        if (code->gen != NULL) {
                func = ec_code_build_dynamic(code, width, values, count,
                                             linear);
                if (!EC_IS_ERR(func)) {
                        return func;
                }

                gf_msg_debug(THIS->name, GF_LOG_DEBUG,
                             "Unable to generate dynamic code. Falling back "
                             "to precompiled code");

                /* The dynamic code generation shouldn't fail in normal
                 * conditions, but if it fails at some point, it's very
                 * probable that it will fail again, so we completely disable
                 * dynamic code generation. */
                code->gen = NULL;
        }

        ec_code_c_prepare(code->gf, values, count);

        if (linear) {
                return ec_code_c_linear;
        }

        return ec_code_c_interleaved;
}

ec_code_func_linear_t
ec_code_build_linear(ec_code_t *code, uint32_t width, uint32_t *values,
                     uint32_t count)
{
    return (ec_code_func_linear_t)ec_code_build(code, width, values, count,
                                                _gf_true);
}

ec_code_func_interleaved_t
ec_code_build_interleaved(ec_code_t *code, uint32_t width, uint32_t *values,
                          uint32_t count)
{
    return (ec_code_func_interleaved_t)ec_code_build(code, width, values,
                                                     count, _gf_false);
}

void
ec_code_release(ec_code_t *code, ec_code_func_t *func)
{
        if ((func->linear != ec_code_c_linear) &&
            (func->interleaved != ec_code_c_interleaved)) {
                ec_code_free(ec_code_chunk_from_func(func->linear));
        }
}

void
ec_code_error(ec_code_builder_t *builder, int32_t error)
{
    if (builder->error == 0) {
        gf_msg(THIS->name, GF_LOG_ERROR, error, EC_MSG_DYN_CODEGEN_FAILED,
               "Failed to generate dynamic code");
        builder->error = error;
    }
}

void
ec_code_emit(ec_code_builder_t *builder, uint8_t *bytes, uint32_t count)
{
    if (builder->error != 0) {
        return;
    }

    if (builder->data != NULL) {
        memcpy(builder->data + builder->size, bytes, count);
    }

    builder->size += count;
    builder->address += count;
}

static char *
ec_code_proc_trim_left(char *text, ssize_t *length)
{
    ssize_t len;

    for (len = *length; (len > 0) && isspace(*text); len--) {
        text++;
    }
    *length = len;

    return text;
}

static char *
ec_code_proc_trim_right(char *text, ssize_t *length, char sep)
{
    char *last;
    ssize_t len;

    len = *length;

    last = text;
    for (len = *length; (len > 0) && (*text != sep); len--) {
        if (!isspace(*text)) {
            last = text + 1;
        }
        text++;
    }
    *last = 0;
    *length = len;

    return text;
}

static char *
ec_code_proc_line_parse(ec_code_proc_t *file, ssize_t *length)
{
    char *text, *end;
    ssize_t len;

    len = file->size - file->pos;
    text = ec_code_proc_trim_left(file->buffer + file->pos, &len);
    end = ec_code_proc_trim_right(text, &len, '\n');
    if (len == 0) {
        if (!file->eof) {
            if (text == file->buffer) {
                file->size = file->pos = 0;
                file->skip = _gf_true;
            } else {
                file->size = file->pos = end - text;
                memmove(file->buffer, text, file->pos + 1);
            }
            len = sys_read(file->fd, file->buffer + file->pos,
                           sizeof(file->buffer) - file->pos - 1);
            if (len > 0) {
                file->size += len;
            }
            file->error = len < 0;
            file->eof = len <= 0;

            return NULL;
        }
        file->size = file->pos = 0;
    } else {
        file->pos = end - file->buffer + 1;
    }

    *length = end - text;

    if (file->skip) {
        file->skip = _gf_false;
        text = NULL;
    }

    return text;
}

static char *
ec_code_proc_line(ec_code_proc_t *file, ssize_t *length)
{
    char *text;

    text = NULL;
    while (!file->eof) {
        text = ec_code_proc_line_parse(file, length);
        if (text != NULL) {
            break;
        }
    }

    return text;
}

static char *
ec_code_proc_split(char *text, ssize_t *length, char sep)
{
    text = ec_code_proc_trim_right(text, length, sep);
    if (*length == 0) {
        return NULL;
    }
    (*length)--;
    text++;

    return ec_code_proc_trim_left(text, length);
}

static uint32_t
ec_code_cpu_check(uint32_t idx, char *list, uint32_t count)
{
    ec_code_gen_t *gen;
    char **ptr;
    char *table[count];
    uint32_t i;

    for (i = 0; i < count; i++) {
        table[i] = list;
        list += strlen(list) + 1;
    }

    gen = ec_code_gen_table[idx];
    while (gen != NULL) {
        for (ptr = gen->flags; *ptr != NULL; ptr++) {
            for (i = 0; i < count; i++) {
                if (strcmp(*ptr, table[i]) == 0) {
                    break;
                }
            }
            if (i >= count) {
                gen = ec_code_gen_table[++idx];
                break;
            }
        }
        if (*ptr == NULL) {
            break;
        }
    }

    return idx;
}

ec_code_gen_t *
ec_code_detect(xlator_t *xl, const char *def)
{
    ec_code_proc_t file;
    ec_code_gen_t *gen = NULL;
    char *line, *data, *list;
    ssize_t length;
    uint32_t count, base, select;

    if (strcmp(def, "none") == 0) {
        gf_msg(xl->name, GF_LOG_INFO, 0, EC_MSG_EXTENSION_NONE,
               "Not using any cpu extensions");

        return NULL;
    }

    file.fd = sys_open(PROC_CPUINFO, O_RDONLY, 0);
    if (file.fd < 0) {
        goto out;
    }
    file.size = file.pos = 0;
    file.eof = file.error = file.skip = _gf_false;

    select = 0;
    if (strcmp(def, "auto") != 0) {
        while (ec_code_gen_table[select] != NULL) {
            if (strcmp(ec_code_gen_table[select]->name, def) == 0) {
                break;
            }
            select++;
        }
        if (ec_code_gen_table[select] == NULL) {
            gf_msg(xl->name, GF_LOG_WARNING, EINVAL, EC_MSG_EXTENSION_UNKNOWN,
                   "CPU extension '%s' is not known. Not using any cpu "
                   "extensions", def);

            return NULL;
        }
    } else {
        def = NULL;
    }

    while ((line = ec_code_proc_line(&file, &length)) != NULL) {
        data = ec_code_proc_split(line, &length, ':');
        if ((data != NULL) && (strcmp(line, "flags") == 0)) {
            list = data;
            count = 0;
            while ((data != NULL) && (*data != 0)) {
                count++;
                data = ec_code_proc_split(data, &length, ' ');
            }
            base = select;
            select = ec_code_cpu_check(select, list, count);
            if ((base != select) && (def != NULL)) {
                gf_msg(xl->name, GF_LOG_WARNING, ENOTSUP,
                       EC_MSG_EXTENSION_UNSUPPORTED,
                       "CPU extension '%s' is not supported", def);
                def = NULL;
            }
        }
    }

    if (file.error) {
        gf_msg(xl->name, GF_LOG_WARNING, 0, EC_MSG_EXTENSION_FAILED,
               "Unable to detemine supported CPU extensions. Not using any "
               "cpu extensions");

        gen = NULL;
    } else {
        gen = ec_code_gen_table[select];
        if (gen == NULL) {
            gf_msg(xl->name, GF_LOG_INFO, 0, EC_MSG_EXTENSION_NONE,
                   "Not using any cpu extensions");
        } else {
            gf_msg(xl->name, GF_LOG_INFO, 0, EC_MSG_EXTENSION,
                   "Using '%s' CPU extensions", gen->name);
        }
    }

    sys_close(file.fd);

out:
    return gen;
}
