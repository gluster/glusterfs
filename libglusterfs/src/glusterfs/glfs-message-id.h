/*
  Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_MESSAGE_ID_H_
#define _GLFS_MESSAGE_ID_H_

#include <inttypes.h>
#include <string.h>

#include <glusterfs/logging.h>

/* Base of all message IDs, all message IDs would be
 * greater than this */
#define GLFS_MSGID_BASE 100000

/* Segment size of allocated range. Any component needing more than this
 * segment size should take multiple segments (at times non contiguous,
 * if extensions are being made post the next segment already allocated) */
#define GLFS_MSGID_SEGMENT 1000

/* Macro to define a range of messages for a component. The first argument is
 * the name of the component. The second argument is the number of segments
 * to allocate. The defined values will be GLFS_MSGID_COMP_<name> and
 * GLFS_MSGID_COMP_<name>_END. */
#define GLFS_MSGID_COMP(_name, _blocks)                                        \
    GLFS_MSGID_COMP_##_name,                                                   \
        GLFS_MSGID_COMP_##_name##_END = (GLFS_MSGID_COMP_##_name +             \
                                         (GLFS_MSGID_SEGMENT * (_blocks)) - 1)

/* Old way to allocate a list of message id's. */
#define GLFS_MSGID(_name, _msgs...)                                            \
    enum _msgid_table_##_name                                                  \
    {                                                                          \
        GLFS_##_name##_COMP_BASE = GLFS_MSGID_COMP_##_name, ##_msgs,           \
        GLGS_##_name##_COMP_END                                                \
    }

/* New way to allocate messages with full description.
 *
 * This new approach has several advantages:
 *
 *   - Centralized message definition
 *   - Customizable list of additional data per message
 *   - Typed message arguments to enforce correctness
 *   - Consistency between different instances of the same message
 *   - Better uniformity in data type representation
 *   - Compile-time generation of messages
 *   - Easily modify the message and update all references
 *   - All argument preparation is done only if the message will be logged
 *   - Very easy to use
 *   - Code auto-completion friendly
 *   - All extra overhead is optimally optimized by gcc/clang
 *
 * The main drawback is that it makes heavy use of some macros, which is
 * considered a bad practice sometimes, and it uses specific gcc/clang
 * extensions.
 *
 * To create a new message:
 *
 *    GLFS_NEW(_comp, _name, _msg, _num, _fields...)
 *
 * To deprecate an existing message:
 *
 *    GLFS_OLD(_comp, _name, _msg, _num, _fields...)
 *
 * To permanently remove a message (but keep its ID reserved):
 *
 *    GLFS_GONE(_comp, _name, _msg, _num, _fields...)
 *
 * Each field is a list composed of the following elements:
 *
 *    (_type, _name, _format, (_values) [, _extra])
 *
 *    _type   is the data type of the field (int32_t, const char *, ...)
 *    _name   is the name of the field (it will also appear in the log string)
 *    _format is the C format string to represent the field
 *    _values is a list of values used by _format (generally it's only _name)
 *    _extra  is an optional extra code to prepare the representation of the
 *            field (check GLFS_UUID() for an example)
 *
 * There are some predefined macros for common data types.
 *
 * Example:
 *
 * Message definition:
 *
 *    GLFS_NEW(LIBGLUSTERFS, LG_MSG_EXAMPLE, "Example message", 3,
 *        GLFS_UINT(number),
 *        GLFS_ERR(error),
 *        GLFS_STR(name)
 *    )
 *
 * Message invocation:
 *
 *    GF_LOG_I("test", LG_MSG_EXAMPLE(3, -2, "test"));
 *
 * This will generate a message like this:
 *
 *    "Example message ({number=3}, {error=2 (File not found)}, {name='test'})"
 */

/* Start the definition of messages for a component. Only one component can be
 * defined at a time. Calling this macro for another component before finishing
 * the definition of messages for the previous component will mess the message
 * ids. */
#define GLFS_COMPONENT(_name)                                                  \
    enum {                                                                     \
        GLFS_##_name##_COMP_BASE = GLFS_MSGID_COMP_##_name - __COUNTER__ - 1   \
    }

/* Unpack arguments. */
#define GLFS_EXPAND(_x...) _x

/* These macros apply a macro to a list of 0..9 arguments. It can be extended
 * if more arguments are required. */

#define GLFS_APPLY_0(_macro, _in)

#define GLFS_APPLY_1(_macro, _in, _f) _macro _f

#define GLFS_APPLY_2(_macro, _in, _f, _more)                                   \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_1(_macro, _in, _more)

#define GLFS_APPLY_3(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_2(_macro, _in, ##_more)

#define GLFS_APPLY_4(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_3(_macro, _in, ##_more)

#define GLFS_APPLY_5(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_4(_macro, _in, ##_more)

#define GLFS_APPLY_6(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_5(_macro, _in, ##_more)

#define GLFS_APPLY_7(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_6(_macro, _in, ##_more)

#define GLFS_APPLY_8(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_7(_macro, _in, ##_more)

#define GLFS_APPLY_9(_macro, _in, _f, _more...)                                \
    _macro _f GLFS_EXPAND _in GLFS_APPLY_8(_macro, _in, ##_more)

/* Apply a macro to a variable number of fields. */
#define GLFS_APPLY(_macro, _in, _num, _fields...)                              \
    GLFS_APPLY_##_num(_macro, _in, ##_fields)

/* Translate a name into an internal name to avoid variable name collisions. */
#define GLFS_GET(_name) _glfs_var_##_name

/* Build a declaration for a structure from a field. */
#define GLFS_FIELD(_type, _name, _extra...) _type GLFS_GET(_name);

/* Extract the name from a field. */
#define GLFS_NAME(_type, _name, _extra...) , GLFS_GET(_name)

/* Build the format string from a field. */
#define GLFS_FMT(_type, _name, _src, _fmt, _extra...) "{" #_name "=" _fmt "}"

/* Build a function call argument from a field. */
#define GLFS_ARG(_type, _name, _extra...) _type GLFS_GET(_name)

/* Initialize a variable from a field in a structure. */
#define GLFS_VAR(_type, _name, _src, _fmt, _data, _extra...)                   \
    _type GLFS_GET(_name) = _info->GLFS_GET(_name);                            \
    _extra

/* Copy a variable from source. */
#define GLFS_COPY(_type, _name, _src, _fmt, _data, _extra...)                  \
    _type GLFS_GET(_name) = _src;                                              \
    _extra

/* Extract the data from a field. */
#define GLFS_DATA(_type, _name, _src, _fmt, _data, _extra...)                  \
    , GLFS_EXPAND _data

/* Generate the fields of the structre. */
#define GLFS_FIELDS(_num, _fields...)                                          \
    GLFS_APPLY(GLFS_FIELD, (), _num, ##_fields)

/* Generate a list of the names of all fields. */
#define GLFS_NAMES(_num, _fields...) GLFS_APPLY(GLFS_NAME, (), _num, ##_fields)

/* Generate a format string for all fields. */
#define GLFS_FMTS(_num, _fields...)                                            \
    GLFS_APPLY(GLFS_FMT, (", "), _num, ##_fields)

/* Generate the function call argument declaration for all fields. */
#define GLFS_ARGS(_num, _fields...) GLFS_APPLY(GLFS_ARG, (, ), _num, ##_fields)

/* Generate the list of variables for all fields from a structure. */
#define GLFS_VARS(_num, _fields...) GLFS_APPLY(GLFS_VAR, (), _num, ##_fields)

/* Generate the list of values for all fields. */
#define GLFS_DATAS(_num, _fields...) GLFS_APPLY(GLFS_DATA, (), _num, ##_fields)

/* Generate the list of variables for all fields from sources. */
#define GLFS_COPIES(_num, _fields...) GLFS_APPLY(GLFS_COPY, (), _num, ##_fields)

/* Create the structure for a message. */
#define GLFS_STRUCT(_name, _num, _fields...)                                   \
    struct _glfs_##_name {                                                     \
        void (*_process)(const char *, const char *, const char *, uint32_t,   \
                         uint32_t, struct _glfs_##_name *);                    \
        GLFS_FIELDS(_num, ##_fields)                                           \
    }

/* Create the processing function for a message. */
#define GLFS_PROCESS(_mod, _name, _msg, _num, _fields...)                      \
    enum { _glfs_##_name = GLFS_##_mod##_COMP_BASE + __COUNTER__ };            \
    _Static_assert((int)_glfs_##_name <= (int)GLFS_MSGID_COMP_##_mod##_END,    \
                   "Too many messages allocated for component " #_mod);        \
    static inline                                                              \
        __attribute__((__always_inline__)) void _glfs_process_##_name(         \
            const char *_dom, const char *_file, const char *_func,            \
            uint32_t _line, uint32_t _level, struct _glfs_##_name *_info)      \
    {                                                                          \
        GLFS_VARS(_num, ##_fields)                                             \
        _gf_log(_dom, _file, _func, _line, _level,                             \
                "[MSGID:%u] " _msg " <" GLFS_FMTS(_num, ##_fields) ">",        \
                _glfs_##_name GLFS_DATAS(_num, ##_fields));                    \
    }

/* Create the data capture function for a message. */
#define GLFS_CREATE(_name, _attr, _num, _fields...)                            \
    static inline __attribute__((__always_inline__, __warn_unused_result__))   \
    _attr struct _glfs_##_name                                                 \
    _name(GLFS_ARGS(_num, ##_fields))                                          \
    {                                                                          \
        return (struct _glfs_##_name){                                         \
            _glfs_process_##_name GLFS_NAMES(_num, ##_fields)};                \
    }

#define GLFS_DEBUG(_dom, _msg, _num, _fields...)                               \
    do {                                                                       \
        GLFS_COPIES(_num, ##_fields)                                           \
        _gf_log(_dom, __FILE__, __FUNCTION__, __LINE__, GF_LOG_DEBUG,          \
                "[MSGID:DBG] " _msg " <" GLFS_FMTS(                            \
                    _num, ##_fields) ">" GLFS_DATAS(_num, ##_fields));         \
    } while (0)

#define GLFS_TRACE(_dom, _msg, _num, _fields...)                               \
    do {                                                                       \
        GLFS_COPIES(_num, ##_fields)                                           \
        _gf_log(_dom, __FILE__, __FUNCTION__, __LINE__, GF_LOG_TRACE,          \
                "[MSGID:TRC] " _msg " <" GLFS_FMTS(                            \
                    _num, ##_fields) ">" GLFS_NAMES(_num, ##_fields));         \
    } while (0)

/* Create a new message. */
#define GLFS_NEW(_mod, _name, _msg, _num, _fields...)                          \
    GLFS_STRUCT(_name, _num, ##_fields);                                       \
    GLFS_PROCESS(_mod, _name, _msg, _num, ##_fields)                           \
    GLFS_CREATE(_name, , _num, ##_fields)

/* Create a deprecated message. */
#define GLFS_OLD(_mod, _name, _msg, _num, _fields...)                          \
    GLFS_STRUCT(_name, _num, ##_fields);                                       \
    GLFS_PROCESS(_mod, _name, _msg, _num, ##_fields)                           \
    GLFS_CREATE(_name, __attribute__((__deprecated__)), _num, ##_fields)

/* Create a deleted message. */
#define GLFS_GONE(_mod, _name, _msg, _num, _fields...)                         \
    enum { _name##_ID_GONE = GLFS_##_mod##_COMP_BASE + __COUNTER__ };

/* Create a new message compatible with the old interface. */
#define GLFS_MIG(_mod, _name, _msg, _num, _fields...)                          \
    enum { _name = GLFS_##_mod##_COMP_BASE + __COUNTER__ };

/* Map a field name to its source. If source is not present, use the name */
#define GLFS_MAP1(_dummy, _src, _data...) _src
#define GLFS_MAP(_name, _src...) GLFS_MAP1(, ##_src, _name)

/* Helper to create an unsigned integer field. */
#define GLFS_UINT(_name, _src...)                                              \
    (uint32_t, _name, GLFS_MAP(_name, ##_src), "%" PRIu32, (GLFS_GET(_name)))

/* Helper to create a signed integer field. */
#define GLFS_INT(_name, _src...)                                               \
    (int32_t, _name, GLFS_MAP(_name, ##_src), "%" PRId32, (GLFS_GET(_name)))

/* Helper to create an error field. */
#define GLFS_ERR(_name, _src...)                                               \
    (int32_t, _name, GLFS_MAP(_name, ##_src), "%" PRId32 " (%s)",              \
     (GLFS_GET(_name), strerror(GLFS_GET(_name))))

/* Helper to create an error field where the error code is encoded in a
 * negative number. */
#define GLFS_RES(_name, _src...)                                               \
    (int32_t, _name, GLFS_MAP(_name, ##_src), "%" PRId32 " (%s)",              \
     (-GLFS_GET(_name), strerror(-GLFS_GET(_name))))

/* Helper to create a string field. */
#define GLFS_STR(_name, _src...)                                               \
    (const char *, _name, GLFS_MAP(_name, ##_src), "'%s'", (GLFS_GET(_name)))

/* Helper to create a gfid field. */
#define GLFS_UUID(_name, _src...)                                              \
    (const uuid_t *, _name, GLFS_MAP(_name, ##_src), "%s",                     \
     (*GLFS_GET(_name##_buff)), char GLFS_GET(_name##_buff)[48];               \
     uuid_unparse(*GLFS_GET(_name), GLFS_GET(_name##_buff)))

/* Helper to create a pointer field. */
#define GLFS_PTR(_name, _src...)                                               \
    (void *, _name, GLFS_MAP(_name, ##_src), "%p", (GLFS_GET(_name)))

/* Per module message segments allocated */
/* NOTE: For any new module add to the end the modules */
enum _msgid_comp {
    GLFS_MSGID_RESERVED = GLFS_MSGID_BASE - 1,

    GLFS_MSGID_COMP(GLUSTERFSD, 1),
    GLFS_MSGID_COMP(LIBGLUSTERFS, 1),
    GLFS_MSGID_COMP(RPC_LIB, 1),
    GLFS_MSGID_COMP(RPC_TRANS_RDMA, 1),
    GLFS_MSGID_COMP(API, 1),
    GLFS_MSGID_COMP(CLI, 1),
    /* glusterd has a lot of messages, taking 2 segments for the same */
    GLFS_MSGID_COMP(GLUSTERD, 2),
    GLFS_MSGID_COMP(AFR, 1),
    GLFS_MSGID_COMP(DHT, 1),
    /* there is no component called 'common', however reserving this segment
     * for common actions/errors like dict_{get/set}, memory accounting*/
    GLFS_MSGID_COMP(COMMON, 1),
    GLFS_MSGID_COMP(UPCALL, 1),
    GLFS_MSGID_COMP(NFS, 1),
    GLFS_MSGID_COMP(POSIX, 1),
    GLFS_MSGID_COMP(PC, 1),
    GLFS_MSGID_COMP(PS, 1),
    GLFS_MSGID_COMP(BITROT_STUB, 1),
    GLFS_MSGID_COMP(CHANGELOG, 1),
    GLFS_MSGID_COMP(BITROT_BITD, 1),
    GLFS_MSGID_COMP(RPC_TRANS_SOCKET, 1),
    GLFS_MSGID_COMP(QUOTA, 1),
    GLFS_MSGID_COMP(CTR, 1),
    GLFS_MSGID_COMP(EC, 1),
    GLFS_MSGID_COMP(IO_CACHE, 1),
    GLFS_MSGID_COMP(IO_THREADS, 1),
    GLFS_MSGID_COMP(MD_CACHE, 1),
    GLFS_MSGID_COMP(OPEN_BEHIND, 1),
    GLFS_MSGID_COMP(QUICK_READ, 1),
    GLFS_MSGID_COMP(READ_AHEAD, 1),
    GLFS_MSGID_COMP(READDIR_AHEAD, 1),
    GLFS_MSGID_COMP(SYMLINK_CACHE, 1),
    GLFS_MSGID_COMP(WRITE_BEHIND, 1),
    GLFS_MSGID_COMP(CHANGELOG_LIB, 1),
    GLFS_MSGID_COMP(SHARD, 1),
    GLFS_MSGID_COMP(JBR, 1),
    GLFS_MSGID_COMP(PL, 1),
    GLFS_MSGID_COMP(DC, 1),
    GLFS_MSGID_COMP(LEASES, 1),
    GLFS_MSGID_COMP(INDEX, 1),
    GLFS_MSGID_COMP(POSIX_ACL, 1),
    GLFS_MSGID_COMP(NLC, 1),
    GLFS_MSGID_COMP(SL, 1),
    GLFS_MSGID_COMP(HAM, 1),
    GLFS_MSGID_COMP(SDFS, 1),
    GLFS_MSGID_COMP(QUIESCE, 1),
    GLFS_MSGID_COMP(TA, 1),
    GLFS_MSGID_COMP(SNAPVIEW_CLIENT, 1),
    GLFS_MSGID_COMP(TEMPLATE, 1),
    GLFS_MSGID_COMP(UTIME, 1),
    GLFS_MSGID_COMP(SNAPVIEW_SERVER, 1),
    GLFS_MSGID_COMP(CVLT, 1),
    /* --- new segments for messages goes above this line --- */

    GLFS_MSGID_END
};

#endif /* !_GLFS_MESSAGE_ID_H_ */
