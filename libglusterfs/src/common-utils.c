/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#else
#include "execinfo_compat.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <assert.h>
#include <libgen.h> /* for dirname() */

#if defined(GF_BSD_HOST_OS) || defined(GF_DARWIN_HOST_OS)
#include <sys/sysctl.h>
#endif
#include <libgen.h>

#include "compat-errno.h"
#include "logging.h"
#include "common-utils.h"
#include "revision.h"
#include "glusterfs.h"
#include "stack.h"
#include "globals.h"
#include "lkowner.h"
#include "syscall.h"
#include "cli1-xdr.h"
#include <ifaddrs.h>
#include "libglusterfs-messages.h"

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif /* AI_ADDRCONFIG */

char *vol_type_str[] = {"Distribute",
                        "Stripe",
                        "Replicate",
                        "Striped-Replicate",
                        "Disperse",
                        "Tier",
                        "Distributed-Stripe",
                        "Distributed-Replicate",
                        "Distributed-Striped-Replicate",
                        "Distributed-Disperse",
                       };

typedef int32_t (*rw_op_t)(int32_t fd, char *buf, int32_t size);
typedef int32_t (*rwv_op_t)(int32_t fd, const struct iovec *buf, int32_t size);

void
md5_wrapper(const unsigned char *data, size_t len, char *md5)
{
        unsigned short i = 0;
        unsigned short lim = MD5_DIGEST_LENGTH*2+1;
        unsigned char scratch[MD5_DIGEST_LENGTH] = {0,};
        MD5(data, len, scratch);
        for (; i < MD5_DIGEST_LENGTH; i++)
                snprintf(md5 + i * 2, lim-i*2, "%02x", scratch[i]);
}

/* works similar to mkdir(1) -p.
 */
int
mkdir_p (char *path, mode_t mode, gf_boolean_t allow_symlinks)
{
        int             i               = 0;
        int             ret             = -1;
        char            dir[PATH_MAX]   = {0,};
        struct stat     stbuf           = {0,};

        strncpy (dir, path, (PATH_MAX - 1));
        dir[PATH_MAX - 1] = '\0';

        i = (dir[0] == '/')? 1: 0;
        do {
                if (path[i] != '/' && path[i] != '\0')
                        continue;

                dir[i] = '\0';
                ret = sys_mkdir (dir, mode);
                if (ret && errno != EEXIST) {
                        gf_msg ("", GF_LOG_ERROR, errno, LG_MSG_DIR_OP_FAILED,
                                "Failed due to reason");
                        goto out;
                }

                if (ret && errno == EEXIST && !allow_symlinks) {
                        ret = sys_lstat (dir, &stbuf);
                        if (ret)
                                goto out;

                        if (S_ISLNK (stbuf.st_mode)) {
                                ret = -1;
                                gf_msg ("", GF_LOG_ERROR, 0,
                                        LG_MSG_DIR_IS_SYMLINK, "%s is a "
                                        "symlink", dir);
                                goto out;
                        }
                }
                dir[i] = '/';

        } while (path[i++] != '\0');

        ret = sys_stat (dir, &stbuf);
        if (ret || !S_ISDIR (stbuf.st_mode)) {
                if (ret == 0)
                        errno = 0;
                ret = -1;
                gf_msg ("", GF_LOG_ERROR, errno, LG_MSG_DIR_OP_FAILED, "Failed"
                        " to create directory, possibly some of the components"
                        " were not directories");
                goto out;
        }

        ret = 0;
out:

        return ret;
}

int
gf_lstat_dir (const char *path, struct stat *stbuf_in)
{
        int ret           = -1;
        struct stat stbuf = {0,};

        if (path == NULL) {
                errno = EINVAL;
                goto out;
        }

        ret = sys_lstat (path, &stbuf);
        if (ret)
                goto out;

        if (!S_ISDIR (stbuf.st_mode)) {
                errno = ENOTDIR;
                ret = -1;
                goto out;
        }
        ret = 0;

out:
        if (!ret && stbuf_in)
                *stbuf_in = stbuf;

        return ret;
}

int
log_base2 (unsigned long x)
{
        int val = 0;

        while (x > 1) {
                x /= 2;
                val++;
        }

        return val;
}

/**
 * gf_rev_dns_lookup -- Perform a reverse DNS lookup on the IP address.
 *
 * @ip: The IP address to perform a reverse lookup on
 *
 * @return: success: Allocated string containing the hostname
 *          failure: NULL
 */
char *
gf_rev_dns_lookup (const char *ip)
{
        char               *fqdn = NULL;
        int                ret  = 0;
        struct sockaddr_in sa   = {0};
        char               host_addr[256] = {0, };

        GF_VALIDATE_OR_GOTO ("resolver", ip, out);

        sa.sin_family = AF_INET;
        inet_pton (AF_INET, ip, &sa.sin_addr);
        ret = getnameinfo ((struct sockaddr *)&sa, sizeof (sa), host_addr,
                          sizeof (host_addr), NULL, 0, 0);

        if (ret != 0) {
                gf_msg ("resolver", GF_LOG_INFO, errno,
                        LG_MSG_RESOLVE_HOSTNAME_FAILED, "could not resolve "
                        "hostname for %s", ip);
                goto out;
        }

        /* Get the FQDN */
        fqdn = gf_strdup (host_addr);

out:
       return fqdn;
}

/**
 * gf_resolve_path_parent -- Given a path, returns an allocated string
 *                           containing the parent's path.
 * @path: Path to parse
 * @return: The parent path if found, NULL otherwise
 */
char *
gf_resolve_path_parent (const char *path)
{
        char    *parent = NULL;
        char    *tmp    = NULL;
        char    *pathc  = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, path, out);

        if (strlen (path) <= 0) {
                gf_msg_callingfn (THIS->name, GF_LOG_DEBUG, 0,
                                  LG_MSG_INVALID_STRING,
                                  "invalid string for 'path'");
                goto out;
        }

        /* dup the parameter, we don't want to modify it */
        pathc = strdupa (path);
        if (!pathc) {
                goto out;
        }

        /* Get the parent directory */
        tmp = dirname (pathc);
        if (strcmp (tmp, "/") == 0)
                goto out;

        parent = gf_strdup (tmp);
out:
        return parent;
}

int32_t
gf_resolve_ip6 (const char *hostname,
                uint16_t port,
                int family,
                void **dnscache,
                struct addrinfo **addr_info)
{
        int32_t ret = 0;
        struct addrinfo hints;
        struct dnscache6 *cache = NULL;
        char service[NI_MAXSERV], host[NI_MAXHOST];

        if (!hostname) {
                gf_msg_callingfn ("resolver", GF_LOG_WARNING, 0,
                                  LG_MSG_HOSTNAME_NULL, "hostname is NULL");
                return -1;
        }

        if (!*dnscache) {
                *dnscache = GF_CALLOC (1, sizeof (struct dnscache6),
                                       gf_common_mt_dnscache6);
                if (!*dnscache)
                        return -1;
        }

        cache = *dnscache;
        if (cache->first && !cache->next) {
                freeaddrinfo(cache->first);
                cache->first = cache->next = NULL;
                gf_msg_trace ("resolver", 0, "flushing DNS cache");
        }

        if (!cache->first) {
                char *port_str = NULL;
                gf_msg_trace ("resolver", 0, "DNS cache not present, freshly "
                              "probing hostname: %s", hostname);

                memset(&hints, 0, sizeof(hints));
                hints.ai_family   = family;
                hints.ai_socktype = SOCK_STREAM;

                ret = gf_asprintf (&port_str, "%d", port);
                if (-1 == ret) {
                        return -1;
                }
                if ((ret = getaddrinfo(hostname, port_str, &hints, &cache->first)) != 0) {
                        gf_msg ("resolver", GF_LOG_ERROR, 0,
                                LG_MSG_GETADDRINFO_FAILED, "getaddrinfo failed"
                                " (%s)", gai_strerror (ret));

                        GF_FREE (*dnscache);
                        *dnscache = NULL;
                        GF_FREE (port_str);
                        return -1;
                }
                GF_FREE (port_str);

                cache->next = cache->first;
        }

        if (cache->next) {
                ret = getnameinfo((struct sockaddr *)cache->next->ai_addr,
                                  cache->next->ai_addrlen,
                                  host, sizeof (host),
                                  service, sizeof (service),
                                  NI_NUMERICHOST);
                if (ret != 0) {
                        gf_msg ("resolver", GF_LOG_ERROR, 0,
                                LG_MSG_GETNAMEINFO_FAILED, "getnameinfo failed"
                                " (%s)", gai_strerror (ret));
                        goto err;
                }

                gf_msg_debug ("resolver", 0, "returning ip-%s (port-%s) for "
                              "hostname: %s and port: %d", host, service,
                              hostname, port);

                *addr_info = cache->next;
        }

        if (cache->next)
                cache->next = cache->next->ai_next;
        if (cache->next) {
                ret = getnameinfo((struct sockaddr *)cache->next->ai_addr,
                                  cache->next->ai_addrlen,
                                  host, sizeof (host),
                                  service, sizeof (service),
                                  NI_NUMERICHOST);
                if (ret != 0) {
                        gf_msg ("resolver", GF_LOG_ERROR, 0,
                                LG_MSG_GETNAMEINFO_FAILED, "getnameinfo failed"
                                " (%s)", gai_strerror (ret));
                        goto err;
                }

                gf_msg_debug ("resolver", 0, "next DNS query will return: "
                              "ip-%s port-%s", host, service);
        }

        return 0;

err:
        freeaddrinfo (cache->first);
        cache->first = cache->next = NULL;
        GF_FREE (cache);
        *dnscache = NULL;
        return -1;
}

/**
 * gf_dnscache_init -- Initializes a dnscache struct and sets the ttl
 *                     to the specified value in the parameter.
 *
 * @ttl: the TTL in seconds
 * @return: SUCCESS: Pointer to an allocated dnscache struct
 *          FAILURE: NULL
 */
struct dnscache *
gf_dnscache_init (time_t ttl)
{
        struct dnscache *cache = GF_MALLOC (sizeof (*cache),
                                            gf_common_mt_dnscache);
        cache->cache_dict = NULL;
        cache->ttl = ttl;
        return cache;
}

/**
 * gf_dnscache_entry_init -- Initialize a dnscache entry
 *
 * @return: SUCCESS: Pointer to an allocated dnscache entry struct
 *          FAILURE: NULL
 */
struct dnscache_entry *
gf_dnscache_entry_init ()
{
        struct dnscache_entry *entry = GF_CALLOC (1, sizeof (*entry),
                                                 gf_common_mt_dnscache_entry);
        return entry;
}

/**
 * gf_dnscache_entry_deinit -- Free memory used by a dnscache entry
 *
 * @entry: Pointer to deallocate
 */
void
gf_dnscache_entry_deinit (struct dnscache_entry *entry)
{
        GF_FREE (entry->ip);
        GF_FREE (entry->fqdn);
        GF_FREE (entry);
}

/**
 * gf_rev_dns_lookup -- Perform a reverse DNS lookup on the IP address.
 *
 * @ip: The IP address to perform a reverse lookup on
 *
 * @return: success: Allocated string containing the hostname
 *          failure: NULL
 */
char *
gf_rev_dns_lookup_cached (const char *ip, struct dnscache *dnscache)
{
        char               *fqdn = NULL;
        int                ret  = 0;
        dict_t             *cache = NULL;
        data_t             *entrydata = NULL;
        struct dnscache_entry *dnsentry = NULL;
        gf_boolean_t        from_cache = _gf_false;

        if (!dnscache)
                goto out;

        if (!dnscache->cache_dict) {
                dnscache->cache_dict = dict_new ();
                if (!dnscache->cache_dict) {
                        goto out;
                }
        }
        cache = dnscache->cache_dict;

        /* Quick cache lookup to see if we already hold it */
        entrydata = dict_get (cache, (char *)ip);
        if (entrydata) {
                dnsentry = (struct dnscache_entry *)entrydata->data;
                /* First check the TTL & timestamp */
                if (time (NULL) - dnsentry->timestamp > dnscache->ttl) {
                        gf_dnscache_entry_deinit (dnsentry);
                        entrydata->data = NULL; /* Mark this as 'null' so
                                                 * dict_del () doesn't try free
                                                 * this after we've already
                                                 * freed it.
                                                 */

                        dict_del (cache, (char *)ip); /* Remove this entry */
                } else {
                        /* Cache entry is valid, get the FQDN and return */
                        fqdn = dnsentry->fqdn;
                        from_cache = _gf_true; /* Mark this as from cache */
                        goto out;
                }
        }

        /* Get the FQDN */
        ret =  gf_get_hostname_from_ip ((char *)ip, &fqdn);
        if (ret != 0)
                goto out;

        if (!fqdn) {
                gf_log_callingfn ("resolver", GF_LOG_CRITICAL,
                                  "Allocation failed for the host address");
                goto out;
        }

        from_cache = _gf_false;
out:
        /* Insert into the cache */
        if (fqdn && !from_cache) {
                struct dnscache_entry *entry = gf_dnscache_entry_init ();

                if (!entry) {
                        goto out;
                }
                entry->fqdn = fqdn;
                entry->ip = gf_strdup (ip);
                if (!ip) {
                        gf_dnscache_entry_deinit (entry);
                        goto out;
                }
                entry->timestamp = time (NULL);

                entrydata = bin_to_data (entry, sizeof (*entry));
                dict_set (cache, (char *)ip, entrydata);
        }
        return fqdn;
}

struct xldump {
	int lineno;
};

/* to catch any format discrepencies that may arise in code */
static int nprintf (struct xldump *dump, const char *fmt, ...)
                    __attribute__ ((__format__ (__printf__, 2, 3)));
static int
nprintf (struct xldump *dump, const char *fmt, ...)
{
        va_list  ap;
        char    *msg = NULL;
        char     header[32];
        int      ret = 0;

        ret = snprintf (header, 32, "%3d:", ++dump->lineno);
        if (ret < 0)
                goto out;

        va_start (ap, fmt);
        ret = vasprintf (&msg, fmt, ap);
        va_end (ap);
        if (-1 == ret)
                goto out;

        /* NOTE: No ret value from gf_msg_plain, so unable to compute printed
         * characters. The return value from nprintf is not used, so for now
         * living with it */
        gf_msg_plain (GF_LOG_WARNING, "%s %s", header, msg);

out:
        FREE (msg);
        return 0;
}


static int
xldump_options (dict_t *this, char *key, data_t *value,	void *d)
{
	nprintf (d, "    option %s %s", key, value->data);
	return 0;
}


static void
xldump_subvolumes (xlator_t *this, void *d)
{
	xlator_list_t *subv = NULL;
	int len = 0;
	char *subvstr = NULL;

	subv = this->children;
	if (!this->children)
		return;

	for (subv = this->children; subv; subv = subv->next)
		len += (strlen (subv->xlator->name) + 1);

	subvstr = GF_CALLOC (1, len, gf_common_mt_strdup);

	len = 0;
	for (subv = this->children; subv; subv= subv->next)
		len += sprintf (subvstr + len, "%s%s", subv->xlator->name,
				subv->next ? " " : "");

	nprintf (d, "    subvolumes %s", subvstr);

	GF_FREE (subvstr);
}


static void
xldump (xlator_t *each, void *d)
{
	nprintf (d, "volume %s", each->name);
	nprintf (d, "    type %s", each->type);
	dict_foreach (each->options, xldump_options, d);

	xldump_subvolumes (each, d);

	nprintf (d, "end-volume");
	nprintf (d, " ");
}


void
gf_log_dump_graph (FILE *specfp, glusterfs_graph_t *graph)
{
	struct xldump xld = {0, };

        gf_msg_plain (GF_LOG_WARNING, "Final graph:");
        gf_msg_plain (GF_LOG_WARNING,
                      "+---------------------------------------"
                      "---------------------------------------+");

	xlator_foreach_depth_first (graph->top, xldump, &xld);

        gf_msg_plain (GF_LOG_WARNING,
                      "+---------------------------------------"
                      "---------------------------------------+");
}

static void
gf_dump_config_flags ()
{
        gf_msg_plain_nomem (GF_LOG_ALERT, "configuration details:");

/* have argp */
#ifdef HAVE_ARGP
        gf_msg_plain_nomem (GF_LOG_ALERT, "argp 1");
#endif

/* ifdef if found backtrace */
#ifdef HAVE_BACKTRACE
        gf_msg_plain_nomem (GF_LOG_ALERT, "backtrace 1");
#endif

/* Berkeley-DB version has cursor->get() */
#ifdef HAVE_BDB_CURSOR_GET
        gf_msg_plain_nomem (GF_LOG_ALERT, "bdb->cursor->get 1");
#endif

/* Define to 1 if you have the <db.h> header file. */
#ifdef HAVE_DB_H
        gf_msg_plain_nomem (GF_LOG_ALERT, "db.h 1");
#endif

/* Define to 1 if you have the <dlfcn.h> header file. */
#ifdef HAVE_DLFCN_H
        gf_msg_plain_nomem (GF_LOG_ALERT, "dlfcn 1");
#endif

/* define if fdatasync exists */
#ifdef HAVE_FDATASYNC
        gf_msg_plain_nomem (GF_LOG_ALERT, "fdatasync 1");
#endif

/* Define to 1 if you have the `pthread' library (-lpthread). */
#ifdef HAVE_LIBPTHREAD
        gf_msg_plain_nomem (GF_LOG_ALERT, "libpthread 1");
#endif

/* define if llistxattr exists */
#ifdef HAVE_LLISTXATTR
        gf_msg_plain_nomem (GF_LOG_ALERT, "llistxattr 1");
#endif

/* define if found setfsuid setfsgid */
#ifdef HAVE_SET_FSID
        gf_msg_plain_nomem (GF_LOG_ALERT, "setfsid 1");
#endif

/* define if found spinlock */
#ifdef HAVE_SPINLOCK
        gf_msg_plain_nomem (GF_LOG_ALERT, "spinlock 1");
#endif

/* Define to 1 if you have the <sys/epoll.h> header file. */
#ifdef HAVE_SYS_EPOLL_H
        gf_msg_plain_nomem (GF_LOG_ALERT, "epoll.h 1");
#endif

/* Define to 1 if you have the <sys/extattr.h> header file. */
#ifdef HAVE_SYS_EXTATTR_H
        gf_msg_plain_nomem (GF_LOG_ALERT, "extattr.h 1");
#endif

/* Define to 1 if you have the <sys/xattr.h> header file. */
#ifdef HAVE_SYS_XATTR_H
        gf_msg_plain_nomem (GF_LOG_ALERT, "xattr.h 1");
#endif

/* define if found st_atim.tv_nsec */
#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
        gf_msg_plain_nomem (GF_LOG_ALERT, "st_atim.tv_nsec 1");
#endif

/* define if found st_atimespec.tv_nsec */
#ifdef HAVE_STRUCT_STAT_ST_ATIMESPEC_TV_NSEC
        gf_msg_plain_nomem (GF_LOG_ALERT, "st_atimespec.tv_nsec 1");
#endif

/* Define to the full name and version of this package. */
#ifdef PACKAGE_STRING
        {
                char *msg = NULL;
                int   ret = -1;

                ret = gf_asprintf (&msg, "package-string: %s", PACKAGE_STRING);
                if (ret >= 0) {
                        gf_msg_plain_nomem (GF_LOG_ALERT, msg);
                        GF_FREE (msg);
                }
        }
#endif

        return;
}

/* Obtain a backtrace and print it to the log */
void
gf_print_trace (int32_t signum, glusterfs_ctx_t *ctx)
{
        char         msg[1024] = {0,};
        char         timestr[64] = {0,};
        call_stack_t *stack = NULL;

        /* Now every gf_log call will just write to a buffer and when the
         * buffer becomes full, its written to the log-file. Suppose the process
         * crashes and prints the backtrace in the log-file, then the previous
         * log information will still be in the buffer itself. So flush the
         * contents of the buffer to the log file before printing the backtrace
         * which helps in debugging.
         */
        gf_log_flush();

        gf_log_disable_suppression_before_exit (ctx);

        /* Pending frames, (if any), list them in order */
        gf_msg_plain_nomem (GF_LOG_ALERT, "pending frames:");
        {
                /* FIXME: traversing stacks outside pool->lock */
                list_for_each_entry (stack, &ctx->pool->all_frames,
                                     all_frames) {
                        if (stack->type == GF_OP_TYPE_FOP)
                                sprintf (msg,"frame : type(%d) op(%s)",
                                         stack->type,
                                         gf_fop_list[stack->op]);
                        else
                                sprintf (msg,"frame : type(%d) op(%d)",
                                         stack->type,
                                         stack->op);

                        gf_msg_plain_nomem (GF_LOG_ALERT, msg);
                }
        }

        sprintf (msg, "patchset: %s", GLUSTERFS_REPOSITORY_REVISION);
        gf_msg_plain_nomem (GF_LOG_ALERT, msg);

        sprintf (msg, "signal received: %d", signum);
        gf_msg_plain_nomem (GF_LOG_ALERT, msg);
        {
                /* Dump the timestamp of the crash too, so the previous logs
                   can be related */
                gf_time_fmt (timestr, sizeof timestr, time (NULL),
                             gf_timefmt_FT);
                gf_msg_plain_nomem (GF_LOG_ALERT, "time of crash: ");
                gf_msg_plain_nomem (GF_LOG_ALERT, timestr);
        }

        gf_dump_config_flags ();
        gf_msg_backtrace_nomem (GF_LOG_ALERT, 200);
        sprintf (msg, "---------");
        gf_msg_plain_nomem (GF_LOG_ALERT, msg);

        /* Send a signal to terminate the process */
        signal (signum, SIG_DFL);
        raise (signum);
}

void
trap (void)
{

}

char *
gf_trim (char *string)
{
        register char *s, *t;

        if (string == NULL) {
                return NULL;
        }

        for (s = string; isspace (*s); s++)
                ;

        if (*s == 0)
                return s;

        t = s + strlen (s) - 1;
        while (t > s && isspace (*t))
                t--;
        *++t = '\0';

        return s;
}

int
gf_strsplit (const char *str, const char *delim,
             char ***tokens, int *token_count)
{
        char *_running = NULL;
        char *running = NULL;
        char *token = NULL;
        char **token_list = NULL;
        int count = 0;
        int i = 0;
        int j = 0;

        if (str == NULL || delim == NULL || tokens == NULL || token_count == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                return -1;
        }

        _running = gf_strdup (str);
        if (_running == NULL)
                return -1;

        running = _running;

        while ((token = strsep (&running, delim)) != NULL) {
                if (token[0] != '\0')
                        count++;
        }
        GF_FREE (_running);

        _running = gf_strdup (str);
        if (_running == NULL)
                return -1;

        running = _running;

        if ((token_list = GF_CALLOC (count, sizeof (char *),
                                     gf_common_mt_char)) == NULL) {
                GF_FREE (_running);
                return -1;
        }

        while ((token = strsep (&running, delim)) != NULL) {
                if (token[0] == '\0')
                        continue;

                token_list[i] = gf_strdup (token);
                if (token_list[i] == NULL)
                        goto free_exit;
                i++;
        }

        GF_FREE (_running);

        *tokens = token_list;
        *token_count = count;
        return 0;

free_exit:
        GF_FREE (_running);
        for (j = 0; j < i; j++)
                GF_FREE (token_list[j]);

        GF_FREE (token_list);
        return -1;
}

int
gf_strstr (const char *str, const char *delim, const char *match)
{
        char *tmp      = NULL;
        char *save_ptr = NULL;
        char *tmp_str  = NULL;

        int  ret       = 0;

        tmp_str = strdup (str);

        if (str == NULL || delim == NULL || match == NULL || tmp_str == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                ret = -1;
                goto out;
        }


        tmp = strtok_r (tmp_str, delim, &save_ptr);

        while (tmp) {
                ret = strcmp (tmp, match);

                if (ret == 0)
                        break;

                tmp = strtok_r (NULL, delim, &save_ptr);
        }

out:
        free (tmp_str);

        return ret;

}

int
gf_volume_name_validate (const char *volume_name)
{
        const char *vname = NULL;

        if (volume_name == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                return -1;
        }

        if (!isalpha (volume_name[0]))
                return 1;

        for (vname = &volume_name[1]; *vname != '\0'; vname++) {
                if (!(isalnum (*vname) || *vname == '_'))
                        return 1;
        }

        return 0;
}


int
gf_string2time (const char *str, uint32_t *n)
{
        unsigned long value = 0;
        char *tail = NULL;
        int old_errno = 0;
        const char *s = NULL;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        old_errno = errno;
        errno = 0;
        value = strtol (str, &tail, 0);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (((tail[0] == '\0') ||
              ((tail[0] == 's') && (tail[1] == '\0')) ||
              ((tail[0] == 's') && (tail[1] == 'e') &&
	       (tail[2] == 'c') && (tail[3] == '\0'))))
               goto out;

        else if (((tail[0] == 'm') && (tail[1] == '\0')) ||
                 ((tail[0] == 'm') && (tail[1] == 'i') &&
                  (tail[2] == 'n') && (tail[3] == '\0'))) {
                value = value * GF_MINUTE_IN_SECONDS;
                goto out;
        }

        else if (((tail[0] == 'h') && (tail[1] == '\0')) ||
                 ((tail[0] == 'h') && (tail[1] == 'r') &&
	         (tail[2] == '\0'))) {
                value = value * GF_HOUR_IN_SECONDS;
                goto out;
        }

        else if (((tail[0] == 'd') && (tail[1] == '\0')) ||
                 ((tail[0] == 'd') && (tail[1] == 'a') &&
	         (tail[2] == 'y') && (tail[3] == 's') &&
                 (tail[4] == '\0'))) {
                value = value * GF_DAY_IN_SECONDS;
                goto out;
        }

        else if (((tail[0] == 'w') && (tail[1] == '\0')) ||
                 ((tail[0] == 'w') && (tail[1] == 'k') &&
	         (tail[2] == '\0'))) {
                value = value * GF_WEEK_IN_SECONDS;
                goto out;
        } else {
                return -1;
        }

out:
        *n = value;

        return 0;
}

int
gf_string2percent (const char *str, double *n)
{
        double value = 0;
        char *tail = NULL;
        int old_errno = 0;
        const char *s = NULL;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        old_errno = errno;
        errno = 0;
        value = strtod (str, &tail);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (!((tail[0] == '\0') ||
              ((tail[0] == '%') && (tail[1] == '\0'))))
                return -1;

        *n = value;

        return 0;
}


static int
_gf_string2long (const char *str, long *n, int base)
{
        long value = 0;
        char *tail = NULL;
        int old_errno = 0;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        old_errno = errno;
        errno = 0;
        value = strtol (str, &tail, base);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
                return -1;

        *n = value;

        return 0;
}

static int
_gf_string2ulong (const char *str, unsigned long *n, int base)
{
        unsigned long value = 0;
        char *tail = NULL;
        int old_errno = 0;
        const char *s = NULL;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        old_errno = errno;
        errno = 0;
        value = strtoul (str, &tail, base);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
                return -1;

        *n = value;

        return 0;
}

static int
_gf_string2uint (const char *str, unsigned int *n, int base)
{
        unsigned long value = 0;
        char *tail = NULL;
        int old_errno = 0;
        const char *s = NULL;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        old_errno = errno;
        errno = 0;
        value = strtoul (str, &tail, base);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
                return -1;

        *n = (unsigned int)value;

        return 0;
}

static int
_gf_string2double (const char *str, double *n)
{
        double value     = 0.0;
        char   *tail     = NULL;
        int    old_errno = 0;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        old_errno = errno;
        errno = 0;
        value = strtod (str, &tail);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
                return -1;

        *n = value;

        return 0;
}

static int
_gf_string2longlong (const char *str, long long *n, int base)
{
        long long value = 0;
        char *tail = NULL;
        int old_errno = 0;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        old_errno = errno;
        errno = 0;
        value = strtoll (str, &tail, base);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
                return -1;

        *n = value;

        return 0;
}

static int
_gf_string2ulonglong (const char *str, unsigned long long *n, int base)
{
        unsigned long long value = 0;
        char *tail = NULL;
        int old_errno = 0;
        const char *s = NULL;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        old_errno = errno;
        errno = 0;
        value = strtoull (str, &tail, base);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
                return -1;

        *n = value;

        return 0;
}

int
gf_string2long (const char *str, long *n)
{
        return _gf_string2long (str, n, 0);
}

int
gf_string2ulong (const char *str, unsigned long *n)
{
        return _gf_string2ulong (str, n, 0);
}

int
gf_string2int (const char *str, int *n)
{
        long l = 0;
        int  ret = 0;

        ret = _gf_string2long (str, &l, 0);

        *n = l;
        return ret;
}

int
gf_string2uint (const char *str, unsigned int *n)
{
        return _gf_string2uint (str, n, 0);
}

int
gf_string2double (const char *str, double *n)
{
        return _gf_string2double (str, n);
}

int
gf_string2longlong (const char *str, long long *n)
{
        return _gf_string2longlong (str, n, 0);
}

int
gf_string2ulonglong (const char *str, unsigned long long *n)
{
        return _gf_string2ulonglong (str, n, 0);
}

int
gf_string2int8 (const char *str, int8_t *n)
{
        long l = 0L;
        int rv = 0;

        rv = _gf_string2long (str, &l, 0);
        if (rv != 0)
                return rv;

        if ((l >= INT8_MIN) && (l <= INT8_MAX)) {
                *n = (int8_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2int16 (const char *str, int16_t *n)
{
        long l = 0L;
        int rv = 0;

        rv = _gf_string2long (str, &l, 0);
        if (rv != 0)
                return rv;

        if ((l >= INT16_MIN) && (l <= INT16_MAX)) {
                *n = (int16_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2int32 (const char *str, int32_t *n)
{
        long l = 0L;
        int rv = 0;

        rv = _gf_string2long (str, &l, 0);
        if (rv != 0)
                return rv;

        if ((l >= INT32_MIN) && (l <= INT32_MAX)) {
                *n = (int32_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2int64 (const char *str, int64_t *n)
{
        long long l = 0LL;
        int rv = 0;

        rv = _gf_string2longlong (str, &l, 0);
        if (rv != 0)
                return rv;

        if (l <= INT64_MAX) {
                *n = (int64_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint8 (const char *str, uint8_t *n)
{
        unsigned long l = 0L;
        int rv = 0;

        rv = _gf_string2ulong (str, &l, 0);
        if (rv != 0)
                return rv;

        if (l <= UINT8_MAX) {
                *n = (uint8_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint16 (const char *str, uint16_t *n)
{
        unsigned long l = 0L;
        int rv = 0;

        rv = _gf_string2ulong (str, &l, 0);
        if (rv != 0)
                return rv;

        if (l <= UINT16_MAX) {
                *n = (uint16_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint32 (const char *str, uint32_t *n)
{
        unsigned long l = 0L;
        int rv = 0;

        rv = _gf_string2ulong (str, &l, 0);
        if (rv != 0)
                return rv;

	if (l <= UINT32_MAX) {
                *n = (uint32_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint64 (const char *str, uint64_t *n)
{
        unsigned long long l = 0ULL;
        int rv = 0;

        rv = _gf_string2ulonglong (str, &l, 0);
        if (rv != 0)
                return rv;

        if (l <= UINT64_MAX) {
                *n = (uint64_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2ulong_base10 (const char *str, unsigned long *n)
{
        return _gf_string2ulong (str, n, 10);
}

int
gf_string2uint_base10 (const char *str, unsigned int *n)
{
        return _gf_string2uint (str,  n, 10);
}

int
gf_string2uint8_base10 (const char *str, uint8_t *n)
{
        unsigned long l = 0L;
        int rv = 0;

        rv = _gf_string2ulong (str, &l, 10);
        if (rv != 0)
                return rv;

        if (l <= UINT8_MAX) {
                *n = (uint8_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint16_base10 (const char *str, uint16_t *n)
{
        unsigned long l = 0L;
        int rv = 0;

        rv = _gf_string2ulong (str, &l, 10);
        if (rv != 0)
                return rv;

        if (l <= UINT16_MAX) {
                *n = (uint16_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint32_base10 (const char *str, uint32_t *n)
{
        unsigned long l = 0L;
        int rv = 0;

        rv = _gf_string2ulong (str, &l, 10);
        if (rv != 0)
                return rv;

        if (l <= UINT32_MAX) {
                *n = (uint32_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

int
gf_string2uint64_base10 (const char *str, uint64_t *n)
{
        unsigned long long l = 0ULL;
        int rv = 0;

        rv = _gf_string2ulonglong (str, &l, 10);
        if (rv != 0)
                return rv;

        if (l <= UINT64_MAX) {
                *n = (uint64_t) l;
                return 0;
        }

        errno = ERANGE;
        return -1;
}

char *
gf_uint64_2human_readable (uint64_t n)
{
        int   ret = 0;
        char *str = NULL;

        if (n >= GF_UNIT_PB) {
                ret = gf_asprintf (&str, "%.1lfPB", ((double) n)/GF_UNIT_PB);
                if (ret < 0)
                        goto err;
        } else if (n >= GF_UNIT_TB) {
                ret = gf_asprintf (&str, "%.1lfTB", ((double) n)/GF_UNIT_TB);
                if (ret < 0)
                        goto err;
        } else if (n >= GF_UNIT_GB) {
                ret = gf_asprintf (&str, "%.1lfGB", ((double) n)/GF_UNIT_GB);
                if (ret < 0)
                        goto err;
        } else if (n >= GF_UNIT_MB) {
                ret = gf_asprintf (&str, "%.1lfMB", ((double) n)/GF_UNIT_MB);
                if (ret < 0)
                        goto err;
        } else if (n >= GF_UNIT_KB) {
                ret = gf_asprintf (&str, "%.1lfKB", ((double) n)/GF_UNIT_KB);
                if (ret < 0)
                        goto err;
        } else {
                ret = gf_asprintf (&str, "%luBytes", n);
                if (ret < 0)
                        goto err;
        }
        return str;
err:
        return NULL;
}

int
gf_string2bytesize_range (const char *str, uint64_t *n, uint64_t umax)
{
        double        value      = 0.0;
        int64_t       int_value  = 0;
        uint64_t      unit       = 0;
        int64_t       max        = 0;
        char         *tail       = NULL;
        int           old_errno  = 0;
        const char   *s          = NULL;
        gf_boolean_t  fraction   = _gf_false;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        max = umax & 0x7fffffffffffffffLL;

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        if (strrchr (str, '.'))
                fraction = _gf_true;

        old_errno = errno;
        errno = 0;
        if (fraction)
                value = strtod (str, &tail);
        else
                int_value = strtoll (str, &tail, 10);

        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        if (tail[0] != '\0')
        {
                if (strcasecmp (tail, GF_UNIT_KB_STRING) == 0)
                        unit = GF_UNIT_KB;
                else if (strcasecmp (tail, GF_UNIT_MB_STRING) == 0)
                        unit = GF_UNIT_MB;
                else if (strcasecmp (tail, GF_UNIT_GB_STRING) == 0)
                        unit = GF_UNIT_GB;
                else if (strcasecmp (tail, GF_UNIT_TB_STRING) == 0)
                        unit = GF_UNIT_TB;
                else if (strcasecmp (tail, GF_UNIT_PB_STRING) == 0)
                        unit = GF_UNIT_PB;
                else if (strcasecmp (tail, GF_UNIT_B_STRING) != 0)
                        return -1;

                if (unit > 0) {
                        if (fraction)
                                value *= unit;
                        else
                                int_value *= unit;
                }
        }

        if (fraction) {
                if ((max - value) < 0) {
                        errno = ERANGE;
                        return -1;
                }
                *n = (uint64_t) value;
        } else {
                if ((max - int_value) < 0) {
                        errno = ERANGE;
                        return -1;
                }
                *n = int_value;
        }

        return 0;
}

int
gf_string2bytesize_size (const char *str, size_t *n)
{
        uint64_t u64;
        size_t max = (size_t) - 1;
        int val = gf_string2bytesize_range (str, &u64, max);
        *n = (size_t) u64;
        return val;
}

int
gf_string2bytesize (const char *str, uint64_t *n)
{
        return gf_string2bytesize_range(str, n, UINT64_MAX);
}

int
gf_string2bytesize_uint64 (const char *str, uint64_t *n)
{
        return gf_string2bytesize_range(str, n, UINT64_MAX);
}

int
gf_string2bytesize_int64 (const char *str, int64_t *n)
{
        uint64_t u64 = 0;
        int      ret = 0;

        ret = gf_string2bytesize_range(str, &u64, INT64_MAX);
        *n = (int64_t) u64;
        return ret;
}

int
gf_string2percent_or_bytesize (const char *str, double *n,
			       gf_boolean_t *is_percent)
{
        double value = 0ULL;
        char *tail = NULL;
        int old_errno = 0;
        const char *s = NULL;

        if (str == NULL || n == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                errno = EINVAL;
                return -1;
        }

        for (s = str; *s != '\0'; s++) {
                if (isspace (*s))
                        continue;
                if (*s == '-')
                        return -1;
                break;
        }

        old_errno = errno;
        errno = 0;
        value = strtod (str, &tail);
        if (str == tail)
                errno = EINVAL;

        if (errno == ERANGE || errno == EINVAL)
                return -1;

        if (errno == 0)
                errno = old_errno;

        /*Maximum accepted value for 64 bit OS will be (2^14 -1)PB*/
        if (tail[0] != '\0') {
                if (strcasecmp (tail, GF_UNIT_KB_STRING) == 0)
                        value *= GF_UNIT_KB;
                else if (strcasecmp (tail, GF_UNIT_MB_STRING) == 0)
                        value *= GF_UNIT_MB;
                else if (strcasecmp (tail, GF_UNIT_GB_STRING) == 0)
                        value *= GF_UNIT_GB;
                else if (strcasecmp (tail, GF_UNIT_TB_STRING) == 0)
                        value *= GF_UNIT_TB;
                else if (strcasecmp (tail, GF_UNIT_PB_STRING) == 0)
                        value *= GF_UNIT_PB;
		else if (strcasecmp (tail, GF_UNIT_PERCENT_STRING) == 0)
			*is_percent = _gf_true;
                else
                        return -1;
        }

        /* Error out if we cannot store the value in uint64 */
        if ((UINT64_MAX - value) < 0) {
                errno = ERANGE;
                return -1;
        }

        *n = value;

        return 0;
}

int64_t
gf_str_to_long_long (const char *number)
{
        int64_t unit = 1;
        int64_t ret = 0;
        char *endptr = NULL ;
        if (!number)
                return 0;

        ret = strtoll (number, &endptr, 0);

        if (endptr) {
                switch (*endptr) {
                case 'G':
                case 'g':
                        if ((* (endptr + 1) == 'B') ||(* (endptr + 1) == 'b'))
                                unit = 1024 * 1024 * 1024;
                        break;
                case 'M':
                case 'm':
                        if ((* (endptr + 1) == 'B') ||(* (endptr + 1) == 'b'))
                                unit = 1024 * 1024;
                        break;
                case 'K':
                case 'k':
                        if ((* (endptr + 1) == 'B') ||(* (endptr + 1) == 'b'))
                                unit = 1024;
                        break;
                case '%':
                        unit = 1;
                        break;
                default:
                        unit = 1;
                        break;
                }
        }
        return ret * unit;
}

int
gf_string2boolean (const char *str, gf_boolean_t *b)
{
        if (str == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                return -1;
        }

        if ((strcasecmp (str, "1") == 0) ||
            (strcasecmp (str, "on") == 0) ||
            (strcasecmp (str, "yes") == 0) ||
            (strcasecmp (str, "true") == 0) ||
            (strcasecmp (str, "enable") == 0)) {
                *b = _gf_true;
                return 0;
        }

        if ((strcasecmp (str, "0") == 0) ||
            (strcasecmp (str, "off") == 0) ||
            (strcasecmp (str, "no") == 0) ||
            (strcasecmp (str, "false") == 0) ||
            (strcasecmp (str, "disable") == 0)) {
                *b = _gf_false;
                return 0;
        }

        return -1;
}


int
gf_lockfd (int fd)
{
        struct gf_flock fl;

        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;

        return fcntl (fd, F_SETLK, &fl);
}


int
gf_unlockfd (int fd)
{
        struct gf_flock fl;

        fl.l_type = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;

        return fcntl (fd, F_SETLK, &fl);
}

static void
compute_checksum (char *buf, size_t size, uint32_t *checksum)
{
        int  ret = -1;
        char *checksum_buf = NULL;

        checksum_buf = (char *)(checksum);

        if (!(*checksum)) {
                checksum_buf [0] = 0xba;
                checksum_buf [1] = 0xbe;
                checksum_buf [2] = 0xb0;
                checksum_buf [3] = 0x0b;
        }

        for (ret = 0; ret < (size - 4); ret += 4) {
                checksum_buf[0] ^= (buf[ret]);
                checksum_buf[1] ^= (buf[ret + 1] << 1) ;
                checksum_buf[2] ^= (buf[ret + 2] << 2);
                checksum_buf[3] ^= (buf[ret + 3] << 3);
        }

        for (ret = 0; ret <= (size % 4); ret++) {
                checksum_buf[ret] ^= (buf[(size - 4) + ret] << ret);
        }

        return;
}

#define GF_CHECKSUM_BUF_SIZE 1024

int
get_checksum_for_file (int fd, uint32_t *checksum)
{
        int ret = -1;
        char buf[GF_CHECKSUM_BUF_SIZE] = {0,};

        /* goto first place */
        sys_lseek (fd, 0L, SEEK_SET);
        do {
                ret = sys_read (fd, &buf, GF_CHECKSUM_BUF_SIZE);
                if (ret > 0)
                        compute_checksum (buf, GF_CHECKSUM_BUF_SIZE,
                                          checksum);
        } while (ret > 0);

        /* set it back */
        sys_lseek (fd, 0L, SEEK_SET);

        return ret;
}


int
get_checksum_for_path (char *path, uint32_t *checksum)
{
        int     ret = -1;
        int     fd = -1;

        GF_ASSERT (path);
        GF_ASSERT (checksum);

        fd = open (path, O_RDWR);

        if (fd == -1) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno, LG_MSG_PATH_ERROR,
                        "Unable to open %s", path);
                goto out;
        }

        ret = get_checksum_for_file (fd, checksum);

out:
        if (fd != -1)
                sys_close (fd);

        return ret;
}

/**
 * get_file_mtime -- Given a path, get the mtime for the file
 *
 * @path: The filepath to check the mtime on
 * @stamp: The parameter to set after we get the mtime
 *
 * @returns: success: 0
 *           errors : Errors returned by the stat () call
 */
int
get_file_mtime (const char *path, time_t *stamp)
{
        struct stat     f_stat  = {0};
        int             ret     = -EINVAL;

        GF_VALIDATE_OR_GOTO (THIS->name, path, out);
        GF_VALIDATE_OR_GOTO (THIS->name, stamp, out);

        ret = sys_stat (path, &f_stat);
        if (ret < 0) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        LG_MSG_FILE_STAT_FAILED, "failed to stat %s",
                        path);
                goto out;
        }

        /* Set the mtime */
        *stamp = f_stat.st_mtime;
out:
        return ret;
}

/**
 * gf_is_ip_in_net -- Checks if an IP Address is in a network.
 *                    A network should be specified by something like
 *                    '10.5.153.0/24' (in CIDR notation).
 *
 * @result : Sets to true if the IP is in the network
 * @ip_str : The IP to check
 * @network: The network to check the IP against.
 *
 * @return: success: 0
 *          failure: -EINVAL for bad args, retval of inet_pton otherwise
 */
gf_boolean_t
gf_is_ip_in_net (const char *network, const char *ip_str)
{
        unsigned long ip_buf       = 0;
        unsigned long net_ip_buf   = 0;
        unsigned long subnet_mask  = 0;
        int           ret          = -EINVAL;
        char          *slash       = NULL;
        char          *net_ip      = NULL;
        char          *subnet      = NULL;
        char          *net_str     = NULL;
        int           family       = AF_INET;
        gf_boolean_t  result       = _gf_false;

        GF_ASSERT (network);
        GF_ASSERT (ip_str);

        if (strchr (network, ':'))
                family = AF_INET6;
        else if (strchr (network, '.'))
                family = AF_INET;
        else {
                family = -1;
                goto out;
        }

        net_str = strdupa (network);
        slash = strchr (net_str, '/');
        if (!slash)
                goto out;
        *slash = '\0';

        subnet = slash + 1;
        net_ip = net_str;

        /* Convert IP address to a long */
        ret = inet_pton (family, ip_str, &ip_buf);
        if (ret < 0)
                gf_msg ("common-utils", GF_LOG_ERROR, errno,
                        LG_MSG_INET_PTON_FAILED, "inet_pton() failed");

        /* Convert network IP address to a long */
        ret = inet_pton (family, net_ip, &net_ip_buf);
        if (ret < 0) {
                gf_msg ("common-utils", GF_LOG_ERROR, errno,
                        LG_MSG_INET_PTON_FAILED, "inet_pton() failed");
                goto out;
        }

        /* Converts /x into a mask */
        subnet_mask = (1 << atoi (subnet)) - 1;

        result = ((ip_buf & subnet_mask) == (net_ip_buf & subnet_mask));
out:
        return result;
}

char *
strtail (char *str, const char *pattern)
{
        int i = 0;

        for (i = 0; str[i] == pattern[i] && str[i]; i++);

        if (pattern[i] == '\0')
                return str + i;

        return NULL;
}

void
skipwhite (char **s)
{
        while (isspace (**s))
                (*s)++;
}

char *
nwstrtail (char *str, char *pattern)
{
        for (;;) {
                skipwhite (&str);
                skipwhite (&pattern);

                if (*str != *pattern || !*str)
                        break;

                str++;
                pattern++;
        }

        return *pattern ? NULL : str;
}

void
skipword (char **s)
{
        if (!*s)
                return;

        skipwhite (s);

        while (!isspace(**s))
                (*s)++;
}

char *
get_nth_word (const char *str, int n)
{
        char           buf[4096] = {0};
        char          *start     = NULL;
        char          *word      = NULL;
        int            i         = 0;
        int            word_len  = 0;
        const char    *end       = NULL;

        if (!str)
                goto out;

        snprintf (buf, sizeof (buf), "%s", str);
        start = buf;

        for (i = 0; i < n-1; i++)
                skipword (&start);

        skipwhite (&start);
        end = strpbrk ((const char *)start, " \t\n\0");

        if (!end)
                goto out;

        word_len = labs (end - start);

        word = GF_CALLOC (1, word_len + 1, gf_common_mt_strdup);
        if (!word)
                goto out;

        strncpy (word, start, word_len);
        *(word + word_len) = '\0';
 out:
        return word;
}

/* Syntax formed according to RFC 1912 (RFC 1123 & 952 are more restrictive)  *
   <hname> ::= <gen-name>*["."<gen-name>]                                     *
   <gen-name> ::= <let-or-digit> <[*[<let-or-digit-or-hyphen>]<let-or-digit>] */
char
valid_host_name (char *address, int length)
{
        int             i = 0;
        int             str_len = 0;
        char            ret = 1;
        char            *dup_addr = NULL;
        char            *temp_str = NULL;
        char            *save_ptr = NULL;

        if ((length > _POSIX_HOST_NAME_MAX) || (length < 1)) {
                ret = 0;
                goto out;
        }

        dup_addr = gf_strdup (address);
        if (!dup_addr) {
                ret = 0;
                goto out;
        }

        if (!isalnum (dup_addr[length - 1]) && (dup_addr[length - 1] != '*')) {
                ret = 0;
                goto out;
        }

        /* Check for consecutive dots, which is invalid in a hostname and is
         * ignored by strtok()
         */
        if (strstr (dup_addr, "..")) {
                ret = 0;
                goto out;
        }

        /* gen-name */
        temp_str = strtok_r (dup_addr, ".", &save_ptr);
        do {
                str_len = strlen (temp_str);

                if (!isalnum (temp_str[0]) ||
                    !isalnum (temp_str[str_len-1])) {
                        ret = 0;
                        goto out;
                }
                for (i = 1; i < str_len; i++) {
                        if (!isalnum (temp_str[i]) && (temp_str[i] != '-')) {
                                ret = 0;
                                goto out;
                        }
                }
        } while ((temp_str = strtok_r (NULL, ".", &save_ptr)));

out:
        GF_FREE (dup_addr);
        return ret;
}

/*  Matches all ipv4 address, if wildcard_acc is true  '*' wildcard pattern for*
  subnets is considered as valid strings as well                               */
char
valid_ipv4_address (char *address, int length, gf_boolean_t wildcard_acc)
{
        int octets = 0;
        int value = 0;
        char *tmp = NULL, *ptr = NULL, *prev = NULL, *endptr = NULL;
        char ret = 1;
        int is_wildcard = 0;

        tmp = gf_strdup (address);

        /*
         * To prevent cases where last character is '.' and which have
         * consecutive dots like ".." as strtok ignore consecutive
         * delimeters.
         */
        if (length <= 0 ||
            (strstr (address, "..")) ||
            (!isdigit (tmp[length - 1]) && (tmp[length - 1] != '*'))) {
                ret = 0;
                goto out;
        }

        prev = tmp;
        prev = strtok_r (tmp, ".", &ptr);

        while (prev != NULL) {
                octets++;
                if (wildcard_acc && !strcmp (prev, "*")) {
                        is_wildcard = 1;
                } else {
                        value = strtol (prev, &endptr, 10);
                        if ((value > 255) || (value < 0) ||
                            (endptr != NULL && *endptr != '\0')) {
                                ret = 0;
                                goto out;
                        }
                }
                prev = strtok_r (NULL, ".", &ptr);
        }

        if ((octets > 4) || (octets < 4 && !is_wildcard)) {
                ret = 0;
        }

out:
        GF_FREE (tmp);
        return ret;
}

/**
 * valid_ipv4_subnetwork() takes the pattern and checks if it contains
 * a valid ipv4 subnetwork pattern i.e. xx.xx.xx.xx/n. IPv4 address
 * part (xx.xx.xx.xx) and mask bits lengh part (n). The mask bits lengh
 * must be in 0-32 range (ipv4 addr is 32 bit). The pattern must be
 * in this format.
 *
 * Returns _gf_true if both IP addr and mask bits len are valid
 *         _gf_false otherwise.
 */
gf_boolean_t
valid_ipv4_subnetwork (const char *address)
{
        char         *slash     = NULL;
        char         *paddr     = NULL;
        char         *endptr    = NULL;
        long         prefixlen  = -1;
        gf_boolean_t retv       = _gf_true;

        if (address == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                return _gf_false;
        }

        paddr = gf_strdup (address);
        if (paddr == NULL) /* ENOMEM */
                return _gf_false;

        /*
         * INVALID: If '/' is not present OR
         *          Nothing specified after '/'
         */
        slash = strchr(paddr, '/');
        if ((slash == NULL) || (slash[1] == '\0')) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INVALID_IPV4_FORMAT, "Invalid IPv4 "
                                  "subnetwork format");
                retv = _gf_false;
                goto out;
        }

        *slash = '\0';
        retv = valid_ipv4_address (paddr, strlen(paddr), _gf_false);
        if (retv == _gf_false) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INVALID_IPV4_FORMAT,
                                  "Invalid IPv4 subnetwork address");
                goto out;
        }

        prefixlen = strtol (slash + 1, &endptr, 10);
        if ((errno != 0) || (*endptr != '\0') ||
            (prefixlen < 0) || (prefixlen > IPv4_ADDR_SIZE)) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, 0,
                                  LG_MSG_INVALID_IPV4_FORMAT,
                                  "Invalid IPv4 subnetwork mask");
                retv = _gf_false;
                goto out;
        }

        retv = _gf_true;
out:
        GF_FREE (paddr);
        return retv;
}

char
valid_ipv6_address (char *address, int length, gf_boolean_t wildcard_acc)
{
        int hex_numbers = 0;
        int value = 0;
        int i = 0;
        char *tmp = NULL, *ptr = NULL, *prev = NULL, *endptr = NULL;
        char ret = 1;
        int is_wildcard = 0;
        int is_compressed = 0;

        tmp = gf_strdup (address);

        /* Check for '%' for link local addresses */
        endptr = strchr(tmp, '%');
        if (endptr) {
                *endptr = '\0';
                length = strlen(tmp);
                endptr = NULL;
        }

        /* Check for compressed form */
        if (length <= 0 || tmp[length - 1] == ':') {
                ret = 0;
                goto out;
        }
        for (i = 0; i < (length - 1) ; i++) {
                if (tmp[i] == ':' && tmp[i + 1] == ':') {
                        if (is_compressed == 0)
                                is_compressed = 1;
                        else {
                                ret = 0;
                                goto out;
                        }
                }
        }

        prev = strtok_r (tmp, ":", &ptr);

        while (prev != NULL) {
                hex_numbers++;
                if (wildcard_acc && !strcmp (prev, "*")) {
                        is_wildcard = 1;
                } else {
                        value = strtol (prev, &endptr, 16);
                        if ((value > 0xffff) || (value < 0)
                        || (endptr != NULL && *endptr != '\0')) {
                                ret = 0;
                                goto out;
                        }
                }
                prev = strtok_r (NULL, ":", &ptr);
        }

        if ((hex_numbers > 8) || (hex_numbers < 8 && !is_wildcard
            && !is_compressed)) {
                ret = 0;
        }

out:
        GF_FREE (tmp);
        return ret;
}

char
valid_internet_address (char *address, gf_boolean_t wildcard_acc)
{
        char ret = 0;
        int length = 0;

        if (address == NULL) {
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                goto out;
        }

        length = strlen (address);
        if (length == 0)
                goto out;

        if (valid_ipv4_address (address, length, wildcard_acc)
            || valid_ipv6_address (address, length, wildcard_acc)
            || valid_host_name (address, length))
                ret = 1;

out:
        return ret;
}

/**
 * valid_mount_auth_address - Validate the rpc-auth.addr.allow/reject pattern
 *
 * @param address - Pattern to be validated
 *
 * @return _gf_true if "address" is "*" (anonymous) 'OR'
 *                  if "address" is valid FQDN or valid IPv4/6 address 'OR'
 *                  if "address" contains wildcard chars e.g. "'*' or '?' or '['"
 *                  if "address" is valid ipv4 subnet pattern (xx.xx.xx.xx/n)
 *         _gf_false otherwise
 *
 *
 * NB: If the user/admin set for wildcard pattern, then it does not have
 *     to be validated. Make it similar to the way exportfs (kNFS) works.
 */
gf_boolean_t
valid_mount_auth_address (char *address)
{
        int    length = 0;
        char   *cp    = NULL;

        /* 1. Check for "NULL and empty string */
        if ((address == NULL) || (address[0] == '\0')){
                gf_msg_callingfn (THIS->name, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "argument invalid");
                return _gf_false;
        }

        /* 2. Check for Anonymous */
        if (strcmp(address, "*") == 0)
                return _gf_true;

        for (cp = address; *cp; cp++) {
                /* 3. Check for wildcard pattern */
                if (*cp == '*' || *cp == '?' || *cp == '[') {
                        return _gf_true;
                }

                /*
                 * 4. check for IPv4 subnetwork i.e. xx.xx.xx.xx/n
                 * TODO: check for IPv6 subnetwork
                 * NB: Wildcard must not be mixed with subnetwork.
                 */
                if (*cp == '/') {
                        return valid_ipv4_subnetwork (address);
                }
        }

        /* 5. Check for v4/v6 IP addr and FQDN/hostname */
        length = strlen (address);
        if ((valid_ipv4_address (address, length, _gf_false)) ||
            (valid_ipv6_address (address, length, _gf_false)) ||
            (valid_host_name (address, length))) {
                return _gf_true;
        }

        return _gf_false;
}

/**
 * gf_sock_union_equal_addr - check if two given gf_sock_unions have same addr
 *
 * @param a - first sock union
 * @param b - second sock union
 * @return _gf_true if a and b have same ipv{4,6} addr, _gf_false otherwise
 */
gf_boolean_t
gf_sock_union_equal_addr (union gf_sock_union *a,
                          union gf_sock_union *b)
{
        if (!a || !b) {
                gf_msg ("common-utils", GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Invalid arguments to gf_sock_union_equal_addr");
                return _gf_false;
        }

        if (a->storage.ss_family != b->storage.ss_family)
                return _gf_false;

        switch (a->storage.ss_family) {
        case AF_INET:
                if (a->sin.sin_addr.s_addr == b->sin.sin_addr.s_addr)
                        return _gf_true;
                else
                        return _gf_false;

        case AF_INET6:
                if (memcmp ((void *)(&a->sin6.sin6_addr),
                            (void *)(&b->sin6.sin6_addr),
                            sizeof (a->sin6.sin6_addr)))
                        return _gf_false;
                else
                        return _gf_true;

        default:
                gf_msg_debug ("common-utils", 0, "Unsupported/invalid address "
                              "family");
                break;
        }

        return _gf_false;
}

/*
 * Check if both have same network address.
 * Extract the network address from the sockaddr(s) addr by applying the
 * network mask. If they match, return boolean _gf_true, _gf_false otherwise.
 *
 * (x == y) <=> (x ^ y == 0)
 * (x & y) ^ (x & z) <=> x & (y ^ z)
 *
 * ((ip1 & mask) == (ip2 & mask)) <=> ((mask & (ip1 ^ ip2)) == 0)
 */
gf_boolean_t
mask_match(const uint32_t a, const uint32_t b, const uint32_t m)
{
        return (((a ^ b) & m) == 0);
}


/*Thread safe conversion function*/
char *
uuid_utoa (uuid_t uuid)
{
        char *uuid_buffer = glusterfs_uuid_buf_get ();
        gf_uuid_unparse (uuid, uuid_buffer);
        return uuid_buffer;
}

/*Re-entrant conversion function*/
char *
uuid_utoa_r (uuid_t uuid, char *dst)
{
        if(!dst)
                return NULL;
        gf_uuid_unparse (uuid, dst);
        return dst;
}

/*Thread safe conversion function*/
char *
lkowner_utoa (gf_lkowner_t *lkowner)
{
        char *lkowner_buffer = glusterfs_lkowner_buf_get ();
        lkowner_unparse (lkowner, lkowner_buffer, GF_LKOWNER_BUF_SIZE);
        return lkowner_buffer;
}

/*Re-entrant conversion function*/
char *
lkowner_utoa_r (gf_lkowner_t *lkowner, char *dst, int len)
{
        if(!dst)
                return NULL;
        lkowner_unparse (lkowner, dst, len);
        return dst;
}

gf_boolean_t
is_valid_lease_id (const char *lease_id)
{
        int i = 0;
        gf_boolean_t valid = _gf_false;

        for (i = 0; i < LEASE_ID_SIZE; i++) {
                if (lease_id[i] != 0) {
                        valid = _gf_true;
                        goto out;
                }
        }
out:
        return valid;
}

/* Lease_id can be a either in printable or non printable binary
 * format. This function can be used to print any lease_id.
 *
 * This function returns a pointer to a buf, containing the ascii
 * representation of the value in lease_id, in the following format:
 * 4hexnum-4hexnum-4hexnum-4hexnum-4hexnum-4hexnum-4hexnum-4hexnum
 *
 * Eg: If lease_id = "lid1-clnt1" the printable string would be:
 * 6c69-6431-2d63-6c6e-7431-0000-0000-0000
 *
 * Note: The pointer returned should not be stored for further use, as any
 * subsequent call to this function will override the same buffer.
 */
char *
leaseid_utoa (const char *lease_id)
{
        char *buf = NULL;
        int   i   = 0;
        int   j   = 0;

        buf = glusterfs_leaseid_buf_get ();
        if (!buf)
                goto out;

        for (i = 0; i < LEASE_ID_SIZE; i++) {
                if (i && !(i % 2)) {
                        buf[j] = '-';
                        j++;
                }
                sprintf (&buf[j], "%02hhx", lease_id[i]);
                j += 2;
                if (j == GF_LEASE_ID_BUF_SIZE)
                        break;
        }
        buf[GF_LEASE_ID_BUF_SIZE - 1] = '\0';
out:
        return buf;
}

void* gf_array_elem (void *a, int index, size_t elem_size)
{
        uint8_t* ptr = a;
        return (void*)(ptr + index * elem_size);
}

void
gf_elem_swap (void *x, void *y, size_t l) {
        uint8_t *a = x, *b = y, c;
        while(l--) {
                c = *a;
                *a++ = *b;
                *b++ = c;
        }
}

void
gf_array_insertionsort (void *A, int l, int r, size_t elem_size,
                        gf_cmp cmp)
{
        int  i = l;
        int  N = r+1;
        void *Temp = NULL;
        int  j = 0;

        for(i = l; i < N; i++) {
                Temp = gf_array_elem (A, i, elem_size);
                j = i - 1;
                while (j >= 0 && (cmp (Temp, gf_array_elem (A, j, elem_size))
                      < 0)) {
                        gf_elem_swap (Temp, gf_array_elem (A, j, elem_size),
                                      elem_size);
                        Temp = gf_array_elem (A, j, elem_size);
                        j = j-1;
                }
        }
}

int
gf_is_str_int (const char *value)
{
        int     flag = 0;
        char   *str  = NULL;
        char   *fptr = NULL;

        GF_VALIDATE_OR_GOTO (THIS->name, value, out);

        str = gf_strdup (value);
        if (!str)
                goto out;

        fptr = str;

        while (*str) {
                if (!isdigit(*str)) {
                        flag = 1;
                        goto out;
                }
                str++;
        }

out:
        GF_FREE (fptr);

        return flag;
}
/*
 * rounds up nr to power of two. If nr is already a power of two, just returns
 * nr
 */

int32_t
gf_roundup_power_of_two (int32_t nr)
{
        int32_t result = 1;

        if (nr < 0) {
                gf_msg ("common-utils", GF_LOG_WARNING, 0,
                        LG_MSG_NEGATIVE_NUM_PASSED, "negative number passed");
                result = -1;
                goto out;
        }

        while (result < nr)
                result *= 2;

out:
        return result;
}

/*
 * rounds up nr to next power of two. If nr is already a power of two, next
 * power of two is returned.
 */

int32_t
gf_roundup_next_power_of_two (int32_t nr)
{
        int32_t result = 1;

        if (nr < 0) {
                gf_msg ("common-utils", GF_LOG_WARNING, 0,
                        LG_MSG_NEGATIVE_NUM_PASSED, "negative number passed");
                result = -1;
                goto out;
        }

        while (result <= nr)
                result *= 2;

out:
        return result;
}

int
get_vol_type (int type, int dist_count, int brick_count)
{
        if ((type != GF_CLUSTER_TYPE_TIER) && (type > 0) &&
             (dist_count < brick_count))
              type = type + GF_CLUSTER_TYPE_MAX - 1;

        return type;
}

int
validate_brick_name (char *brick)
{
        char *delimiter = NULL;
        int  ret = 0;
        delimiter = strrchr (brick, ':');
        if (!delimiter || delimiter == brick
            || *(delimiter+1) != '/')
                ret = -1;

        return ret;
}

char *
get_host_name (char *word, char **host)
{
        char *delimiter = NULL;
        delimiter = strrchr (word, ':');
        if (delimiter)
                *delimiter = '\0';
        else
                return NULL;
        *host = word;
        return *host;
}


char *
get_path_name (char *word, char **path)
{
        char *delimiter = NULL;
        delimiter = strchr (word, '/');
        if (!delimiter)
                return NULL;
        *path = delimiter;
        return *path;
}

void
gf_path_strip_trailing_slashes (char *path)
{
        int i = 0;
        int len = 0;

        if (!path)
                return;

        len = strlen (path);
        for (i = len - 1; i > 0; i--) {
                if (path[i] != '/')
                        break;
	}

        if (i < (len -1))
                path [i+1] = '\0';

        return;
}

uint64_t
get_mem_size ()
{
        uint64_t memsize = -1;

#if defined GF_LINUX_HOST_OS || defined GF_SOLARIS_HOST_OS

	uint64_t page_size = 0;
	uint64_t num_pages = 0;

	page_size = sysconf (_SC_PAGESIZE);
	num_pages = sysconf (_SC_PHYS_PAGES);

	memsize = page_size * num_pages;
#endif

#if defined GF_BSD_HOST_OS || defined GF_DARWIN_HOST_OS

	size_t len = sizeof(memsize);
	int name [] = { CTL_HW, HW_PHYSMEM };

	sysctl (name, 2, &memsize, &len, NULL, 0);
#endif
	return memsize;
}

/* Strips all whitespace characters in a string and returns length of new string
 * on success
 */
int
gf_strip_whitespace (char *str, int len)
{
        int     i = 0;
        int     new_len = 0;
        char    *new_str = NULL;

        GF_ASSERT (str);

        new_str = GF_CALLOC (1, len + 1, gf_common_mt_char);
        if (new_str == NULL)
                return -1;

        for (i = 0; i < len; i++) {
                if (!isspace (str[i]))
                        new_str[new_len++] = str[i];
        }
        new_str[new_len] = '\0';

        if (new_len != len) {
                memset (str, 0, len);
                strncpy (str, new_str, new_len);
        }

        GF_FREE (new_str);
        return new_len;
}

int
gf_canonicalize_path (char *path)
{
        int             ret                  = -1;
        int             path_len             = 0;
        int             dir_path_len         = 0;
        char           *tmppath              = NULL;
        char           *dir                  = NULL;
        char           *tmpstr               = NULL;

        if (!path || *path != '/')
                goto out;

        if (!strcmp (path, "/"))
                return 0;

        tmppath = gf_strdup (path);
        if (!tmppath)
                goto out;

        /* Strip the extra slashes and return */
        bzero (path, strlen(path));
        path[0] = '/';
        dir = strtok_r(tmppath, "/", &tmpstr);

        while (dir) {
                dir_path_len = strlen(dir);
                strncpy ((path + path_len + 1), dir, dir_path_len);
                path_len += dir_path_len + 1;
                dir = strtok_r (NULL, "/", &tmpstr);
                if (dir)
                        strncpy ((path + path_len), "/", 1);
        }
        path[path_len] = '\0';
        ret = 0;

 out:
        if (ret)
                gf_msg ("common-utils", GF_LOG_ERROR, 0, LG_MSG_PATH_ERROR,
                        "Path manipulation failed");

        GF_FREE(tmppath);

        return ret;
}

static const char *__gf_timefmts[] = {
        "%F %T",
        "%Y/%m/%d-%T",
        "%b %d %T",
        "%F %H%M%S",
	"%Y-%m-%d-%T",
        "%s",
};

static const char *__gf_zerotimes[] = {
        "0000-00-00 00:00:00",
        "0000/00/00-00:00:00",
        "xxx 00 00:00:00",
        "0000-00-00 000000",
	"0000-00-00-00:00:00",
        "0",
};

void
_gf_timestuff (gf_timefmts *fmt, const char ***fmts, const char ***zeros)
{
        *fmt = gf_timefmt_last;
        *fmts = __gf_timefmts;
        *zeros = __gf_zerotimes;
}


char *
generate_glusterfs_ctx_id (void)
{
        char           tmp_str[1024] = {0,};
        char           hostname[256] = {0,};
        struct timeval tv = {0,};
        char           now_str[32];

        if (gettimeofday (&tv, NULL) == -1) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno,
                        LG_MSG_GETTIMEOFDAY_FAILED, "gettimeofday: "
                        "failed");
        }

        if (gethostname (hostname, 256) == -1) {
                gf_msg ("glusterfsd", GF_LOG_ERROR, errno,
                        LG_MSG_GETHOSTNAME_FAILED, "gethostname: failed");
        }

        gf_time_fmt (now_str, sizeof now_str, tv.tv_sec, gf_timefmt_Ymd_T);
        snprintf (tmp_str, sizeof tmp_str, "%s-%d-%s:%"
#ifdef GF_DARWIN_HOST_OS
                  PRId32,
#else
                  "ld",
#endif
                  hostname, getpid(), now_str, tv.tv_usec);

        return gf_strdup (tmp_str);
}

char *
gf_get_reserved_ports ()
{
        char    *ports_info  = NULL;
#if defined GF_LINUX_HOST_OS
        int     proc_fd      = -1;
        char    *proc_file   = "/proc/sys/net/ipv4/ip_local_reserved_ports";
        char    buffer[4096] = {0,};
        int32_t ret          = -1;

        proc_fd = open (proc_file, O_RDONLY);
        if (proc_fd == -1) {
                /* What should be done in this case? error out from here
                 * and thus stop the glusterfs process from starting or
                 * continue with older method of using any of the available
                 * port? For now 2nd option is considered.
                 */
                gf_msg ("glusterfs", GF_LOG_WARNING, errno,
                        LG_MSG_FILE_OP_FAILED, "could not open the file "
                        "/proc/sys/net/ipv4/ip_local_reserved_ports for "
                        "getting reserved ports info");
                goto out;
        }

        ret = sys_read (proc_fd, buffer, sizeof (buffer));
        if (ret < 0) {
                gf_msg ("glusterfs", GF_LOG_WARNING, errno,
                        LG_MSG_FILE_OP_FAILED, "could not read the file %s for"
                        " getting reserved ports info", proc_file);
                goto out;
        }
        ports_info = gf_strdup (buffer);

out:
        if (proc_fd != -1)
                sys_close (proc_fd);
#endif /* GF_LINUX_HOST_OS */
        return ports_info;
}

int
gf_process_reserved_ports (unsigned char *ports, uint32_t ceiling)
{
        int      ret         = -1;

        memset (ports, 0, GF_PORT_ARRAY_SIZE);

#if defined GF_LINUX_HOST_OS
        char    *ports_info  = NULL;
        char    *tmp         = NULL;
        char    *blocked_port = NULL;

        ports_info = gf_get_reserved_ports ();
        if (!ports_info) {
                gf_msg ("glusterfs", GF_LOG_WARNING, 0,
                        LG_MSG_RESERVED_PORTS_ERROR, "Not able to get reserved"
                        " ports, hence there is a possibility that glusterfs "
                        "may consume reserved port");
                goto out;
        }

        blocked_port = strtok_r (ports_info, ",\n",&tmp);

        while (blocked_port) {
                gf_ports_reserved (blocked_port, ports, ceiling);
                blocked_port = strtok_r (NULL, ",\n", &tmp);
        }

        ret = 0;

out:
        GF_FREE (ports_info);

#else  /* FIXME: Non Linux Host */
        ret = 0;
#endif /* GF_LINUX_HOST_OS */

        return ret;
}

gf_boolean_t
gf_ports_reserved (char *blocked_port, unsigned char *ports, uint32_t ceiling)
{
        gf_boolean_t    result      = _gf_false;
        char            *range_port = NULL;
        int16_t         tmp_port1   = -1;
        int16_t         tmp_port2   = -1;

        if (strstr (blocked_port, "-") == NULL) {
                /* get rid of the new line character*/
                if (blocked_port[strlen(blocked_port) -1] == '\n')
                        blocked_port[strlen(blocked_port) -1] = '\0';
                if (gf_string2int16 (blocked_port, &tmp_port1) == 0) {
                        if (tmp_port1 > ceiling
                            || tmp_port1 < 0) {
                                gf_msg ("glusterfs-socket", GF_LOG_WARNING, 0,
                                        LG_MSG_INVALID_PORT, "invalid port %d",
                                        tmp_port1);
                                result = _gf_true;
                                goto out;
                        } else {
                                gf_msg_debug ("glusterfs", 0, "blocking port "
                                              "%d", tmp_port1);
                                BIT_SET (ports, tmp_port1);
                        }
                } else {
                        gf_msg ("glusterfs-socket", GF_LOG_WARNING, 0,
                                LG_MSG_INVALID_PORT, "%s is not a valid port "
                                "identifier", blocked_port);
                        result = _gf_true;
                        goto out;
                }
        } else {
                range_port = strtok (blocked_port, "-");
                if (!range_port){
                        result = _gf_true;
                        goto out;
                }
                if (gf_string2int16 (range_port, &tmp_port1) == 0) {
                        if (tmp_port1 > ceiling)
                                tmp_port1 = ceiling;
                        if (tmp_port1 < 0)
                                tmp_port1 = 0;
                }
                range_port = strtok (NULL, "-");
                if (!range_port) {
                        result = _gf_true;
                        goto out;
                }
                /* get rid of the new line character*/
                if (range_port[strlen(range_port) -1] == '\n')
                        range_port[strlen(range_port) - 1] = '\0';
                if (gf_string2int16 (range_port, &tmp_port2) == 0) {
                        if (tmp_port2 > ceiling)
                                tmp_port2 = ceiling;
                        if (tmp_port2 < 0)
                                tmp_port2 = 0;
                }
                gf_msg_debug ("glusterfs", 0, "lower: %d, higher: %d",
                              tmp_port1, tmp_port2);
                for (; tmp_port1 <= tmp_port2; tmp_port1++)
                        BIT_SET (ports, tmp_port1);
        }

out:
        return result;
}

/* Takes in client ip{v4,v6} and returns associated hostname, if any
 * Also, allocates memory for the hostname.
 * Returns: 0 for success, -1 for failure
 */
int
gf_get_hostname_from_ip (char *client_ip, char **hostname)
{
        int                      ret                          = -1;
        struct sockaddr         *client_sockaddr              = NULL;
        struct sockaddr_in       client_sock_in               = {0};
        struct sockaddr_in6      client_sock_in6              = {0};
        char                     client_hostname[NI_MAXHOST]  = {0};
        char                    *client_ip_copy               = NULL;
        char                    *tmp                          = NULL;
        char                    *ip                           = NULL;

        /* if ipv4, reverse lookup the hostname to
         * allow FQDN based rpc authentication
         */
        if (valid_ipv4_address (client_ip, strlen (client_ip), 0) == _gf_false) {
                /* most times, we get a.b.c.d:port form, so check that */
                client_ip_copy = gf_strdup (client_ip);
                if (!client_ip_copy)
                        goto out;

                ip = strtok_r (client_ip_copy, ":", &tmp);
        } else {
                ip = client_ip;
        }

        if (valid_ipv4_address (ip, strlen (ip), 0) == _gf_true) {
                client_sockaddr = (struct sockaddr *)&client_sock_in;
                client_sock_in.sin_family = AF_INET;
                ret = inet_pton (AF_INET, ip,
                                 (void *)&client_sock_in.sin_addr.s_addr);

        } else if (valid_ipv6_address (ip, strlen (ip), 0) == _gf_true) {
                client_sockaddr = (struct sockaddr *) &client_sock_in6;

                client_sock_in6.sin6_family = AF_INET6;
                ret = inet_pton (AF_INET6, ip,
                                 (void *)&client_sock_in6.sin6_addr);
        } else {
                goto out;
        }

        if (ret != 1) {
                ret = -1;
                goto out;
        }

        ret = getnameinfo (client_sockaddr,
                           sizeof (*client_sockaddr),
                           client_hostname, sizeof (client_hostname),
                           NULL, 0, 0);
        if (ret) {
                gf_msg ("common-utils", GF_LOG_ERROR, 0,
                        LG_MSG_GETNAMEINFO_FAILED, "Could not lookup hostname "
                        "of %s : %s", client_ip, gai_strerror (ret));
                ret = -1;
                goto out;
        }

        *hostname = gf_strdup ((char *)client_hostname);
 out:
        if (client_ip_copy)
                GF_FREE (client_ip_copy);

        return ret;
}

gf_boolean_t
gf_interface_search (char *ip)
{
        int32_t         ret = -1;
        gf_boolean_t    found = _gf_false;
        struct          ifaddrs *ifaddr, *ifa;
        int             family;
        char            host[NI_MAXHOST];
        xlator_t        *this = NULL;
        char            *pct = NULL;

        this = THIS;

        ret = getifaddrs (&ifaddr);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, LG_MSG_GETIFADDRS_FAILED,
                        "getifaddrs() failed: %s\n", gai_strerror(ret));
                goto out;
        }

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr) {
                        /*
                         * This seemingly happens if an interface hasn't
                         * been bound to a particular protocol (seen with
                         * TUN devices).
                         */
                        continue;
                }
                family = ifa->ifa_addr->sa_family;

                if (family != AF_INET && family != AF_INET6)
                        continue;

                ret = getnameinfo (ifa->ifa_addr,
                        (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                              sizeof(struct sockaddr_in6),
                        host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                LG_MSG_GETNAMEINFO_FAILED, "getnameinfo() "
                                "failed: %s\n", gai_strerror(ret));
                        goto out;
                }

                /*
                 * Sometimes the address comes back as addr%eth0 or
                 * similar.  Since % is an invalid character, we can
                 * strip it out with confidence that doing so won't
                 * harm anything.
                 */
                pct = index(host,'%');
                if (pct) {
                        *pct = '\0';
                }

                if (strncmp (ip, host, NI_MAXHOST) == 0) {
                        gf_msg_debug (this->name, 0, "%s is local address at "
                                      "interface %s", ip, ifa->ifa_name);
                        found = _gf_true;
                        goto out;
                }
        }
out:
        if(ifaddr)
                freeifaddrs (ifaddr);
        return found;
}

char *
get_ip_from_addrinfo (struct addrinfo *addr, char **ip)
{
        char buf[64];
        void *in_addr = NULL;
        struct sockaddr_in *s4 = NULL;
        struct sockaddr_in6 *s6 = NULL;

        switch (addr->ai_family)
        {
                case AF_INET:
                        s4 = (struct sockaddr_in *)addr->ai_addr;
                        in_addr = &s4->sin_addr;
                        break;

                case AF_INET6:
                        s6 = (struct sockaddr_in6 *)addr->ai_addr;
                        in_addr = &s6->sin6_addr;
                        break;

                default:
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                LG_MSG_INVALID_FAMILY, "Invalid family");
                        return NULL;
        }

        if (!inet_ntop(addr->ai_family, in_addr, buf, sizeof(buf))) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0, LG_MSG_CONVERSION_FAILED,
                        "String conversion failed");
                return NULL;
        }

        *ip = gf_strdup (buf);
        return *ip;
}

gf_boolean_t
gf_is_loopback_localhost (const struct sockaddr *sa, char *hostname)
{
        GF_ASSERT (sa);

        gf_boolean_t is_local = _gf_false;
        const struct in_addr *addr4 = NULL;
        const struct in6_addr *addr6 = NULL;
        uint8_t      *ap   = NULL;
        struct in6_addr loopbackaddr6 = IN6ADDR_LOOPBACK_INIT;

        switch (sa->sa_family) {
                case AF_INET:
                        addr4 = &(((struct sockaddr_in *)sa)->sin_addr);
                        ap = (uint8_t*)&addr4->s_addr;
                        if (ap[0] == 127)
                                is_local = _gf_true;
                        break;

                case AF_INET6:
                        addr6 = &(((struct sockaddr_in6 *)sa)->sin6_addr);
                        if (memcmp (addr6, &loopbackaddr6,
                                    sizeof (loopbackaddr6)) == 0)
                                is_local = _gf_true;
                        break;

                default:
                        if (hostname)
                                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                        LG_MSG_INVALID_FAMILY, "unknown "
                                        "address family %d for %s",
                                        sa->sa_family, hostname);
                        break;
        }

        return is_local;
}

gf_boolean_t
gf_is_local_addr (char *hostname)
{
        int32_t         ret = -1;
        struct          addrinfo *result = NULL;
        struct          addrinfo *res = NULL;
        gf_boolean_t    found = _gf_false;
        char            *ip = NULL;
        xlator_t        *this = NULL;
        struct          addrinfo hints;

        this = THIS;

        memset (&hints, 0, sizeof (hints));
        /*
         * Removing AI_ADDRCONFIG from default_hints
         * for being able to use link local ipv6 addresses
         */
        hints.ai_family = AF_UNSPEC;

        ret = getaddrinfo (hostname, NULL, &hints, &result);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, LG_MSG_GETADDRINFO_FAILED,
                        "error in getaddrinfo: %s\n", gai_strerror(ret));
                goto out;
        }

        for (res = result; res != NULL; res = res->ai_next) {
                gf_msg_debug (this->name, 0, "%s ",
                              get_ip_from_addrinfo (res, &ip));

                found = gf_is_loopback_localhost (res->ai_addr, hostname)
                        || gf_interface_search (ip);
                if (found) {
                        GF_FREE (ip);
                        goto out;
                }
                GF_FREE (ip);
        }

out:
        if (result)
                freeaddrinfo (result);

        if (!found)
                gf_msg_debug (this->name, 0, "%s is not local", hostname);

        return found;
}

gf_boolean_t
gf_is_same_address (char *name1, char *name2)
{
        struct addrinfo         *addr1 = NULL;
        struct addrinfo         *addr2 = NULL;
        struct addrinfo         *p = NULL;
        struct addrinfo         *q = NULL;
        gf_boolean_t            ret = _gf_false;
        int                     gai_err = 0;
        struct                  addrinfo hints;

        memset (&hints, 0, sizeof (hints));
        hints.ai_family = AF_UNSPEC;

        gai_err = getaddrinfo(name1, NULL, &hints, &addr1);
        if (gai_err != 0) {
                gf_msg (name1, GF_LOG_WARNING, 0, LG_MSG_GETADDRINFO_FAILED,
                        "error in getaddrinfo: %s\n", gai_strerror(gai_err));
                goto out;
        }

        gai_err = getaddrinfo(name2, NULL, &hints, &addr2);
        if (gai_err != 0) {
                gf_msg (name2, GF_LOG_WARNING, 0, LG_MSG_GETADDRINFO_FAILED,
                        "error in getaddrinfo: %s\n", gai_strerror(gai_err));
                goto out;
        }

        for (p = addr1; p; p = p->ai_next) {
                for (q = addr2; q; q = q->ai_next) {
                        if (p->ai_addrlen != q->ai_addrlen) {
                                continue;
                        }
                        if (memcmp(p->ai_addr,q->ai_addr,p->ai_addrlen)) {
                                continue;
                        }
                        ret = _gf_true;
                        goto out;
                }
        }

out:
        if (addr1) {
                freeaddrinfo(addr1);
        }
        if (addr2) {
                freeaddrinfo(addr2);
        }
        return ret;

}

/* Sets log file path from user provided arguments */
int
gf_set_log_file_path (cmd_args_t *cmd_args, glusterfs_ctx_t *ctx)
{
        int   i = 0;
        int   j = 0;
        int   ret = 0;
        char  tmp_str[1024] = {0,};

        if (!cmd_args)
                goto done;

        if (cmd_args->mount_point) {
                j = 0;
                i = 0;
                if (cmd_args->mount_point[0] == '/')
                        i = 1;
                for (; i < strlen (cmd_args->mount_point); i++,j++) {
                        tmp_str[j] = cmd_args->mount_point[i];
                        if (cmd_args->mount_point[i] == '/')
                                tmp_str[j] = '-';
                }

                ret = gf_asprintf (&cmd_args->log_file,
                                   DEFAULT_LOG_FILE_DIRECTORY "/%s.log",
                                   tmp_str);
                if (ret > 0)
                        ret = 0;
                goto done;
        }

        if (ctx && GF_GLUSTERD_PROCESS == ctx->process_mode) {
                ret = gf_asprintf (&cmd_args->log_file,
                                   DEFAULT_LOG_FILE_DIRECTORY "/%s.log",
                                   GLUSTERD_NAME);
                if (ret > 0)
                        ret = 0;

                goto done;
        }

        if (cmd_args->volfile) {
                j = 0;
                i = 0;
                if (cmd_args->volfile[0] == '/')
                        i = 1;
                for (; i < strlen (cmd_args->volfile); i++,j++) {
                        tmp_str[j] = cmd_args->volfile[i];
                        if (cmd_args->volfile[i] == '/')
                                tmp_str[j] = '-';
                }
                ret = gf_asprintf (&cmd_args->log_file,
                                   DEFAULT_LOG_FILE_DIRECTORY "/%s.log",
                                   tmp_str);
                if (ret > 0)
                        ret = 0;
                goto done;
        }

        if (cmd_args->volfile_server) {

                ret = gf_asprintf (&cmd_args->log_file,
                                   DEFAULT_LOG_FILE_DIRECTORY "/%s-%s-%d.log",
                                   cmd_args->volfile_server,
                                   cmd_args->volfile_id, getpid());
                if (ret > 0)
                        ret = 0;
        }
done:
        return ret;
}

int
gf_set_log_ident (cmd_args_t *cmd_args)
{
        int              ret = 0;
        char            *ptr = NULL;

        if (cmd_args->log_file == NULL) {
                /* no ident source */
                return 0;
        }

        /* TODO: Some idents would look like, etc-glusterfs-glusterd.vol, which
         * seems ugly and can be bettered? */
        /* just get the filename as the ident */
        if (NULL != (ptr = strrchr (cmd_args->log_file, '/'))) {
                ret = gf_asprintf (&cmd_args->log_ident, "%s", ptr + 1);
        } else {
                ret = gf_asprintf (&cmd_args->log_ident, "%s",
                                   cmd_args->log_file);
        }

        if (ret > 0)
                ret = 0;
        else
                return ret;

        /* remove .log suffix */
        if (NULL != (ptr = strrchr (cmd_args->log_ident, '.'))) {
                if (strcmp (ptr, ".log") == 0) {
                        ptr[0] = '\0';
                }
        }

        return ret;
}

int
gf_thread_cleanup_xint (pthread_t thread)
{
        int ret = 0;
        void *res = NULL;

        ret = pthread_cancel (thread);
        if (ret != 0)
                goto error_return;

        ret = pthread_join (thread, &res);
        if (ret != 0)
                goto error_return;

        if (res != PTHREAD_CANCELED)
                goto error_return;

        ret = 0;

 error_return:
        return ret;
}

int
gf_thread_create (pthread_t *thread, const pthread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg)
{
        sigset_t set, old;
        int ret;

        sigemptyset (&set);

        sigfillset (&set);
        sigdelset (&set, SIGSEGV);
        sigdelset (&set, SIGBUS);
        sigdelset (&set, SIGILL);
        sigdelset (&set, SIGSYS);
        sigdelset (&set, SIGFPE);
        sigdelset (&set, SIGABRT);

        pthread_sigmask (SIG_BLOCK, &set, &old);

        ret = pthread_create (thread, attr, start_routine, arg);

        pthread_sigmask (SIG_SETMASK, &old, NULL);

        return ret;
}

int
gf_thread_create_detached (pthread_t *thread,
                         void *(*start_routine)(void *), void *arg)
{
        pthread_attr_t attr;
        int ret = -1;

        ret = pthread_attr_init (&attr);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, ret,
                        LG_MSG_PTHREAD_ATTR_INIT_FAILED,
                        "Thread attribute initialization failed");
                return -1;
        }

        pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

        ret = gf_thread_create (thread, &attr, start_routine, arg);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, ret,
                        LG_MSG_PTHREAD_FAILED,
                        "Thread creation failed");
                ret = -1;
        }

        pthread_attr_destroy (&attr);

        return ret;
}

int
gf_skip_header_section (int fd, int header_len)
{
        int  ret           = -1;

        ret = sys_lseek (fd, header_len, SEEK_SET);
        if (ret == (off_t) -1) {
                gf_msg ("", GF_LOG_ERROR, 0, LG_MSG_SKIP_HEADER_FAILED,
                        "Failed to skip header section");
        } else {
                ret = 0;
        }

        return ret;
}

gf_boolean_t
gf_is_service_running (char *pidfile, int *pid)
{
        FILE            *file = NULL;
        gf_boolean_t    running = _gf_false;
        int             ret = 0;
        int             fno = 0;

        file = fopen (pidfile, "r+");
        if (!file) {
                goto out;
        }

        fno = fileno (file);
        ret = lockf (fno, F_TEST, 0);
        if (ret == -1)
                running = _gf_true;
        if (!pid) {
                goto out;
        }

        ret = fscanf (file, "%d", pid);
        if (ret <= 0) {
                gf_msg ("", GF_LOG_ERROR, errno, LG_MSG_FILE_OP_FAILED,
                        "Unable to read pidfile: %s", pidfile);
                *pid = -1;
        }

        if (!*pid) {
                /*
                 * PID 0 means we've started the process, but it hasn't gotten
                 * far enough to put in a real PID yet.  More details are in
                 * glusterd_brick_start.
                 */
                running = _gf_true;
        }

out:
        if (file)
                fclose (file);
        return running;
}

/* Check if the pid is > 0 */
gf_boolean_t
gf_valid_pid (const char *pid, int length)
{
        gf_boolean_t    ret = _gf_true;
        pid_t           value = 0;
        char           *end_ptr = NULL;

        if (length <= 0) {
                ret = _gf_false;
                goto out;
        }

        value = strtol (pid, &end_ptr, 10);
        if (value <= 0) {
                ret = _gf_false;
        }
out:
        return ret;
}

static int
dht_is_linkfile_key (dict_t *this, char *key, data_t *value, void *data)
{
        gf_boolean_t *linkfile_key_found = NULL;

        if (!data)
                goto out;

        linkfile_key_found = data;

        *linkfile_key_found = _gf_true;
out:
        return 0;
}


gf_boolean_t
dht_is_linkfile (struct iatt *buf, dict_t *dict)
{
        gf_boolean_t linkfile_key_found = _gf_false;

        if (!IS_DHT_LINKFILE_MODE (buf))
                return _gf_false;

        dict_foreach_fnmatch (dict, "*."DHT_LINKFILE_STR, dht_is_linkfile_key,
                              &linkfile_key_found);

        return linkfile_key_found;
}

int
gf_check_log_format (const char *value)
{
        int log_format = -1;

        if (!strcasecmp (value, GF_LOG_FORMAT_NO_MSG_ID))
                log_format = gf_logformat_traditional;
        else if (!strcasecmp (value, GF_LOG_FORMAT_WITH_MSG_ID))
                log_format = gf_logformat_withmsgid;

        if (log_format == -1)
                gf_msg (THIS->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_LOG,
                        "Invalid log-format. possible values are "
                        GF_LOG_FORMAT_NO_MSG_ID "|" GF_LOG_FORMAT_WITH_MSG_ID);

        return log_format;
}

int
gf_check_logger (const char *value)
{
        int logger = -1;

        if (!strcasecmp (value, GF_LOGGER_GLUSTER_LOG))
                logger = gf_logger_glusterlog;
        else if (!strcasecmp (value, GF_LOGGER_SYSLOG))
                logger = gf_logger_syslog;

        if (logger == -1)
                gf_msg (THIS->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_LOG,
                        "Invalid logger. possible values are "
                        GF_LOGGER_GLUSTER_LOG "|" GF_LOGGER_SYSLOG);

        return logger;
}

/* gf_compare_sockaddr compares the given addresses @addr1 and @addr2 for
 * equality, ie. if they both refer to the same address.
 *
 * This was inspired by sock_addr_cmp_addr() from
 * https://www.opensource.apple.com/source/postfix/postfix-197/postfix/src/util/sock_addr.c
 */
gf_boolean_t
gf_compare_sockaddr (const struct sockaddr *addr1,
                     const struct sockaddr *addr2)
{
        GF_ASSERT (addr1 != NULL);
        GF_ASSERT (addr2 != NULL);

        /* Obviously, the addresses don't match if their families are different
         */
        if (addr1->sa_family != addr2->sa_family)
                return _gf_false;


        if (AF_INET == addr1->sa_family) {
                if (((struct sockaddr_in *)addr1)->sin_addr.s_addr ==
                       ((struct sockaddr_in *)addr2)->sin_addr.s_addr)
                        return _gf_true;

        } else if (AF_INET6 == addr1->sa_family) {
                if (memcmp ((char *)&((struct sockaddr_in6 *)addr1)->sin6_addr,
                            (char *)&((struct sockaddr_in6 *)addr2)->sin6_addr,
                            sizeof (struct in6_addr)) == 0)
                        return _gf_true;
        }
        return _gf_false;
}

/*
 * gf_set_timestamp:
 *      It sets the mtime and atime of 'dest' file as of 'src'.
 */

int
gf_set_timestamp  (const char *src, const char* dest)
{
        struct stat    sb             = {0, };
        struct timeval new_time[2]    = {{0, },{0,}};
        int            ret            = 0;
        xlator_t       *this          = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (src);
        GF_ASSERT (dest);

        ret = sys_stat (src, &sb);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        LG_MSG_FILE_STAT_FAILED, "stat on %s", src);
                goto out;
        }
        new_time[0].tv_sec = sb.st_atime;
        new_time[0].tv_usec = ST_ATIM_NSEC (&sb)/1000;

        new_time[1].tv_sec = sb.st_mtime;
        new_time[1].tv_usec = ST_MTIM_NSEC (&sb)/1000;

        /* The granularity is micro seconds as per the current
         * requiremnt. Hence using 'utimes'. This can be updated
         * to 'utimensat' if we need timestamp in nanoseconds.
         */
        ret = sys_utimes (dest, new_time);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, LG_MSG_UTIMES_FAILED,
                        "utimes on %s", dest);
        }
out:
        return ret;
}

static void
gf_backtrace_end (char *buf, size_t frames)
{
        size_t pos = 0;

        if (!buf)
                return;

        pos = strlen (buf);

        frames = min(frames, GF_BACKTRACE_LEN - pos -1);

        if (frames <= 0)
                return;

        memset (buf+pos, ')', frames);
        buf[pos+frames] = '\0';
}

/*Returns bytes written*/
static int
gf_backtrace_append (char *buf, size_t pos, char *framestr)
{
        if (pos >= GF_BACKTRACE_LEN)
                return -1;
        return snprintf (buf+pos, GF_BACKTRACE_LEN-pos, "(--> %s ", framestr);
}

static int
gf_backtrace_fillframes (char *buf)
{
        void    *array[GF_BACKTRACE_FRAME_COUNT];
        size_t  frames                  = 0;
        FILE    *fp                     = NULL;
        char    callingfn[GF_BACKTRACE_FRAME_COUNT-2][1024] = {{0},};
        int     ret                     = -1;
        int     fd                      = -1;
        size_t  idx                     = 0;
        size_t  pos                     = 0;
        size_t  inc                     = 0;
        char    tmpl[32]                = "/tmp/btXXXXXX";

        frames = backtrace (array, GF_BACKTRACE_FRAME_COUNT);
        if (!frames)
                return -1;

        fd = gf_mkostemp (tmpl, 0, O_RDWR);
        if (fd == -1)
                return -1;

        /*The most recent two frames are the calling function and
         * gf_backtrace_save, which we can infer.*/

        backtrace_symbols_fd (&array[2], frames-2, fd);

        fp = fdopen (fd, "r");
        if (!fp) {
                sys_close (fd);
                ret = -1;
                goto out;
        }

        ret = fseek (fp, 0L, SEEK_SET);
        if (ret)
                goto out;

        pos = 0;
        for (idx = 0; idx < frames - 2; idx++) {
                ret = fscanf (fp, "%s", callingfn[idx]);
                if (ret == EOF)
                        break;
                inc = gf_backtrace_append (buf, pos, callingfn[idx]);
                if (inc == -1)
                        break;
                pos += inc;
        }
        gf_backtrace_end (buf, idx);

out:
        if (fp)
                fclose (fp);

        sys_unlink (tmpl);

        return (idx > 0)? 0: -1;

}

/* Optionally takes @buf to save backtrace.  If @buf is NULL, uses the
 * pre-allocated ctx->btbuf to avoid allocating memory while printing
 * backtrace.
 * TODO: This API doesn't provide flexibility in terms of no. of frames
 * of the backtrace is being saved in the buffer. Deferring fixing it
 * when there is a real-use for that.*/

char *
gf_backtrace_save (char *buf)
{
        char *bt = NULL;

        if (!buf) {
                bt = THIS->ctx->btbuf;
                GF_ASSERT (bt);

        } else {
                bt = buf;

        }

        if ((0 == gf_backtrace_fillframes (bt)))
                return bt;

        gf_msg (THIS->name, GF_LOG_WARNING, 0, LG_MSG_BACKTRACE_SAVE_FAILED,
                "Failed to save the backtrace.");
        return NULL;
}

gf_loglevel_t
fop_log_level (glusterfs_fop_t fop, int op_errno)
{
        /* if gfid doesn't exist ESTALE comes */
        if (op_errno == ENOENT || op_errno == ESTALE)
                return GF_LOG_DEBUG;

        if ((fop == GF_FOP_ENTRYLK) ||
            (fop == GF_FOP_FENTRYLK) ||
            (fop == GF_FOP_FINODELK) ||
            (fop == GF_FOP_INODELK) ||
            (fop == GF_FOP_LK)) {
                /*
                 * if non-blocking lock fails EAGAIN comes
                 * if locks xlator is not loaded ENOSYS comes
                 */
                if (op_errno == EAGAIN || op_errno == ENOSYS)
                        return GF_LOG_DEBUG;
        }

        if ((fop == GF_FOP_GETXATTR) ||
            (fop == GF_FOP_FGETXATTR)) {
                if (op_errno == ENOTSUP || op_errno == ENODATA)
                        return GF_LOG_DEBUG;
        }

        if ((fop == GF_FOP_SETXATTR) ||
            (fop == GF_FOP_FSETXATTR) ||
            (fop == GF_FOP_REMOVEXATTR) ||
            (fop == GF_FOP_FREMOVEXATTR)) {
                if (op_errno == ENOTSUP)
                        return GF_LOG_DEBUG;
        }

        if (fop == GF_FOP_MKNOD || fop == GF_FOP_MKDIR)
                if (op_errno == EEXIST)
                        return GF_LOG_DEBUG;

        return GF_LOG_ERROR;
}

/* This function will build absolute path of file/directory from the
 * current location and relative path given from the current location
 * For example consider our current path is /a/b/c/ and relative path
 * from current location is ./../x/y/z .After parsing through this
 * function the absolute path becomes /a/b/x/y/z/.
 *
 * The function gives a pointer to absolute path if it is successful
 * and also returns zero.
 * Otherwise function gives NULL pointer with returning an err value.
 *
 * So the user need to free memory allocated for path.
 *
 */

int32_t
gf_build_absolute_path (char *current_path, char *relative_path, char **path)
{
        char                    *absolute_path          = NULL;
        char                    *token                  = NULL;
        char                    *component              = NULL;
        char                    *saveptr                = NULL;
        char                    *end                    = NULL;
        int                     ret                     = 0;
        size_t                  relativepath_len        = 0;
        size_t                  currentpath_len         = 0;
        size_t                  max_absolutepath_len    = 0;

        GF_ASSERT (current_path);
        GF_ASSERT (relative_path);
        GF_ASSERT (path);

        if (!path || !current_path || !relative_path) {
                ret = -EFAULT;
                goto err;
        }
        /* Check for current and relative path
         * current path should be absolute one and  start from '/'
         * relative path should not start from '/'
         */
        currentpath_len = strlen (current_path);
        if (current_path[0] != '/' || (currentpath_len > PATH_MAX)) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Wrong value for current path %s", current_path);
                ret = -EINVAL;
                goto err;
        }

        relativepath_len = strlen (relative_path);
        if (relative_path[0] == '/' || (relativepath_len > PATH_MAX)) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0, LG_MSG_INVALID_ENTRY,
                        "Wrong value for relative path %s", relative_path);
                ret = -EINVAL;
                goto err;
        }

        /* It is maximum possible value for absolute path */
        max_absolutepath_len = currentpath_len + relativepath_len + 2;

        absolute_path = GF_CALLOC (1, max_absolutepath_len, gf_common_mt_char);
        if (!absolute_path) {
                ret = -ENOMEM;
                goto err;
        }
        absolute_path[0] = '\0';

        /* If current path is root i.e contains only "/", we do not
         * need to copy it
         */
        if (strcmp (current_path, "/") != 0) {
                strcpy (absolute_path, current_path);

                /* We trim '/' at the end for easier string manipulation */
                gf_path_strip_trailing_slashes (absolute_path);
        }

        /* Used to spilt relative path based on '/' */
        component = gf_strdup (relative_path);
        if (!component) {
                ret = -ENOMEM;
                goto err;
        }

        /* In the relative path, we want to consider ".." and "."
         * if token is ".." , we just need to reduce one level hierarchy
         * if token is "." , we just ignore it
         * if token is NULL , end of relative path
         * if absolute path becomes '\0' and still "..", then it is a bad
         * relative path,  it points to out of boundary area and stop
         * building the absolute path
         * All other cases we just concatenate token to the absolute path
         */
        for (token = strtok_r (component,  "/", &saveptr),
             end = strchr (absolute_path, '\0'); token;
             token = strtok_r (NULL, "/", &saveptr)) {
                if (strcmp (token, ".") == 0)
                        continue;

                else if (strcmp (token, "..") == 0) {

                        if (absolute_path[0] == '\0') {
                                ret = -EACCES;
                                goto err;
                         }

                         end = strrchr (absolute_path, '/');
                         *end = '\0';
                } else {
                        ret = snprintf (end, max_absolutepath_len -
                                        strlen (absolute_path), "/%s", token);
                        end = strchr (absolute_path , '\0');
                }
        }

        if (strlen (absolute_path) > PATH_MAX) {
                ret = -EINVAL;
                goto err;
        }
        *path = gf_strdup (absolute_path);

err:
        if (component)
                GF_FREE (component);
        if (absolute_path)
                GF_FREE (absolute_path);
        return ret;
}

/* This is an utility function which will recursively delete
 * a folder and its contents.
 *
 * @param delete_path folder to be deleted.
 *
 * @return 0 on success and -1 on failure.
 */
int
recursive_rmdir (const char *delete_path)
{
        int             ret             = -1;
        char            path[PATH_MAX]  = {0,};
        struct stat     st              = {0,};
        DIR            *dir             = NULL;
        struct dirent  *entry           = NULL;
        struct dirent   scratch[2]      = {{0,},};
        xlator_t       *this            = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_VALIDATE_OR_GOTO (this->name, delete_path, out);

        dir = sys_opendir (delete_path);
        if (!dir) {
                gf_msg_debug (this->name, 0, "Failed to open directory %s. "
                              "Reason : %s", delete_path, strerror (errno));
                ret = 0;
                goto out;
        }

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        while (entry) {
                snprintf (path, PATH_MAX, "%s/%s", delete_path, entry->d_name);
                ret = sys_lstat (path, &st);
                if (ret == -1) {
                        gf_msg_debug (this->name, 0, "Failed to stat entry %s :"
                                      " %s", path, strerror (errno));
                        goto out;
                }

                if (S_ISDIR (st.st_mode))
                        ret = recursive_rmdir (path);
                else
                        ret = sys_unlink (path);

                if (ret) {
                        gf_msg_debug (this->name, 0, " Failed to remove %s. "
                                      "Reason : %s", path, strerror (errno));
                }

                gf_msg_debug (this->name, 0, "%s %s", ret ?
                              "Failed to remove" : "Removed", entry->d_name);

                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }

        ret = sys_closedir (dir);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to close dir %s. Reason :"
                              " %s", delete_path, strerror (errno));
        }

        ret = sys_rmdir (delete_path);
        if (ret) {
                gf_msg_debug (this->name, 0, "Failed to rmdir: %s,err: %s",
                              delete_path, strerror (errno));
        }

out:
        return ret;
}
/*
 * Input: Array of strings 'array' terminating in NULL
 *        string 'elem' to be searched in the array
 *
 * Output: Index of the element in the array if found, '-1' otherwise
 */
int
gf_get_index_by_elem (char **array, char *elem)
{
        int     i = 0;

        for (i = 0; array[i]; i++) {
                if (strcmp (elem, array[i]) == 0)
                        return i;
        }

        return -1;
}

static int
get_pathinfo_host (char *pathinfo, char *hostname, size_t size)
{
        char    *start = NULL;
        char    *end = NULL;
        int     ret  = -1;
        int     i    = 0;

        if (!pathinfo)
                goto out;

        start = strchr (pathinfo, ':');
        if (!start)
                goto out;

        end = strrchr (pathinfo, ':');
        if (start == end)
                goto out;

        memset (hostname, 0, size);
        i = 0;
        while (++start != end)
                hostname[i++] = *start;
        ret = 0;
out:
        return ret;
}

/*Note: 'pathinfo' should be gathered only from one brick*/
int
glusterfs_is_local_pathinfo (char *pathinfo, gf_boolean_t *is_local)
{
        int             ret   = 0;
        char            pathinfohost[1024] = {0};
        char            localhost[1024] = {0};

        *is_local = _gf_false;
        ret = get_pathinfo_host (pathinfo, pathinfohost, sizeof (pathinfohost));
        if (ret)
                goto out;

        ret = gethostname (localhost, sizeof (localhost));
        if (ret)
                goto out;

        if (!strcmp (localhost, pathinfohost))
                *is_local = _gf_true;
out:
        return ret;
}

ssize_t
gf_nread (int fd, void *buf, size_t count)
{
        ssize_t  ret           = 0;
        ssize_t  read_bytes    = 0;

        for (read_bytes = 0; read_bytes < count; read_bytes += ret) {
                ret = sys_read (fd, buf + read_bytes, count - read_bytes);
                if (ret == 0) {
                        break;
                } else if (ret < 0) {
                        if (errno == EINTR)
                                ret = 0;
                        else
                                goto out;
                }
        }

        ret = read_bytes;
out:
        return ret;
}

ssize_t
gf_nwrite (int fd, const void *buf, size_t count)
{
        ssize_t  ret        = 0;
        ssize_t  written    = 0;

        for (written = 0; written != count; written += ret) {
                ret = sys_write (fd, buf + written, count - written);
                if (ret < 0) {
                        if (errno == EINTR)
                                ret = 0;
                        else
                                goto out;
                }
        }

        ret = written;
out:
        return ret;
}

void
gf_free_mig_locks (lock_migration_info_t *locks)
{
        lock_migration_info_t    *current       = NULL;
        lock_migration_info_t    *temp          = NULL;

        if (!locks)
                return;

        if (list_empty (&locks->list))
                return;

        list_for_each_entry_safe (current, temp, &locks->list, list) {
                list_del_init (&current->list);
                GF_FREE (current->client_uid);
                GF_FREE (current);
        }
}

void
_mask_cancellation (void)
{
        (void) pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
}

void
_unmask_cancellation (void)
{
        (void) pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
}

/* This is a wrapper function to add a pointer to a list,
 * which doesn't contain list member
 */
struct list_node*
_list_node_add (void *ptr, struct list_head *list,
               int (*compare)(struct list_head *, struct list_head *))
{
        struct list_node  *node = NULL;

        if (ptr == NULL || list == NULL)
                goto out;

        node = GF_CALLOC (1, sizeof (struct list_node), gf_common_list_node);

        if (node == NULL)
                goto out;

        node->ptr = ptr;
        if (compare)
                list_add_order (&node->list, list, compare);
        else
                list_add_tail (&node->list, list);
out:
        return node;
}

struct list_node*
list_node_add (void *ptr, struct list_head *list)
{
        return _list_node_add (ptr, list, NULL);
}

struct list_node*
list_node_add_order (void *ptr, struct list_head *list,
                     int (*compare)(struct list_head *, struct list_head *))
{
        return _list_node_add (ptr, list, compare);
}

void
list_node_del (struct list_node *node)
{
        if (node == NULL)
                return;

        list_del_init (&node->list);
        GF_FREE (node);
}

const char *
fop_enum_to_pri_string (glusterfs_fop_t fop)
{
        switch (fop) {
        case GF_FOP_OPEN:
        case GF_FOP_STAT:
        case GF_FOP_FSTAT:
        case GF_FOP_LOOKUP:
        case GF_FOP_ACCESS:
        case GF_FOP_READLINK:
        case GF_FOP_OPENDIR:
        case GF_FOP_STATFS:
        case GF_FOP_READDIR:
        case GF_FOP_READDIRP:
        case GF_FOP_GETACTIVELK:
        case GF_FOP_SETACTIVELK:
                return "HIGH";

        case GF_FOP_CREATE:
        case GF_FOP_FLUSH:
        case GF_FOP_LK:
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
        case GF_FOP_UNLINK:
        case GF_FOP_SETATTR:
        case GF_FOP_FSETATTR:
        case GF_FOP_MKNOD:
        case GF_FOP_MKDIR:
        case GF_FOP_RMDIR:
        case GF_FOP_SYMLINK:
        case GF_FOP_RENAME:
        case GF_FOP_LINK:
        case GF_FOP_SETXATTR:
        case GF_FOP_GETXATTR:
        case GF_FOP_FGETXATTR:
        case GF_FOP_FSETXATTR:
        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
        case GF_FOP_IPC:
        case GF_FOP_LEASE:
                return "NORMAL";

        case GF_FOP_READ:
        case GF_FOP_WRITE:
        case GF_FOP_FSYNC:
        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
        case GF_FOP_FSYNCDIR:
        case GF_FOP_XATTROP:
        case GF_FOP_FXATTROP:
        case GF_FOP_RCHECKSUM:
        case GF_FOP_ZEROFILL:
        case GF_FOP_FALLOCATE:
        case GF_FOP_SEEK:
                return "LOW";

        case GF_FOP_NULL:
        case GF_FOP_FORGET:
        case GF_FOP_RELEASE:
        case GF_FOP_RELEASEDIR:
        case GF_FOP_GETSPEC:
        case GF_FOP_MAXVALUE:
        case GF_FOP_DISCARD:
                return "LEAST";
        default:
                return "UNKNOWN";
        }
}

const char *
fop_enum_to_string (glusterfs_fop_t fop)
{
        static const char *const str_map[] = {
                "NULL",
                "STAT",
                "READLINK",
                "MKNOD",
                "MKDIR",
                "UNLINK",
                "RMDIR",
                "SYMLINK",
                "RENAME",
                "LINK",
                "TRUNCATE",
                "OPEN",
                "READ",
                "WRITE",
                "STATFS",
                "FLUSH",
                "FSYNC",
                "SETXATTR",
                "GETXATTR",
                "REMOVEXATTR",
                "OPENDIR",
                "FSYNCDIR",
                "ACCESS",
                "CREATE",
                "FTRUNCATE",
                "FSTAT",
                "LK",
                "LOOKUP",
                "READDIR",
                "INODELK",
                "FINODELK",
                "ENTRYLK",
                "FENTRYLK",
                "XATTROP",
                "FXATTROP",
                "FGETXATTR",
                "FSETXATTR",
                "RCHECKSUM",
                "SETATTR",
                "FSETATTR",
                "READDIRP",
                "FORGET",
                "RELEASE",
                "RELEASEDIR",
                "GETSPEC",
                "FREMOVEXATTR",
                "FALLOCATE",
                "DISCARD",
                "ZEROFILL",
                "IPC",
                "SEEK",
                "COMPOUND",
                "MAXVALUE"};
        if (fop <= GF_FOP_MAXVALUE)
                return str_map[fop];

        return "UNKNOWNFOP";
}

const char *
gf_inode_type_to_str (ia_type_t type)
{
        static const char *const str_ia_type[] = {
                "UNKNOWN",
                "REGULAR FILE",
                "DIRECTORY",
                "LINK",
                "BLOCK DEVICE",
                "CHARACTER DEVICE",
                "PIPE",
                "SOCKET"};
        return str_ia_type[type];
}

gf_boolean_t
gf_is_zero_filled_stat (struct iatt *buf)
{
        if (!buf)
                return 1;

        /* Do not use st_dev because it is transformed to store the xlator id
         * in place of the device number. Do not use st_ino because by this time
         * we've already mapped the root ino to 1 so it is not guaranteed to be
         * 0.
         */
        if ((buf->ia_nlink == 0) && (buf->ia_ctime == 0))
                return 1;

        return 0;
}

void
gf_zero_fill_stat (struct iatt *buf)
{
        buf->ia_nlink = 0;
        buf->ia_ctime = 0;
}

gf_boolean_t
gf_is_valid_xattr_namespace (char *key)
{
        static char *xattr_namespaces[] = {"trusted.", "security.", "system.",
                                           "user.", NULL };
        int i = 0;

        for (i = 0; xattr_namespaces[i]; i++) {
                if (strncmp (key, xattr_namespaces[i],
                             strlen (xattr_namespaces[i])) == 0)
                        return _gf_true;
        }

        return _gf_false;
}

ino_t
gfid_to_ino (uuid_t gfid)
{
        ino_t ino = 0;
        int32_t i;

        for (i = 8; i < 16; i++) {
                ino <<= 8;
                ino += (uint8_t)gfid[i];
        }

        return ino;
}

int
gf_bits_count (uint64_t n)
{
        int val = 0;
#ifdef _GNU_SOURCE
        val = __builtin_popcountll (n);
#else
        n -= (n >> 1) & 0x5555555555555555ULL;
        n = ((n >> 2) & 0x3333333333333333ULL) + (n & 0x3333333333333333ULL);
        n = (n + (n >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
        n += n >> 8;
        n += n >> 16;
        n += n >> 32;
        val = n & 0xFF;
#endif
        return val;
}

int
gf_bits_index (uint64_t n)
{
    return ffsll(n) - 1;
}

const char*
gf_fop_string (glusterfs_fop_t fop)
{
        if ((fop > GF_FOP_NULL) && (fop < GF_FOP_MAXVALUE))
                return gf_fop_list[fop];
        return "INVALID";
}
