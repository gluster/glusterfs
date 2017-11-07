/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _ATOMIC_H
#define _ATOMIC_H

#include <inttypes.h>
#include <stdbool.h>

#include "locking.h"

/* Macros used to join two arguments and generate a new macro name. */
#define GF_ATOMIC_MACRO_1(_macro) _macro
#define GF_ATOMIC_MACRO(_base, _name) GF_ATOMIC_MACRO_1(_base##_name)

/* There's a problem on 32-bit architectures when we try to use atomic
 * builtins with 64-bit types. Only way to solve the problem is to use
 * a mutex to protect the access to the atomic, but we don't want to
 * use mutexes for other smaller types that could work with the atomic
 * builtins.
 *
 * So on each atomic type we add a field for the mutex if atomic operation
 * is not supported and a dummy zero size field if it's supported. This way
 * we can have different atomic types, some with a mutex and some without.
 *
 * To define these types, we use two macros:
 *
 *     GF_ATOMIC_MUTEX_FIELD_0 = char lk[0]
 *     GF_ATOMIC_MUTEX_FILED_1 = gf_lock_t lk
 *
 * Both macros define the 'lk' field that will be used in the atomic
 * structure. One when the atomic is supported by the architecture and
 * another when not. We need to define the field even if it won't be
 * used. Otherwise the compiler will return an error.
 *
 * Now we need to take the mutex or not depending on the existence of
 * the mutex field in the structure. To do so we check the size of the
 * structure, and if it's bigger than uint64_t (all structures with a
 * mutex will be bigger), we use the mutex-based version. Otherwise we
 * use the atomic builtin. This check is easily optimized out by the
 * compiler, leaving a clean and efficient compiled code. */

#define GF_ATOMIC_MUTEX_FIELD_0 char lk[0]
#define GF_ATOMIC_MUTEX_FIELD_1 gf_lock_t lk

/* We'll use SIZEOF_LONG to determine the architecture. 32-bit machines
 * will have 4 here, while 64-bit machines will have 8. If additional
 * needs or restrictions appear on other platforms, these tests can be
 * extended to handle them. */

/* GF_ATOMIC_SIZE_X macros map each type size to one of the
 * GF_ATOMIC_MUTEX_FIELD_X macros, depending on detected conditions. */

#if defined(HAVE_ATOMIC_BUILTINS) || defined(HAVE_SYNC_BUILTINS)

#define GF_ATOMIC_SIZE_1 GF_ATOMIC_MUTEX_FIELD_0
#define GF_ATOMIC_SIZE_2 GF_ATOMIC_MUTEX_FIELD_0
#define GF_ATOMIC_SIZE_4 GF_ATOMIC_MUTEX_FIELD_0

#if SIZEOF_LONG >= 8
#define GF_ATOMIC_SIZE_8 GF_ATOMIC_MUTEX_FIELD_0
#endif

#endif /* HAVE_(ATOMIC|SYNC)_BUILTINS */

/* Any GF_ATOMIC_SIZE_X macro not yet defined will use the mutex version */
#ifndef GF_ATOMIC_SIZE_1
#define GF_ATOMIC_SIZE_1 GF_ATOMIC_MUTEX_FIELD_1
#endif

#ifndef GF_ATOMIC_SIZE_2
#define GF_ATOMIC_SIZE_2 GF_ATOMIC_MUTEX_FIELD_1
#endif

#ifndef GF_ATOMIC_SIZE_4
#define GF_ATOMIC_SIZE_4 GF_ATOMIC_MUTEX_FIELD_1
#endif

#ifndef GF_ATOMIC_SIZE_8
#define GF_ATOMIC_SIZE_8 GF_ATOMIC_MUTEX_FIELD_1
#endif

/* This macro is used to define all atomic types supported. First field
 * represents the size of the type in bytes, and the second one the name. */
#define GF_ATOMIC_TYPE(_size, _name) \
        typedef struct _gf_atomic_##_name##_t { \
                GF_ATOMIC_MACRO(GF_ATOMIC_SIZE_, _size); \
                _name##_t value; \
        } gf_atomic_##_name##_t

/* The atomic types we support */
GF_ATOMIC_TYPE(1, int8);              /* gf_atomic_int8_t */
GF_ATOMIC_TYPE(2, int16);             /* gf_atomic_int16_t */
GF_ATOMIC_TYPE(4, int32);             /* gf_atomic_int32_t */
GF_ATOMIC_TYPE(8, int64);             /* gf_atomic_int64_t */
GF_ATOMIC_TYPE(SIZEOF_LONG, intptr);  /* gf_atomic_intptr_t */
GF_ATOMIC_TYPE(1, uint8);             /* gf_atomic_uint8_t */
GF_ATOMIC_TYPE(2, uint16);            /* gf_atomic_uint16_t */
GF_ATOMIC_TYPE(4, uint32);            /* gf_atomic_uint32_t */
GF_ATOMIC_TYPE(8, uint64);            /* gf_atomic_uint64_t */
GF_ATOMIC_TYPE(SIZEOF_LONG, uintptr); /* gf_atomic_uintptr_t */

/* Define the default atomic type as int64_t */
#define gf_atomic_t gf_atomic_int64_t

/* This macro will choose between the mutex based version and the atomic
 * builtin version depending on the size of the atomic structure. */
#define GF_ATOMIC_CHOOSE(_atomic, _op, _args...) \
        ((sizeof(_atomic) > sizeof(uint64_t)) \
            ? ({ GF_ATOMIC_MACRO(GF_ATOMIC_LOCK_, _op)(_atomic, ## _args); }) \
            : ({ GF_ATOMIC_MACRO(GF_ATOMIC_BASE_, _op)(_atomic, ## _args); }))

/* Macros to implement the mutex-based atomics. */
#define GF_ATOMIC_OP_PREPARE(_atomic, _name) \
        typeof(_atomic) *__atomic = &(_atomic); \
        gf_lock_t *__lock = (gf_lock_t *)&__atomic->lk; \
        LOCK(__lock); \
        typeof(__atomic->value) _name = __atomic->value

#define GF_ATOMIC_OP_STORE(_value) \
        (__atomic->value = (_value))

#define GF_ATOMIC_OP_RETURN(_value) \
        ({ \
                UNLOCK(__lock); \
                _value; \
        })

#define GF_ATOMIC_LOCK_INIT(_atomic, _value) \
        do { \
                typeof(_atomic) *__atomic = &(_atomic); \
                LOCK_INIT((gf_lock_t *)&__atomic->lk); \
                __atomic->value = (_value); \
        } while (0)

#define GF_ATOMIC_LOCK_GET(_atomic) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_ADD(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value += (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_SUB(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value -= (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_AND(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value &= (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_OR(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value |= (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_XOR(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value ^= (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_NAND(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value = ~(__value & (_value))); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_FETCH_ADD(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value + (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_FETCH_SUB(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value - (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_FETCH_AND(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value & (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_FETCH_OR(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value | (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_FETCH_XOR(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(__value ^ (_value)); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_FETCH_NAND(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(~(__value & (_value))); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_SWAP(_atomic, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                GF_ATOMIC_OP_STORE(_value); \
                GF_ATOMIC_OP_RETURN(__value); \
        })

#define GF_ATOMIC_LOCK_CMP_SWAP(_atomic, _expected, _value) \
        ({ \
                GF_ATOMIC_OP_PREPARE(_atomic, __value); \
                bool __ret = (__value == (_expected)); \
                if (__ret) { \
                        GF_ATOMIC_OP_STORE(_value); \
                } \
                GF_ATOMIC_OP_RETURN(__ret); \
        })

#if defined(HAVE_ATOMIC_BUILTINS)

/* If compiler supports __atomic builtins, we use them. */

#define GF_ATOMIC_BASE_INIT(_atomic, _value) \
        __atomic_store_n(&(_atomic).value, (_value), __ATOMIC_RELEASE)

#define GF_ATOMIC_BASE_GET(_atomic) \
        __atomic_load_n(&(_atomic).value, __ATOMIC_ACQUIRE)

#define GF_ATOMIC_BASE_ADD(_atomic, _value) \
        __atomic_add_fetch(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_SUB(_atomic, _value) \
        __atomic_sub_fetch(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_AND(_atomic, _value) \
        __atomic_and_fetch(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_OR(_atomic, _value) \
        __atomic_or_fetch(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_XOR(_atomic, _value) \
        __atomic_xor_fetch(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_NAND(_atomic, _value) \
        __atomic_nand_fetch(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_FETCH_ADD(_atomic, _value) \
        __atomic_fetch_add(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_FETCH_SUB(_atomic, _value) \
        __atomic_fetch_sub(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_FETCH_AND(_atomic, _value) \
        __atomic_fetch_and(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_FETCH_OR(_atomic, _value) \
        __atomic_fetch_or(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_FETCH_XOR(_atomic, _value) \
        __atomic_fetch_xor(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_FETCH_NAND(_atomic, _value) \
        __atomic_fetch_nand(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_SWAP(_atomic, _value) \
        __atomic_exchange_n(&(_atomic).value, (_value), __ATOMIC_ACQ_REL)

#define GF_ATOMIC_BASE_CMP_SWAP(_atomic, _expected, _value) \
        ({ \
                typeof((_atomic).value) __expected = (_expected); \
                __atomic_compare_exchange_n(&(_atomic).value, &__expected, \
                                            (_value), 0, __ATOMIC_ACQ_REL, \
                                            __ATOMIC_ACQUIRE); \
        })

#elif defined(HAVE_SYNC_BUILTINS)

/* If compiler doesn't support __atomic builtins but supports __sync builtins,
 * we use them. */

#define GF_ATOMIC_BASE_INIT(_atomic, _value) \
        do { \
                (_atomic).value = (_value); \
                __sync_synchronize(); \
        } while (0)

#define GF_ATOMIC_BASE_ADD(_atomic, _value) \
        __sync_add_and_fetch(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_SUB(_atomic, _value) \
        __sync_sub_and_fetch(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_AND(_atomic, _value) \
        __sync_and_and_fetch(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_OR(_atomic, _value) \
        __sync_or_and_fetch(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_XOR(_atomic, _value) \
        __sync_xor_and_fetch(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_NAND(_atomic, _value) \
        __sync_nand_and_fetch(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_FETCH_ADD(_atomic, _value) \
        __sync_fetch_and_add(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_FETCH_SUB(_atomic, _value) \
        __sync_fetch_and_sub(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_FETCH_AND(_atomic, _value) \
        __sync_fetch_and_and(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_FETCH_OR(_atomic, _value) \
        __sync_fetch_and_or(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_FETCH_XOR(_atomic, _value) \
        __sync_fetch_and_xor(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_FETCH_NAND(_atomic, _value) \
        __sync_fetch_and_nand(&(_atomic).value, (_value))

#define GF_ATOMIC_BASE_SWAP(_atomic, _value) \
        ({ \
                __sync_synchronize(); \
                __sync_lock_test_and_set(&(_atomic).value, (_value)); \
        })

#define GF_ATOMIC_BASE_CMP_SWAP(_atomic, _expected, _value) \
        __sync_bool_compare_and_swap(&(_atomic).value, (_expected), (_value))

#define GF_ATOMIC_BASE_GET(_atomic) GF_ATOMIC_BASE_ADD(_atomic, 0)

#else /* !HAVE_ATOMIC_BUILTINS && !HAVE_SYNC_BUILTINS */

/* The compiler doesn't support any atomic builtin. We fallback to the
 * mutex-based implementation. */

#define GF_ATOMIC_BASE_INIT(_atomic, _value) \
        GF_ATOMIC_LOCK_INIT(_atomic, _value)

#define GF_ATOMIC_BASE_GET(_atomic) \
        GF_ATOMIC_LOCK_GET(_atomic)

#define GF_ATOMIC_BASE_ADD(_atomic, _value) \
        GF_ATOMIC_LOCK_ADD(_atomic, _value)

#define GF_ATOMIC_BASE_SUB(_atomic, _value) \
        GF_ATOMIC_LOCK_SUB(_atomic, _value)

#define GF_ATOMIC_BASE_AND(_atomic, _value) \
        GF_ATOMIC_LOCK_AND(_atomic, _value)

#define GF_ATOMIC_BASE_OR(_atomic, _value) \
        GF_ATOMIC_LOCK_OR(_atomic, _value)

#define GF_ATOMIC_BASE_XOR(_atomic, _value) \
        GF_ATOMIC_LOCK_XOR(_atomic, _value)

#define GF_ATOMIC_BASE_NAND(_atomic, _value) \
        GF_ATOMIC_LOCK_NAND(_atomic, _value)

#define GF_ATOMIC_BASE_FETCH_ADD(_atomic, _value) \
        GF_ATOMIC_LOCK_FETCH_ADD(_atomic, _value)

#define GF_ATOMIC_BASE_FETCH_SUB(_atomic, _value) \
        GF_ATOMIC_LOCK_FETCH_SUB(_atomic, _value)

#define GF_ATOMIC_BASE_FETCH_AND(_atomic, _value) \
        GF_ATOMIC_LOCK_FETCH_AND(_atomic, _value)

#define GF_ATOMIC_BASE_FETCH_OR(_atomic, _value) \
        GF_ATOMIC_LOCK_FETCH_OR(_atomic, _value)

#define GF_ATOMIC_BASE_FETCH_XOR(_atomic, _value) \
        GF_ATOMIC_LOCK_FETCH_XOR(_atomic, _value)

#define GF_ATOMIC_BASE_FETCH_NAND(_atomic, _value) \
        GF_ATOMIC_LOCK_FETCH_NAND(_atomic, _value)

#define GF_ATOMIC_BASE_SWAP(_atomic, _value) \
        GF_ATOMIC_LOCK_SWAP(_atomic, _value)

#define GF_ATOMIC_BASE_CMP_SWAP(_atomic, _expected, _value) \
        GF_ATOMIC_LOCK_CMP_SWAP(_atomic, _expected, _value)

#endif /* HAVE_(ATOMIC|SYNC)_BUILTINS */

/* Here we declare the real atomic macros available to the user. */

/* All macros have a 'gf_atomic_xxx' as 1st argument */

#define GF_ATOMIC_INIT(_atomic, _value) GF_ATOMIC_CHOOSE(_atomic, INIT, _value)
#define GF_ATOMIC_GET(_atomic)          GF_ATOMIC_CHOOSE(_atomic, GET)
#define GF_ATOMIC_ADD(_atomic, _value)  GF_ATOMIC_CHOOSE(_atomic, ADD, _value)
#define GF_ATOMIC_SUB(_atomic, _value)  GF_ATOMIC_CHOOSE(_atomic, SUB, _value)
#define GF_ATOMIC_AND(_atomic, _value)  GF_ATOMIC_CHOOSE(_atomic, AND, _value)
#define GF_ATOMIC_OR(_atomic, _value)   GF_ATOMIC_CHOOSE(_atomic, OR, _value)
#define GF_ATOMIC_XOR(_atomic, _value)  GF_ATOMIC_CHOOSE(_atomic, XOR, _value)
#define GF_ATOMIC_NAND(_atomic, _value) GF_ATOMIC_CHOOSE(_atomic, NAND, _value)

#define GF_ATOMIC_FETCH_ADD(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, FETCH_ADD, _value)

#define GF_ATOMIC_FETCH_SUB(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, FETCH_SUB, _value)

#define GF_ATOMIC_FETCH_AND(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, FETCH_AND, _value)

#define GF_ATOMIC_FETCH_OR(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, FETCH_OR, _value)

#define GF_ATOMIC_FETCH_XOR(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, FETCH_XOR, _value)

#define GF_ATOMIC_FETCH_NAND(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, FETCH_NAND, _value)

#define GF_ATOMIC_SWAP(_atomic, _value) \
        GF_ATOMIC_CHOOSE(_atomic, SWAP, _value)

#define GF_ATOMIC_CMP_SWAP(_atomic, _expected, _value) \
        GF_ATOMIC_CHOOSE(_atomic, CMP_SWAP, _expected, _value)

#define GF_ATOMIC_INC(_atomic)       GF_ATOMIC_ADD(_atomic, 1)
#define GF_ATOMIC_DEC(_atomic)       GF_ATOMIC_SUB(_atomic, 1)
#define GF_ATOMIC_FETCH_INC(_atomic) GF_ATOMIC_FETCH_ADD(_atomic, 1)
#define GF_ATOMIC_FETCH_DEC(_atomic) GF_ATOMIC_FETCH_SUB(_atomic, 1)

#endif /* _ATOMIC_H */
