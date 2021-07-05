#ifndef _GF_BUF_H_
#define _GF_BUF_H_

#include "glusterfs/common-utils.h"

#ifndef GF_BUF_STACK_THRESHOLD
#define GF_BUF_STACK_THRESHOLD 64
#endif

#ifndef GF_BUF_ROUNDUP
#define GF_BUF_ROUNDUP 16
#endif

typedef struct gf_buf {
    unsigned long heap : 1;
    unsigned long allocated : __BITS_PER_LONG - 1;
    unsigned long string : 1;
    unsigned long used : __BITS_PER_LONG - 1;
    char *data;
} gf_buf_t;

#define gf_buf_decl_null(var)                                                  \
    gf_buf_t var                                                               \
        __attribute__((__cleanup__(gf_buf_cleanup))) = {0, 0, 0, 0, NULL}

#define gf_buf_decl_string(var)                                                \
    gf_buf_t var __attribute__((__cleanup__(gf_buf_cleanup))) = {0, 0, 1, 1, ""}

#define gf_buf_decl_init_data(var, ptr, size)                                  \
    gf_buf_t var __attribute__((__cleanup__(gf_buf_cleanup))) = {              \
        0, 0, 0, (size), (ptr)}

#define gf_buf_decl_init_string(var, str)                                      \
    gf_buf_t var __attribute__((__cleanup__(gf_buf_cleanup))) = {              \
        0, 0, 1, strlen(str) + 1, (str)}

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
gf_buf_cleanup(struct gf_buf *buf)
{
    if (buf->heap)
        __gf_buf_free(buf->data);
}

static inline size_t
__gf_buf_roundup(size_t size)
{
    return (size + (GF_BUF_ROUNDUP - 1)) & (-GF_BUF_ROUNDUP);
}

#define __gf_buf_assign(buf, ptr, size)                                        \
    do {                                                                       \
        size_t __sz = __gf_buf_roundup(size);                                  \
        if (__sz > GF_BUF_STACK_THRESHOLD) {                                   \
            (buf)->data = memcpy(__gf_buf_malloc(__sz), (ptr), (size));        \
            (buf)->heap = 1;                                                   \
        } else {                                                               \
            (buf)->data = memcpy(alloca(__sz), (ptr), (size));                 \
            (buf)->heap = 0;                                                   \
        }                                                                      \
        (buf)->allocated = __sz;                                               \
    } while (0)

#define gf_buf_assign_string(buf, str)                                         \
    do {                                                                       \
        size_t __nr = strlen(str) + 1;                                         \
        if ((buf)->allocated >= __nr)                                          \
            memcpy((buf)->data, (str), __nr);                                  \
        else {                                                                 \
            if ((buf)->heap)                                                   \
                __gf_buf_free((buf)->data);                                    \
            __gf_buf_assign((buf), (str), __nr);                               \
        }                                                                      \
        (buf)->used = __nr;                                                    \
        (buf)->string = 1;                                                     \
    } while (0)

#define gf_buf_assign_data(buf, ptr, size)                                     \
    do {                                                                       \
        if ((buf)->allocated >= (size))                                        \
            memcpy((buf)->data, (ptr), (size));                                \
        else {                                                                 \
            if ((buf)->heap)                                                   \
                __gf_buf_free((buf)->data);                                    \
            __gf_buf_assign((buf), (ptr), (size));                             \
        }                                                                      \
        (buf)->used = size;                                                    \
        (buf)->string = 0;                                                     \
    } while (0)

#define gf_buf_reset(buf)                                                      \
    do {                                                                       \
        if ((buf)->heap)                                                       \
            __gf_free((buf)->data);                                            \
        (buf)->data = NULL;                                                    \
        (buf)->allocated = 0;                                                  \
        (buf)->used = 0;                                                       \
        (buf)->string = 0;                                                     \
        (buf)->heap = 0;                                                       \
    } while (0)

#define __gf_buf_alloc_minimal(buf)                                            \
    do {                                                                       \
        (buf)->data = alloca(GF_BUF_ROUNDUP);                                  \
        (buf)->allocated = GF_BUF_ROUNDUP;                                     \
        (buf)->heap = 0;                                                       \
    } while (0)

#define gf_buf_clear_string(buf)                                               \
    do {                                                                       \
        if ((buf)->allocated == 0)                                             \
            __gf_buf_alloc_minimal(buf);                                       \
        (buf)->data[0] = '\0';                                                 \
        (buf)->used = 1;                                                       \
        (buf)->string = 1;                                                     \
    } while (0)

#define gf_buf_clear_data(buf)                                                 \
    do {                                                                       \
        if ((buf)->allocated == 0)                                             \
            __gf_buf_alloc_minimal(buf);                                       \
        memset((buf)->data, 0, (buf)->allocated);                              \
        (buf)->used = 0;                                                       \
        (buf)->string = 0;                                                     \
    } while (0)

#define gf_buf_copy(dst, src)                                                  \
    do {                                                                       \
        if ((dst)->allocated >= (src)->used)                                   \
            memcpy((dst)->data, (src)->data, (src)->used);                     \
        else {                                                                 \
            if ((dst)->heap)                                                   \
                __gf_buf_free((dst)->data);                                    \
            __gf_buf_assign((dst), (src)->data, (src)->used);                  \
        }                                                                      \
        (dst)->used = (src)->used;                                             \
        (dst)->string = (src)->string;                                         \
    } while (0)

#define gf_buf_equal(buf0, buf1)                                               \
    ((buf0)->used == ((buf1)->used) &&                                         \
     !memcmp((buf0)->data, (buf1)->data, (buf0)->used))

#define gf_buf_concat(dst, src)                                                \
    do {                                                                       \
        char *__tmp = NULL;                                                    \
        size_t __of = (dst)->string ? 1 : 0;                                   \
        size_t __sz = (dst)->used + (src)->used - __of, __nr = __sz;           \
        if ((dst)->allocated >= __sz)                                          \
            memcpy((dst)->data + (dst)->used - __of, (src)->data,              \
                   (src)->used);                                               \
        else {                                                                 \
            __nr = __gf_buf_roundup(__sz);                                     \
            if (__nr > GF_BUF_STACK_THRESHOLD)                                 \
                __tmp = __gf_buf_malloc(__nr);                                 \
            else                                                               \
                __tmp = alloca(__nr);                                          \
            memcpy(__tmp, (dst)->data, (dst)->used - __of);                    \
            memcpy(__tmp + (dst)->used - __of, (src)->data, (src)->used);      \
            if ((dst)->heap)                                                   \
                __gf_buf_free((dst)->data);                                    \
            (dst)->data = __tmp;                                               \
            (dst)->heap = !!(__nr > GF_BUF_STACK_THRESHOLD);                   \
            (dst)->allocated = __nr;                                           \
        }                                                                      \
        (dst)->used = __sz;                                                    \
        (dst)->string = (src)->string;                                         \
    } while (0)

#define gf_buf_concat_string(buf, str)                                         \
    do {                                                                       \
        char *__tmp = NULL;                                                    \
        size_t __of = (buf)->string ? 1 : 0, __ln = strlen(str) + 1;           \
        size_t __sz = (buf)->used + strlen(str) + ((buf)->string ? 0 : 1),     \
               __nr = __sz;                                                    \
        if ((buf)->allocated >= __sz)                                          \
            memcpy((buf)->data + (buf)->used - __of, (str), __ln);             \
        else {                                                                 \
            __nr = __gf_buf_roundup(__sz);                                     \
            if (__nr > GF_BUF_STACK_THRESHOLD)                                 \
                __tmp = __gf_buf_malloc(__nr);                                 \
            else                                                               \
                __tmp = alloca(__nr);                                          \
            memcpy(__tmp, (buf)->data, (buf)->used - __of);                    \
            memcpy(__tmp + (buf)->used - __of, (str), __ln);                   \
            if ((buf)->heap)                                                   \
                __gf_buf_free((buf)->data);                                    \
            (buf)->data = __tmp;                                               \
            (buf)->heap = !!(__nr > GF_BUF_STACK_THRESHOLD);                   \
            (buf)->allocated = __nr;                                           \
        }                                                                      \
        (buf)->used = __sz;                                                    \
        (buf)->string = 1;                                                     \
    } while (0)

#define gf_buf_sprintf(buf, ...)                                               \
    do {                                                                       \
        size_t __sz = snprintf(NULL, 0, __VA_ARGS__) + 1;                      \
        if ((buf)->allocated < __sz) {                                         \
            if ((buf)->heap)                                                   \
                __gf_buf_free((buf)->data);                                    \
            __sz = __gf_buf_roundup(__sz);                                     \
            if (__sz > GF_BUF_STACK_THRESHOLD) {                               \
                (buf)->data = __gf_buf_malloc(__sz);                           \
                (buf)->heap = 1;                                               \
            } else {                                                           \
                (buf)->data = alloca(__sz);                                    \
                (buf)->heap = 0;                                               \
            }                                                                  \
            (buf)->allocated = __sz;                                           \
        }                                                                      \
        (buf)->used = sprintf((buf)->data, __VA_ARGS__) + 1;                   \
        (buf)->string = 1;                                                     \
    } while (0)

#define gf_buf_compact(buf)                                                    \
    do {                                                                       \
        size_t __sz = ((buf)->used ? __gf_buf_roundup((buf)->used)             \
                                   : GF_BUF_ROUNDUP);                          \
        if ((buf)->allocated > __sz) {                                         \
            char *__tmp = NULL;                                                \
            if (__sz > GF_BUF_STACK_THRESHOLD)                                 \
                __tmp = __gf_buf_malloc(__sz);                                 \
            else                                                               \
                __tmp = alloca(__sz);                                          \
            memcpy(__tmp, (buf)->data, (buf)->used);                           \
            if ((buf)->heap)                                                   \
                __gf_buf_free((buf)->data);                                    \
            (buf)->data = __tmp;                                               \
            (buf)->heap = !!(__sz > GF_BUF_STACK_THRESHOLD);                   \
            (buf)->allocated = __sz;                                           \
        }                                                                      \
    } while (0)

#define gf_buf_data(buf) ((buf)->used ? (buf)->data : NULL)

#define gf_buf_data_size(buf) ((buf)->used)

#define gf_buf_copyout(buf)                                                    \
    ((buf)->used ? gf_memdup((buf)->data, ((buf)->used)) : NULL)

#endif /* _GF_BUF_H_ */
