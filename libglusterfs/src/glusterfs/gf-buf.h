#ifndef _GF_BUF_H_
#define _GF_BUF_H_

#include "glusterfs/common-utils.h"

#ifndef GF_BUF_STACK_THRESHOLD
#define GF_BUF_STACK_THRESHOLD 64
#endif

#ifndef GF_BUF_ROUNDUP
#define GF_BUF_ROUNDUP 16
#endif

#ifndef GF_ALLOCA_ALIGN
#define GF_ALLOCA_ALIGN 16
#endif

typedef struct gf_buf {
    unsigned long heap : 1;
    unsigned long allocated : __BITS_PER_LONG - 1;
    unsigned long string : 1;
    unsigned long used : __BITS_PER_LONG - 1;
    char *data;
} gf_buf_t;

static inline void *
__gf_buf_malloc(size_t size)
{
    return GF_MALLOC((size), gf_common_mt_char);
}

static inline void
__gf_buf_free(void *ptr)
{
    GF_FREE(ptr);
}

static inline void
__gf_buf_cleanup(struct gf_buf *buf)
{
    if (buf->heap)
        __gf_buf_free(buf->data);
}

static inline size_t
__gf_buf_roundup(size_t size)
{
    return (size + (GF_BUF_ROUNDUP - 1)) & (-GF_BUF_ROUNDUP);
}

#ifdef HAVE_BUILTIN_ALLOCA_WITH_ALIGN

#define __gf_buf_alloca(size)                                                  \
    __builtin_alloca_with_align((size), GF_ALLOCA_ALIGN * CHAR_BIT)

#else /* not HAVE_BUILTIN_ALLOCA_WITH_ALIGN */

#define __gf_buf_alloca(size) __builtin_alloca(size)

#endif /* HAVE_BUILTIN_ALLOCA_WITH_ALIGN */

#define gf_buf_decl_null(var, size)                                            \
    gf_buf_t var __attribute__((__cleanup__(__gf_buf_cleanup))) = {            \
        !!(__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD),                   \
        __gf_buf_roundup(size), 0, 0,                                          \
        (__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD)                      \
            ? ({                                                               \
                  char *__tmp = __gf_buf_malloc(__gf_buf_roundup(size));       \
                  if (__tmp)                                                   \
                      memset(__tmp, 0, (size));                                \
                  __tmp;                                                       \
              })                                                               \
            : ({                                                               \
                  char *__tmp = __gf_buf_alloca(__gf_buf_roundup(size));       \
                  memset(__tmp, 0, (size));                                    \
                  __tmp;                                                       \
              })}

#define gf_buf_decl_string(var, size)                                          \
    gf_buf_t var __attribute__((__cleanup__(__gf_buf_cleanup))) = {            \
        !!(__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD),                   \
        __gf_buf_roundup(size), 1, 1,                                          \
        (__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD)                      \
            ? ({                                                               \
                  char *__tmp = __gf_buf_malloc(__gf_buf_roundup(size));       \
                  if (__tmp)                                                   \
                      *__tmp = '\0';                                           \
                  __tmp;                                                       \
              })                                                               \
            : ({                                                               \
                  char *__tmp = __gf_buf_alloca(__gf_buf_roundup(size));       \
                  *__tmp = '\0';                                               \
                  __tmp;                                                       \
              })}

#define gf_buf_decl_string_init(var, size, string)                             \
    gf_buf_t var __attribute__((__cleanup__(__gf_buf_cleanup))) = {            \
        !!(__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD),                   \
        __gf_buf_roundup(size), 1, strlen(string) + 1,                         \
        (__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD)                      \
            ? ({                                                               \
                  char *__tmp = __gf_buf_malloc(__gf_buf_roundup(size));       \
                  if (__tmp)                                                   \
                      memcpy(__tmp, (string), strlen(string) + 1);             \
                  __tmp;                                                       \
              })                                                               \
            : ({                                                               \
                  char *__tmp = __gf_buf_alloca(__gf_buf_roundup(size));       \
                  memcpy(__tmp, (string), strlen(string) + 1);                 \
                  __tmp;                                                       \
              })}

#define gf_buf_decl_data_init(var, size, data, datasize)                       \
    gf_buf_t var __attribute__((__cleanup__(__gf_buf_cleanup))) = {            \
        !!(__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD),                   \
        __gf_buf_roundup(size), 0, (datasize),                                 \
        (__gf_buf_roundup(size) > GF_BUF_STACK_THRESHOLD)                      \
            ? ({                                                               \
                  char *__tmp = __gf_buf_malloc(__gf_buf_roundup(size));       \
                  if (__tmp)                                                   \
                      memcpy(__tmp, (data), (datasize));                       \
                  __tmp;                                                       \
              })                                                               \
            : ({                                                               \
                  char *__tmp = __gf_buf_alloca(__gf_buf_roundup(size));       \
                  memcpy(__tmp, (data), (datasize));                           \
                  __tmp;                                                       \
              })}

static inline int
gf_buf_error(gf_buf_t *buf)
{
    return buf->data == NULL;
}

static inline char *
gf_buf_data(gf_buf_t *buf)
{
    return buf->used ? buf->data : NULL;
}

static inline size_t
gf_buf_data_size(gf_buf_t *buf)
{
    return buf->used;
}

static inline char *
gf_buf_copyout(gf_buf_t *buf)
{
    return buf->used ? gf_memdup(buf->data, buf->used) : NULL;
}

static inline void
gf_buf_reset(gf_buf_t *buf)
{
    if (buf->heap)
        __gf_buf_free(buf->data);
    buf->data = NULL;
    buf->allocated = 0;
    buf->used = 0;
    buf->string = 0;
    buf->heap = 0;
}

static inline int
__gf_buf_alloc_minimal(gf_buf_t *buf)
{
    buf->data = __gf_buf_malloc(GF_BUF_ROUNDUP);
    if (buf->data) {
        buf->allocated = GF_BUF_ROUNDUP;
        buf->heap = 1;
        return 0;
    }
    return 1;
}

static inline void
gf_buf_clear_string(gf_buf_t *buf)
{
    if (buf->allocated == 0)
        if (__gf_buf_alloc_minimal(buf))
            return;
    buf->data[0] = '\0';
    buf->used = 1;
    buf->string = 1;
}

static inline void
gf_buf_clear_data(gf_buf_t *buf)
{
    if (buf->allocated == 0)
        if (__gf_buf_alloc_minimal(buf))
            return;
    memset(buf->data, 0, buf->allocated);
    buf->used = 0;
    buf->string = 0;
}

static inline int
__gf_buf_assign(gf_buf_t *buf, char *ptr, size_t size)
{
    size_t __sz = __gf_buf_roundup(size);

    if (__sz > buf->allocated) {
        buf->data = __gf_buf_malloc(__sz);
        if (buf->data) {
            memcpy(buf->data, ptr, size);
            buf->allocated = __sz;
            buf->heap = 1;
            return 0;
        } else
            buf->allocated = 0;
    }
    return 1;
}

static inline void
gf_buf_assign_string(gf_buf_t *buf, char *str)
{
    size_t __sz = strlen(str) + 1;

    if (buf->allocated >= __sz)
        memcpy(buf->data, str, __sz);
    else {
        if (buf->heap)
            __gf_buf_free(buf->data);
        if (__gf_buf_assign(buf, str, __sz))
            return;
    }
    buf->used = __sz;
    buf->string = 1;
}

static inline void
gf_buf_assign_data(gf_buf_t *buf, char *ptr, size_t size)
{
    if (buf->allocated >= size)
        memcpy(buf->data, ptr, size);
    else {
        if (buf->heap)
            __gf_buf_free(buf->data);
        if (__gf_buf_assign(buf, ptr, size))
            return;
    }
    buf->used = size;
    buf->string = 0;
}

static inline void
gf_buf_copy(gf_buf_t *dst, gf_buf_t *src)
{
    if (dst->allocated >= src->used)
        memcpy(dst->data, src->data, src->used);
    else {
        if (dst->heap)
            __gf_buf_free(dst->data);
        if (__gf_buf_assign(dst, src->data, src->used))
            return;
    }
    dst->used = src->used;
    dst->string = src->string;
}

static inline int
gf_buf_equal(gf_buf_t *buf0, gf_buf_t *buf1)
{
    return buf0->used == buf1->used &&
           !memcmp(buf0->data, buf1->data, buf0->used);
}

static inline int
gf_buf_join(gf_buf_t *dst, gf_buf_t *src)
{
    size_t *__sz;

    if (dst->string || src->string)
        return 1;
    __sz = dst->used + src->used;

    if (dst->allocated >= __sz)
        memcpy(dst->data + dst->used, src->data, src->used);
    else {
        size_t __nr = __gf_buf_roundup(__sz);
        char *__tmp = __gf_buf_malloc(__nr);

        if (__tmp) {
            memcpy(__tmp, dst->data, dst->used);
            memcpy(__tmp + dst->used, src->data, src->used);
            if (dst->heap)
                __gf_buf_free(dst->data);
            dst->data = __tmp;
            dst->heap = 1;
            dst->allocated = __nr;
        } else
            return 1;
    }

    dst->used = __sz;
    return 0;
}

static inline int
gf_buf_concat(gf_buf_t *dst, gf_buf_t *src)
{
    size_t *__sz;

    if (!dst->string || !src->string)
        return 1;
    __sz = dst->used + src->used - 1;

    if (dst->allocated >= __sz)
        memcpy(dst->data + dst->used - 1, src->data, src->used);
    else {
        size_t __nr = __gf_buf_roundup(__sz);
        char *__tmp = __gf_buf_malloc(__nr);

        if (__tmp) {
            memcpy(__tmp, dst->data, dst->used - 1);
            memcpy(__tmp + dst->used - 1, src->data, src->used);
            if (dst->heap)
                __gf_buf_free(dst->data);
            dst->data = __tmp;
            dst->heap = 1;
            dst->allocated = __nr;
        } else
            return 1;
    }

    dst->used = __sz;
    return 0;
}

static inline int
gf_buf_concat_string(gf_buf_t *dst, char *str)
{
    size_t *__sz, __ln;

    if (!dst->string || !str)
        return 1;

    __ln = strlen(str);
    __sz = dst->used + __ln;

    if (dst->allocated >= __sz)
        memcpy(dst->data + dst->used - 1, str, __ln + 1);
    else {
        size_t __nr = __gf_buf_roundup(__sz);
        char *__tmp = __gf_buf_malloc(__nr);

        if (__tmp) {
            memcpy(__tmp, dst->data, dst->used - 1);
            memcpy(__tmp + dst->used - 1, str, __ln + 1);
            if (dst->heap)
                __gf_buf_free(dst->data);
            dst->data = __tmp;
            dst->heap = 1;
            dst->allocated = __nr;
        } else
            return 1;
    }

    dst->used = __sz;
    return 0;
}

static inline int
gf_buf_sprintf(gf_buf_t *buf, const char *fmt, ...)
{
    va_list ap;
    size_t __sz;

    va_start(ap, fmt);
    __sz = vsnprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    if (buf->allocated < __sz) {
        char *__tmp;

        __sz = __gf_buf_roundup(__sz);
        __tmp = __gf_buf_malloc(__sz);
        if (__tmp) {
            if (buf->heap)
                __gf_buf_free(buf->data);
            buf->heap = 1;
            buf->data = __tmp;
            buf->allocated = __sz;
        } else
            return 1;
    }

    va_start(ap, fmt);
    buf->used = vsprintf(buf->data, fmt, ap) + 1;
    va_end(ap);

    buf->string = 1;
    return 0;
}

static inline int
gf_buf_snprintf(gf_buf_t *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    size_t __sz, __nr;

    va_start(ap, fmt);
    __sz = snprintf(NULL, 0, fmt, ap) + 1;
    va_end(ap);

    if (__sz > size)
        __sz = size;
    if (buf->allocated < __sz) {
        char *__tmp;

        __sz = __gf_buf_roundup(__sz);
        __tmp = __gf_buf_malloc(__sz);
        if (__tmp) {
            if (buf->heap)
                __gf_buf_free(buf->data);
            buf->heap = 1;
            buf->data = __tmp;
            buf->allocated = __sz;
        } else
            return 1;
    }

    va_start(ap, fmt);
    __nr = vsnprintf(buf->data, size, fmt, ap) + 1;
    va_end(ap);

    buf->used = (__nr > size ? size : __nr);
    buf->string = 1;
    return 0;
}

static inline int
gf_buf_compact(gf_buf_t *buf)
{
    size_t __sz = (buf->used ?
                   __gf_buf_roundup(buf->used) :
                   GF_BUF_ROUNDUP);

    if (buf->allocated > __sz) {
        char *__tmp = __gf_buf_malloc(__sz);
        if (__tmp) {
            memcpy(__tmp, buf->data, buf->used);
            if (buf->heap)
                __gf_buf_free(buf->data);
            buf->data = __tmp;
            buf->heap = 1;
            buf->allocated = __sz;
        }
    }
}

#endif /* _GF_BUF_H_ */
