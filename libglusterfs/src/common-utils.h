/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <sys/uio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif

void trap (void);

#define GF_UNIVERSAL_ANSWER 42    /* :O */

/* To solve type punned error */
#define VOID(ptr) ((void **) ((void *) ptr))

#include "logging.h"
#include "glusterfs.h"
#include "locking.h"
#include "mem-pool.h"
#include "uuid.h"



#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define roof(a,b) ((((a)+(b)-1)/((b)?(b):1))*(b))
#define floor(a,b) (((a)/((b)?(b):1))*(b))


#define GF_UNIT_KB    1024ULL
#define GF_UNIT_MB    1048576ULL
#define GF_UNIT_GB    1073741824ULL
#define GF_UNIT_TB    1099511627776ULL
#define GF_UNIT_PB    1125899906842624ULL

#define GF_UNIT_KB_STRING    "KB"
#define GF_UNIT_MB_STRING    "MB"
#define GF_UNIT_GB_STRING    "GB"
#define GF_UNIT_TB_STRING    "TB"
#define GF_UNIT_PB_STRING    "PB"

#define GF_UNIT_PERCENT_STRING      "%"

#define GEOREP "geo-replication"
#define GHADOOP "glusterfs-hadoop"

#define WIPE(statp) do { typeof(*statp) z = {0,}; if (statp) *statp = z; } while (0)

#define IS_EXT_FS(fs_name)          \
        (!strcmp (fs_name, "ext2") || \
         !strcmp (fs_name, "ext3") || \
         !strcmp (fs_name, "ext4"))

/* Defining this here as it is needed by glusterd for setting
 * nfs port in volume status.
 */
#define GF_NFS3_PORT    38467

enum _gf_boolean
{
	_gf_false = 0,
	_gf_true = 1
};

/*
 * we could have initialized these as +ve values and treated
 * them as negative while comparing etc.. (which would have
 * saved us with the pain of assigning values), but since we
 * only have a couple of clients that use this feature, it's
 * okay.
 */
enum _gf_client_pid
{
        GF_CLIENT_PID_MAX    =  0,
        GF_CLIENT_PID_GSYNCD = -1,
        GF_CLIENT_PID_HADOOP = -2,
        GF_CLIENT_PID_DEFRAG = -3,
};

typedef enum _gf_boolean gf_boolean_t;
typedef enum _gf_client_pid gf_client_pid_t;
typedef int (*gf_cmp) (void *, void *);

void gf_global_variable_init(void);

in_addr_t gf_resolve_ip (const char *hostname, void **dnscache);

void gf_log_volume_file (FILE *specfp);
void gf_print_trace (int32_t signal);

extern char *gf_fop_list[GF_FOP_MAXVALUE];
extern char *gf_mgmt_list[GF_MGMT_MAXVALUE];

#define VECTORSIZE(count) (count * (sizeof (struct iovec)))

#define STRLEN_0(str) (strlen(str) + 1)
#define VALIDATE_OR_GOTO(arg,label)   do {				\
		if (!arg) {						\
			errno = EINVAL;					\
			gf_log_callingfn ((this ? (this->name) :        \
                                           "(Govinda! Govinda!)"),      \
                                          GF_LOG_WARNING,               \
                                          "invalid argument: " #arg);   \
			goto label;					\
		}							\
	} while (0)

#define GF_VALIDATE_OR_GOTO(name,arg,label)   do {                      \
		if (!arg) {                                             \
			errno = EINVAL;                                 \
			gf_log_callingfn (name, GF_LOG_ERROR,           \
                                          "invalid argument: " #arg);	\
			goto label;                                     \
		}                                                       \
	} while (0)

#define GF_VALIDATE_OR_GOTO_WITH_ERROR(name, arg, label, errno, error) do { \
                if (!arg) {                                                 \
                        errno = error;                                  \
                        gf_log_callingfn (name, GF_LOG_ERROR,           \
                                          "invalid argument: " #arg);   \
                        goto label;                                     \
                }                                                       \
        }while (0)

#define GF_ASSERT_AND_GOTO_WITH_ERROR(name, arg, label, errno, error) do { \
                if (!arg) {                                             \
                        GF_ASSERT (0);                                  \
                        errno = error;                                  \
                        goto label;                                     \
                }                                                       \
        }while (0)

#define GF_VALIDATE_ABSOLUTE_PATH_OR_GOTO(name,arg,label)               \
        do {                                                            \
                GF_VALIDATE_OR_GOTO (name, arg, label);                 \
                if ((arg[0]) != '/') {                                  \
                        errno = EINVAL;                                 \
			gf_log_callingfn (name, GF_LOG_ERROR,           \
                                          "invalid argument: " #arg);	\
                        goto label;                                     \
                }                                                       \
	} while (0)

#define GF_REMOVE_SLASH_FROM_PATH(path, string)                         \
        do {                                                            \
                int i = 0;                                              \
                for (i = 1; i < strlen (path); i++) {                   \
                        string[i-1] = path[i];                          \
                        if (string[i-1] == '/')                         \
                                string[i-1] = '-';                      \
                }                                                       \
        } while (0)


#define GF_IF_INTERNAL_XATTR_GOTO(pattern, dict, trav, op_errno, label) \
        do {                                                            \
                if (!dict) {                                            \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "setxattr dict is null");               \
                        goto label;                                     \
                }                                                       \
                trav = dict->members_list;                              \
                while (trav) {                                          \
                        if (!fnmatch (pattern, trav->key, 0)) {         \
                                op_errno = EPERM;                       \
                                gf_log (this->name, GF_LOG_ERROR,       \
                                        "attempt to set internal"       \
                                        " xattr: %s: %s", trav->key,    \
                                        strerror (op_errno));           \
                                goto label;                             \
                        }                                               \
                        trav = trav->next;                              \
                }                                                       \
        } while (0)

#define GF_IF_NATIVE_XATTR_GOTO(pattern, key, op_errno, label)          \
        do {                                                            \
                if (!key) {                                             \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "no key for removexattr");              \
                        goto label;                                     \
                }                                                       \
                if (!fnmatch (pattern, key, 0)) {                       \
                        op_errno = EPERM;                               \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "attempt to remove internal "           \
                                "xattr: %s: %s", key,                   \
                                strerror (op_errno));                   \
                        goto label;                                     \
                }                                                       \
        } while (0)


#define GF_FILE_CONTENT_REQUESTED(_xattr_req,_content_limit) \
	(dict_get_uint64 (_xattr_req, "glusterfs.content", _content_limit) == 0)

#ifdef DEBUG
#define GF_ASSERT(x) assert (x);
#else
#define GF_ASSERT(x)                                                    \
        do {                                                            \
                if (!(x)) {                                             \
                        gf_log_callingfn ("", GF_LOG_ERROR,             \
                                          "Assertion failed: " #x);     \
                }                                                       \
        } while (0)
#endif

#define GF_UUID_ASSERT(u) \
        if (uuid_is_null (u))\
                GF_ASSERT (!"uuid null");

union gf_sock_union {
        struct sockaddr_storage storage;
        struct sockaddr_in6 sin6;
        struct sockaddr_in sin;
        struct sockaddr sa;
};

#define GF_HIDDEN_PATH ".glusterfs"

static inline void
iov_free (struct iovec *vector, int count)
{
	int i;

	for (i = 0; i < count; i++)
		FREE (vector[i].iov_base);

	GF_FREE (vector);
}


static inline int
iov_length (const struct iovec *vector, int count)
{
	int     i = 0;
	size_t  size = 0;

	for (i = 0; i < count; i++)
		size += vector[i].iov_len;

	return size;
}


static inline struct iovec *
iov_dup (struct iovec *vector, int count)
{
	int           bytecount = 0;
	int           i;
	struct iovec *newvec = NULL;

	bytecount = (count * sizeof (struct iovec));
	newvec = GF_MALLOC (bytecount, gf_common_mt_iovec);
	if (!newvec)
		return NULL;

	for (i = 0; i < count; i++) {
		newvec[i].iov_len  = vector[i].iov_len;
		newvec[i].iov_base = vector[i].iov_base;
	}

	return newvec;
}


static inline int
iov_subset (struct iovec *orig, int orig_count,
	    off_t src_offset, off_t dst_offset,
	    struct iovec *new)
{
	int    new_count = 0;
	int    i;
	off_t  offset = 0;
	size_t start_offset = 0;
	size_t end_offset = 0;


	for (i = 0; i < orig_count; i++) {
		if ((offset + orig[i].iov_len < src_offset)
		    || (offset > dst_offset)) {
			goto not_subset;
		}

		if (!new) {
			goto count_only;
		}

		start_offset = 0;
		end_offset = orig[i].iov_len;

		if (src_offset >= offset) {
			start_offset = (src_offset - offset);
		}

		if (dst_offset <= (offset + orig[i].iov_len)) {
			end_offset = (dst_offset - offset);
		}

		new[new_count].iov_base = orig[i].iov_base + start_offset;
		new[new_count].iov_len = end_offset - start_offset;

	count_only:
		new_count++;

	not_subset:
		offset += orig[i].iov_len;
	}

	return new_count;
}


static inline void
iov_unload (char *buf, const struct iovec *vector, int count)
{
	int i;
	int copied = 0;

	for (i = 0; i < count; i++) {
		memcpy (buf + copied, vector[i].iov_base, vector[i].iov_len);
		copied += vector[i].iov_len;
	}
}


static inline int
mem_0filled (const char *buf, size_t size)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < size; i++) {
		ret = buf[i];
		if (ret)
			break;
	}

	return ret;
}


static inline int
iov_0filled (struct iovec *vector, int count)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < count; i++) {
		ret = mem_0filled (vector[i].iov_base, vector[i].iov_len);
		if (ret)
			break;
	}

	return ret;
}


static inline void *
memdup (const void *ptr, size_t size)
{
	void *newptr = NULL;

	newptr = GF_MALLOC (size, gf_common_mt_memdup);
	if (!newptr)
		return NULL;

	memcpy (newptr, ptr, size);
	return newptr;
}

int
mkdir_p (char *path, mode_t mode, gf_boolean_t allow_symlinks, int *start);
/*
 * rounds up nr to power of two. If nr is already a power of two, just returns
 * nr
 */

int32_t gf_roundup_power_of_two (uint32_t nr);

/*
 * rounds up nr to next power of two. If nr is already a power of two, next
 * power of two is returned.
 */

int32_t gf_roundup_next_power_of_two (uint32_t nr);

char *gf_trim (char *string);
int gf_strsplit (const char *str, const char *delim,
		 char ***tokens, int *token_count);
int gf_volume_name_validate (const char *volume_name);

int gf_string2long (const char *str, long *n);
int gf_string2ulong (const char *str, unsigned long *n);
int gf_string2int (const char *str, int *n);
int gf_string2uint (const char *str, unsigned int *n);
int gf_string2double (const char *str, double *n);
int gf_string2longlong (const char *str, long long *n);
int gf_string2ulonglong (const char *str, unsigned long long *n);

int gf_string2int8 (const char *str, int8_t *n);
int gf_string2int16 (const char *str, int16_t *n);
int gf_string2int32 (const char *str, int32_t *n);
int gf_string2int64 (const char *str, int64_t *n);
int gf_string2uint8 (const char *str, uint8_t *n);
int gf_string2uint16 (const char *str, uint16_t *n);
int gf_string2uint32 (const char *str, uint32_t *n);
int gf_string2uint64 (const char *str, uint64_t *n);

int gf_strstr (const char *str, const char *delim, const char *match);

int gf_string2ulong_base10 (const char *str, unsigned long *n);
int gf_string2uint_base10 (const char *str, unsigned int *n);
int gf_string2uint8_base10 (const char *str, uint8_t *n);
int gf_string2uint16_base10 (const char *str, uint16_t *n);
int gf_string2uint32_base10 (const char *str, uint32_t *n);
int gf_string2uint64_base10 (const char *str, uint64_t *n);

int gf_string2bytesize (const char *str, uint64_t *n);
int gf_string2percent_or_bytesize (const char *str, uint64_t *n,
				   gf_boolean_t *is_percent);

int gf_string2boolean (const char *str, gf_boolean_t *b);
int gf_string2percent (const char *str, uint32_t *n);
int gf_string2time (const char *str, uint32_t *n);

int gf_lockfd (int fd);
int gf_unlockfd (int fd);

int get_checksum_for_file (int fd, uint32_t *checksum);
int log_base2 (unsigned long x);

int get_checksum_for_path (char *path, uint32_t *checksum);

char *strtail (char *str, const char *pattern);
void skipwhite (char **s);
char *nwstrtail (char *str, char *pattern);
void skip_word (char **str);
/* returns a new string with nth word of given string. n>=1 */
char *get_nth_word (const char *str, int n);

char valid_host_name (char *address, int length);
char valid_ipv4_address (char *address, int length, gf_boolean_t wildcard_acc);
char valid_ipv6_address (char *address, int length, gf_boolean_t wildcard_acc);
char valid_internet_address (char *address, gf_boolean_t wildcard_acc);
char valid_ipv4_wildcard_check (char *address);
char valid_ipv6_wildcard_check (char *address);
char valid_wildcard_internet_address (char *address);

char *uuid_utoa (uuid_t uuid);
char *uuid_utoa_r (uuid_t uuid, char *dst);
char *lkowner_utoa (gf_lkowner_t *lkowner);
char *lkowner_utoa_r (gf_lkowner_t *lkowner, char *dst, int len);

void gf_array_insertionsort (void *a, int l, int r, size_t elem_size,
                             gf_cmp cmp);
int gf_is_str_int (const char *value);

char *gf_uint64_2human_readable (uint64_t);
int validate_brick_name (char *brick);
char *get_host_name (char *word, char **host);
char *get_path_name (char *word, char **path);
void gf_path_strip_trailing_slashes (char *path);
uint64_t get_mem_size ();
int gf_strip_whitespace (char *str, int len);
#endif /* _COMMON_UTILS_H */
