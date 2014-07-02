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
#include <openssl/md5.h>
#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif
#include <limits.h>

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

#define IPv4_ADDR_SIZE 32


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

#define GF_SELINUX_XATTR_KEY "security.selinux"

#define WIPE(statp) do { typeof(*statp) z = {0,}; if (statp) *statp = z; } while (0)

#define IS_EXT_FS(fs_name)          \
        (!strcmp (fs_name, "ext2") || \
         !strcmp (fs_name, "ext3") || \
         !strcmp (fs_name, "ext4"))

/* Defining this here as it is needed by glusterd for setting
 * nfs port in volume status.
 */
#define GF_NFS3_PORT    2049
#define GF_CLIENT_PORT_CEILING 1024

#define GF_MINUTE_IN_SECONDS 60
#define GF_HOUR_IN_SECONDS (60*60)
#define GF_DAY_IN_SECONDS (24*60*60)
#define GF_WEEK_IN_SECONDS (7*24*60*60)

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

void gf_log_dump_graph (FILE *specfp, glusterfs_graph_t *graph);
void gf_print_trace (int32_t signal, glusterfs_ctx_t *ctx);
int  gf_set_log_file_path (cmd_args_t *cmd_args);

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

#define GF_REMOVE_INTERNAL_XATTR(pattern, dict)                         \
        do {                                                            \
                if (!dict) {                                            \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "dict is null");                        \
                        break;                                          \
                }                                                       \
                dict_foreach_fnmatch (dict, pattern,                    \
                                      dict_remove_foreach_fn,           \
                                      NULL);                            \
        } while (0)

#define GF_IF_INTERNAL_XATTR_GOTO(pattern, dict, op_errno, label)       \
        do {                                                            \
                if (!dict) {                                            \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "setxattr dict is null");               \
                        goto label;                                     \
                }                                                       \
                if (dict_foreach_fnmatch (dict, pattern,                \
                                          dict_null_foreach_fn,         \
                                          NULL) > 0) {                  \
                        op_errno = EPERM;                               \
                        gf_log (this->name, GF_LOG_ERROR,               \
                                "attempt to set internal"               \
                                " xattr: %s: %s", pattern,              \
                                strerror (op_errno));                   \
                        goto label;                                     \
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

#define IOV_MIN(n) min(IOV_MAX,n)

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
iov_dup (const struct iovec *vector, int count)
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


static inline size_t
iov_load (const struct iovec *vector, int count, char *buf, int size)
{
	size_t left = size;
	size_t cp = 0;
	int    ret = 0;
	int    i = 0;

	while (left && i < count) {
		cp = min (vector[i].iov_len, left);
		if (vector[i].iov_base != buf + (size - left))
			memcpy (vector[i].iov_base, buf + (size - left), cp);
		ret += cp;
		left -= cp;
		if (left)
			i++;
	}

	return ret;
}


static inline size_t
iov_copy (const struct iovec *dst, int dcnt,
	  const struct iovec *src, int scnt)
{
	size_t  ret = 0;
	size_t  left = 0;
	size_t  min_i = 0;
	int     s_i = 0, s_ii = 0;
	int     d_i = 0, d_ii = 0;

	ret = min (iov_length (dst, dcnt), iov_length (src, scnt));
	left = ret;

	while (left) {
		min_i = min (dst[d_i].iov_len - d_ii, src[s_i].iov_len - s_ii);
		memcpy (dst[d_i].iov_base + d_ii, src[s_i].iov_base + s_ii,
			min_i);

		d_ii += min_i;
		if (d_ii == dst[d_i].iov_len) {
			d_ii = 0;
			d_i++;
		}

		s_ii += min_i;
		if (s_ii == src[s_i].iov_len) {
			s_ii = 0;
			s_i++;
		}

		left -= min_i;
	}

	return ret;
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

typedef enum {
        gf_timefmt_default = 0,
        gf_timefmt_FT = 0,  /* YYYY-MM-DD hh:mm:ss */
        gf_timefmt_Ymd_T,   /* YYYY/MM-DD-hh:mm:ss */
        gf_timefmt_bdT,     /* ddd DD hh:mm:ss */
        gf_timefmt_F_HMS,   /* YYYY-MM-DD hhmmss */
        gf_timefmt_last
} gf_timefmts;

static inline void
gf_time_fmt (char *dst, size_t sz_dst, time_t utime, unsigned int fmt)
{
        extern void _gf_timestuff (gf_timefmts *, const char ***, const char ***);
        static gf_timefmts timefmt_last = (gf_timefmts) -1;
        static const char **fmts;
        static const char **zeros;
        struct tm tm;

        if (timefmt_last == -1)
                _gf_timestuff (&timefmt_last, &fmts, &zeros);
        if (timefmt_last < fmt) fmt = gf_timefmt_default;
        if (utime && gmtime_r (&utime, &tm) != NULL) {
                strftime (dst, sz_dst, fmts[fmt], &tm);
        } else {
                strncpy (dst, "N/A", sz_dst);
        }
}

int
mkdir_p (char *path, mode_t mode, gf_boolean_t allow_symlinks);
/*
 * rounds up nr to power of two. If nr is already a power of two, just returns
 * nr
 */

int
gf_lstat_dir (const char *path, struct stat *stbuf_in);

int32_t gf_roundup_power_of_two (int32_t nr);

/*
 * rounds up nr to next power of two. If nr is already a power of two, next
 * power of two is returned.
 */

int32_t gf_roundup_next_power_of_two (int32_t nr);

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
int gf_string2percent (const char *str, double *n);
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

gf_boolean_t mask_match (const uint32_t a, const uint32_t b, const uint32_t m);
char valid_host_name (char *address, int length);
char valid_ipv4_address (char *address, int length, gf_boolean_t wildcard_acc);
char valid_ipv6_address (char *address, int length, gf_boolean_t wildcard_acc);
char valid_internet_address (char *address, gf_boolean_t wildcard_acc);
gf_boolean_t valid_mount_auth_address (char *address);
gf_boolean_t valid_ipv4_subnetwork (const char *address);
gf_boolean_t gf_sock_union_equal_addr (union gf_sock_union *a,
                                       union gf_sock_union *b);

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
int gf_canonicalize_path (char *path);
char *generate_glusterfs_ctx_id (void);
char *gf_get_reserved_ports();
int gf_process_reserved_ports (gf_boolean_t ports[]);
gf_boolean_t gf_ports_reserved (char *blocked_port, gf_boolean_t *ports);
int gf_get_hostname_from_ip (char *client_ip, char **hostname);
gf_boolean_t gf_is_local_addr (char *hostname);
gf_boolean_t gf_is_same_address (char *host1, char *host2);
void md5_wrapper(const unsigned char *data, size_t len, char *md5);

int gf_thread_create (pthread_t *thread, const pthread_attr_t *attr,
		      void *(*start_routine)(void *), void *arg);
#ifdef __NetBSD__
size_t backtrace(void **, size_t);
char **backtrace_symbols(void *const *, size_t);
#endif

gf_boolean_t
gf_is_service_running (char *pidfile, int *pid);
int
gf_skip_header_section (int fd, int header_len);

struct iatt;
struct _dict;

inline gf_boolean_t
dht_is_linkfile (struct iatt *buf, struct _dict *dict);

#endif /* _COMMON_UTILS_H */
