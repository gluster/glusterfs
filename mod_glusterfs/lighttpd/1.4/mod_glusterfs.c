/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"

#include "stat_cache.h"
#include "mod_glusterfs.h"
#include "etag.h"
#include "http_chunk.h"
#include "response.h"

#include "fdevent.h"
#include <libglusterfsclient.h>

#ifdef HAVE_ATTR_ATTRIBUTES_H
#include <attr/attributes.h>
#endif

#ifdef HAVE_FAM_H
# include <fam.h>
#endif

#include "sys-mmap.h"

/* NetBSD 1.3.x needs it */
#ifndef MAP_FAILED
# define MAP_FAILED -1
#endif

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

#ifndef HAVE_LSTAT
#define lstat stat
#endif

#if 0
/* enables debug code for testing if all nodes in the stat-cache as accessable */
#define DEBUG_STAT_CACHE
#endif

#ifdef HAVE_LSTAT
#undef HAVE_LSTAT
#endif

#define GLUSTERFS_FILE_CHUNK (FILE_CHUNK + 1)

/* Keep this value large. Each glusterfs_async_read of GLUSTERFS_CHUNK_SIZE results in a network_backend_write of the read data*/

#define GLUSTERFS_CHUNK_SIZE 8192

/**
 * this is a staticfile for a lighttpd plugin
 *
 */

typedef struct glusterfs_async_local {
        int op_ret;
        int op_errno;
        char async_read_complete;
        off_t length;
        size_t read_bytes;
        glusterfs_iobuf_t *buf;
        pthread_mutex_t lock;
        pthread_cond_t cond;
} glusterfs_async_local_t;


typedef struct {
        glusterfs_file_t fd;
        void *buf;
        buffer *glusterfs_path;
        /*  off_t response_content_length; */
        int prefix;
}mod_glusterfs_ctx_t;

/* plugin config for all request/connections */
typedef struct {
        buffer *logfile;
        buffer *loglevel;
        buffer *specfile;
        buffer *prefix;
        buffer *xattr_file_size;
        buffer *document_root;
        array *exclude_exts;
        unsigned short cache_timeout;

        /* FIXME: its a pointer, hence cant be short */
        unsigned long handle;
} plugin_config;

static int (*network_backend_write)(struct server *srv, connection *con, int fd,
                                    chunkqueue *cq);

typedef struct {
        PLUGIN_DATA;
        buffer *range_buf;
        plugin_config **config_storage;
  
        plugin_config conf;
} plugin_data;

typedef struct {
        chunkqueue *cq;
        glusterfs_iobuf_t *buf;
        size_t length;
}mod_glusterfs_chunkqueue;

#ifdef HAVE_FAM_H
typedef struct {
        FAMRequest *req;
        FAMConnection *fc;

        buffer *name;

        int version;
} fam_dir_entry;
#endif

/* the directory name is too long to always compare on it
 * - we need a hash
 * - the hash-key is used as sorting criteria for a tree
 * - a splay-tree is used as we can use the caching effect of it
 */

/* we want to cleanup the stat-cache every few seconds, let's say 10
 *
 * - remove entries which are outdated since 30s
 * - remove entries which are fresh but havn't been used since 60s
 * - if we don't have a stat-cache entry for a directory, release it from the monitor
 */

#ifdef DEBUG_STAT_CACHE
typedef struct {
        int *ptr;

        size_t used;
        size_t size;
} fake_keys;

static fake_keys ctrl;
#endif

int 
mod_glusterfs_readv_async_cbk (int op_ret, int op_errno,
                               glusterfs_iobuf_t *buf,
                               void *cbk_data)
{
        glusterfs_async_local_t *local = cbk_data;
        pthread_mutex_lock (&local->lock);
        {
                local->async_read_complete = 1;
                local->buf = buf;
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                pthread_cond_signal (&local->cond);
        }
        pthread_mutex_unlock (&local->lock);

        return 0;
}

static int
mod_glusterfs_read_async (server *srv, connection *con, chunk *glusterfs_chunk)
{
        glusterfs_async_local_t local;
        off_t end = 0;
        int nbytes;
        int complete;
        chunkqueue *cq = NULL;
        chunk *c = NULL;
        off_t offset = glusterfs_chunk->file.start;
        size_t length = glusterfs_chunk->file.length;
        glusterfs_file_t fd = glusterfs_chunk->file.name;

        pthread_cond_init (&local.cond, NULL);
        pthread_mutex_init (&local.lock, NULL);
  
        //local.fd = fd;
        memset (&local, 0, sizeof (local));

        if (length > 0)
                end = offset + length;

        cq = chunkqueue_init ();
        if (!cq) {
                con->http_status = 500;
                return HANDLER_FINISHED;
        }

        do {
                glusterfs_iobuf_t *buf;
                int i;
                if (length > 0) {
                        nbytes = end - offset;
                        if (nbytes > GLUSTERFS_CHUNK_SIZE)
                                nbytes = GLUSTERFS_CHUNK_SIZE;
                } else
                        nbytes = GLUSTERFS_CHUNK_SIZE;

                glusterfs_read_async(fd, 
                                     nbytes,
                                     offset,
                                     mod_glusterfs_readv_async_cbk,
                                     (void *)&local);

                pthread_mutex_lock (&local.lock);
                {
                        while (!local.async_read_complete) {
                                pthread_cond_wait (&local.cond, &local.lock);
                        }

                        local.async_read_complete = 0;
                        buf = local.buf;

                        if ((int)length < 0)
                                complete = (local.op_ret <= 0);
                        else {
                                local.read_bytes += local.op_ret;
                                complete = ((local.read_bytes == length)
                                            || (local.op_ret <= 0));
                        }
                }
                pthread_mutex_unlock (&local.lock);

                if (local.op_ret > 0) {
                        unsigned long check = 0;
                        for (i = 0; i < buf->count; i++) {
                                buffer *nw_write_buf = buffer_init ();

                                check += buf->vector[i].iov_len;        

                                nw_write_buf->used = nw_write_buf->size = buf->vector[i].iov_len;
                                nw_write_buf->ptr = buf->vector[i].iov_base;

                                //      buffer_copy_memory (nw_write_buf, buf->vector[i].iov_base, buf->vector[i].iov_len + 1);
                                offset += local.op_ret;
                                chunkqueue_append_buffer_weak (cq, nw_write_buf);
                        }
  
                        network_backend_write (srv, con, con->fd, cq);
  
                        if (chunkqueue_written (cq) != local.op_ret) {
                                mod_glusterfs_chunkqueue *gf_cq;
                                glusterfs_chunk->file.start = offset;
                                if ((int)glusterfs_chunk->file.length > 0)
                                        glusterfs_chunk->file.length -= local.read_bytes;

                                gf_cq = calloc (1, sizeof (*gf_cq));
                                /* ERR_ABORT (gf_cq); */
                                gf_cq->cq = cq;
                                gf_cq->buf = buf;
                                gf_cq->length = local.op_ret;
                                glusterfs_chunk->file.mmap.start = (char *)gf_cq;
                                return local.read_bytes;
                        }
      
                        for (c = cq->first ; c; c = c->next) 
                                c->mem->ptr = NULL;

                        chunkqueue_reset (cq);
                }

                glusterfs_free (buf);
        } while (!complete);

        chunkqueue_free (cq);
        glusterfs_close (fd);

        if (local.op_ret < 0)
                con->http_status = 500;

        return (local.op_ret < 0 ? HANDLER_FINISHED : HANDLER_GO_ON);
}

int mod_glusterfs_network_backend_write(struct server *srv, connection *con,
                                        int fd, chunkqueue *cq)
{
        chunk *c, *prev, *first;
        int chunks_written = 0;
        int error = 0;

        for (first = prev = c = cq->first; c; c = c->next, chunks_written++) {

                if (c->type == MEM_CHUNK && c->mem->used && !c->mem->ptr) {
                        if (cq->first != c) {
                                prev->next = NULL;

                                /* call stored network_backend_write */
                                network_backend_write (srv, con, fd, cq);

                                prev->next = c;
                        } 
                        cq->first = c->next;

                        if (c->file.fd < 0) {
                                error = HANDLER_ERROR;
                                break;
                        }

                        if (c->file.mmap.start) {
                                chunk *tmp;
                                mod_glusterfs_chunkqueue *gf_cq = (mod_glusterfs_chunkqueue *)c->file.mmap.start;

                                network_backend_write (srv, con, fd, gf_cq->cq);

                                if ((size_t)chunkqueue_written (gf_cq->cq) 
                                    != gf_cq->length) {
                                        cq->first = first;
                                        return chunks_written;
                                }
                                for (tmp = gf_cq->cq->first ; tmp;
                                     tmp = tmp->next)
                                        tmp->mem->ptr = NULL;

                                chunkqueue_free (gf_cq->cq);
                                glusterfs_free (gf_cq->buf);
                                free (gf_cq);
                                c->file.mmap.start = NULL;
                        }
      
                        mod_glusterfs_read_async (srv, con, c);
                        if (c->file.mmap.start) {
                                /* pending chunkqueue from
                                   mod_glusterfs_read_async to be written to
                                   network */
                                cq->first = first;
                                return chunks_written;
                        }

                        buffer_free (c->mem);
                        c->mem = NULL;

                        c->type = FILE_CHUNK;
                        c->offset = c->file.length = 0;
                        c->file.name = NULL;

                        if (first == c)
                                first = c->next;

                        if (cq->last == c)
                                cq->last = NULL;

                        prev->next = c->next;

                        free(c);
                }     
                prev = c;
        }

        network_backend_write (srv, con, fd, cq);

        cq->first = first;

        return chunks_written;
}

int chunkqueue_append_glusterfs_file (connection *con, glusterfs_file_t fd,
                                      off_t offset, size_t len, size_t buf_size)
{
        chunk *c = NULL;
        c = chunkqueue_get_append_tempfile (con->write_queue);
  
        if (c->file.is_temp) {
                close (c->file.fd);
                unlink (c->file.name->ptr);
        }

        c->type = MEM_CHUNK;

        buffer_free (c->mem);

        c->mem = buffer_init ();
        c->mem->used = len + 1;
        c->mem->size = buf_size;
        c->mem->ptr = NULL;
        c->offset = 0;

        buffer_free (c->file.name);

        /* fd returned by libglusterfsclient is a pointer */
        c->file.name = (buffer *)fd;
        c->file.start = offset;
        c->file.length = len;

        //c->file.fd = fd;
        c->file.mmap.start = NULL;
        return 0;
}

/* init the plugin data */
INIT_FUNC(mod_glusterfs_init) {
        plugin_data *p;

        p = calloc(1, sizeof(*p));
        network_backend_write = NULL;

        return p;
}

/* detroy the plugin data */
FREE_FUNC(mod_glusterfs_free) {
        plugin_data *p = p_d;

        UNUSED (srv);

        if (!p) return HANDLER_GO_ON;
  
        if (p->config_storage) {
                size_t i;
                for (i = 0; i < srv->config_context->used; i++) {
                        plugin_config *s = p->config_storage[i];
      
                        buffer_free (s->logfile);
                        buffer_free (s->loglevel);
                        buffer_free (s->specfile);
                        buffer_free (s->prefix);
                        buffer_free (s->xattr_file_size);
                        buffer_free (s->document_root);
                        array_free (s->exclude_exts);
  
                        free (s);
                }
                free (p->config_storage);
        }
        buffer_free (p->range_buf);

        free (p);
  
        return HANDLER_GO_ON;
}

SETDEFAULTS_FUNC(mod_glusterfs_set_defaults) {
        plugin_data *p = p_d;
        size_t i = 0;
  
        config_values_t cv[] = {
                { "glusterfs.logfile",              NULL, T_CONFIG_STRING,
                  T_CONFIG_SCOPE_CONNECTION },
    
                { "glusterfs.loglevel",             NULL, T_CONFIG_STRING,
                  T_CONFIG_SCOPE_CONNECTION },    

                { "glusterfs.volume-specfile",      NULL, T_CONFIG_STRING,
                  T_CONFIG_SCOPE_CONNECTION }, 

                { "glusterfs.cache-timeout",        NULL, T_CONFIG_SHORT,
                  T_CONFIG_SCOPE_CONNECTION },
    
                { "glusterfs.exclude-extensions",   NULL, T_CONFIG_ARRAY,
                  T_CONFIG_SCOPE_CONNECTION },
    
                /*TODO: get the prefix from config_conext and 
                  remove glusterfs.prefix from conf file */
                { "glusterfs.prefix",               NULL, T_CONFIG_STRING,
                  T_CONFIG_SCOPE_CONNECTION },
    
                { "glusterfs.xattr-interface-size-limit", NULL, T_CONFIG_STRING,
                  T_CONFIG_SCOPE_CONNECTION },

                { "glusterfs.document-root",        NULL, T_CONFIG_STRING,
                  T_CONFIG_SCOPE_CONNECTION },
    
                { NULL,                          NULL, T_CONFIG_UNSET,
                  T_CONFIG_SCOPE_UNSET }
        };
  
        p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));
        /* ERR_ABORT (p->config_storage);*/
        p->range_buf = buffer_init ();
  
        for (i = 0; i < srv->config_context->used; i++) {
                plugin_config *s;

                s = calloc(1, sizeof(plugin_config));
                /* ERR_ABORT (s); */
                s->logfile = buffer_init ();
                s->loglevel = buffer_init ();
                s->specfile = buffer_init ();
                s->document_root = buffer_init ();
                s->exclude_exts = array_init ();
                s->prefix = buffer_init ();
                s->xattr_file_size = buffer_init ();
    
                cv[0].destination = s->logfile;
                cv[1].destination = s->loglevel;
                cv[2].destination = s->specfile;
                cv[3].destination = &s->cache_timeout;
                cv[4].destination = s->exclude_exts;
                cv[5].destination = s->prefix;
                cv[6].destination = s->xattr_file_size;
                cv[7].destination = s->document_root;
                p->config_storage[i] = s;
    
                if (0 != config_insert_values_global(srv,
                                                     ((data_config *)srv->config_context->data[i])->value,
                                                     cv)) {
                        return HANDLER_FINISHED;
                }
        }
  
        return HANDLER_GO_ON;
}

#define PATCH(x)                                \
        p->conf.x = s->x;

static int mod_glusterfs_patch_connection(server *srv, connection *con,
                                          plugin_data *p) {
        size_t i, j;
        plugin_config *s;

        /* skip the first, the global context */
        /* glusterfs related config can only occur inside $HTTP["url"] == "<glusterfs-prefix>" */
        p->conf.logfile = NULL;
        p->conf.loglevel = NULL;
        p->conf.specfile = NULL;
        p->conf.cache_timeout = 0;
        p->conf.exclude_exts = NULL;
        p->conf.prefix = NULL;
        p->conf.xattr_file_size = NULL;
        p->conf.document_root = NULL;

        for (i = 1; i < srv->config_context->used; i++) {
                data_config *dc = (data_config *)srv->config_context->data[i];
                s = p->config_storage[i];

                /* condition didn't match */
                if (!config_check_cond(srv, con, dc)) continue;
    
                /* merge config */
                for (j = 0; j < dc->value->used; j++) {
                        data_unset *du = dc->value->data[j];
      
                        if (buffer_is_equal_string (du->key,
                                                    CONST_STR_LEN("glusterfs.logfile"))) {
                                PATCH (logfile);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN("glusterfs.loglevel"))) {
                                PATCH (loglevel);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN ("glusterfs.volume-specfile"))) {
                                PATCH (specfile);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN("glusterfs.cache-timeout"))) {
                                PATCH (cache_timeout);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN ("glusterfs.exclude-extensions"))) {
                                PATCH (exclude_exts);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN ("glusterfs.prefix"))) {
                                PATCH (prefix);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN ("glusterfs.xattr-interface-size-limit"))) {
                                PATCH (xattr_file_size);
                        } else if (buffer_is_equal_string (du->key,
                                                           CONST_STR_LEN ("glusterfs.document-root"))) {
                                PATCH (document_root);
                        }
                }
        }
        return 0;
}

#undef PATCH

static int http_response_parse_range(server *srv, connection *con,
                                     plugin_data *p) {
        int multipart = 0;
        int error;
        off_t start, end;
        const char *s, *minus;
        char *boundary = "fkj49sn38dcn3";
        data_string *ds;
        stat_cache_entry *sce = NULL;
        buffer *content_type = NULL;
        size_t size = 0;
        mod_glusterfs_ctx_t *ctx = con->plugin_ctx[p->id];

        if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr) {
                size = atoi (p->conf.xattr_file_size->ptr);
        }

        if (HANDLER_ERROR == stat_cache_get_entry(srv, con, con->physical.path,
                                                  &sce)) {
                SEGFAULT();
        }
  
        start = 0;
        end = sce->st.st_size - 1;
  
        con->response.content_length = 0;
  
        if (NULL != (ds = (data_string *)array_get_element(con->response.headers,
                                                           "Content-Type"))) {
                content_type = ds->value;
        }
  
        for (s = con->request.http_range, error = 0;
             !error && *s && NULL != (minus = strchr(s, '-')); ) {
                char *err;
                off_t la, le;
    
                if (s == minus) {
                        /* -<stop> */
      
                        le = strtoll(s, &err, 10);
      
                        if (le == 0) {
                                /* RFC 2616 - 14.35.1 */
        
                                con->http_status = 416;
                                error = 1;
                        } else if (*err == '\0') {
                                /* end */
                                s = err;
        
                                end = sce->st.st_size - 1;
                                start = sce->st.st_size + le;
                        } else if (*err == ',') {
                                multipart = 1;
                                s = err + 1;
        
                                end = sce->st.st_size - 1;
                                start = sce->st.st_size + le;
                        } else {
                                error = 1;
                        }
      
                } else if (*(minus+1) == '\0' || *(minus+1) == ',') {
                        /* <start>- */
      
                        la = strtoll(s, &err, 10);
      
                        if (err == minus) {
                                /* ok */
        
                                if (*(err + 1) == '\0') {
                                        s = err + 1;
          
                                        end = sce->st.st_size - 1;
                                        start = la;
          
                                } else if (*(err + 1) == ',') {
                                        multipart = 1;
                                        s = err + 2;
          
                                        end = sce->st.st_size - 1;
                                        start = la;
                                } else {
                                        error = 1;
                                }
                        } else {
                                /* error */
                                error = 1;
                        }
                } else {
                        /* <start>-<stop> */
      
                        la = strtoll(s, &err, 10);

                        if (err == minus) {
                                le = strtoll(minus+1, &err, 10);
        
                                /* RFC 2616 - 14.35.1 */
                                if (la > le) {
                                        error = 1;
                                }
        
                                if (*err == '\0') {
                                        /* ok, end*/
                                        s = err;

                                        end = le;
                                        start = la;
                                } else if (*err == ',') {
                                        multipart = 1;
                                        s = err + 1;
          
                                        end = le;
                                        start = la;
                                } else {
                                        /* error */
          
                                        error = 1;
                                }
                        } else {
                                /* error */
        
                                error = 1;
                        }
                }
    
                if (!error) {
                        if (start < 0) start = 0;
      
                        /* RFC 2616 - 14.35.1 */
                        if (end > sce->st.st_size - 1) end = sce->st.st_size - 1;
      
                        if (start > sce->st.st_size - 1) {
                                error = 1;
        
                                con->http_status = 416;
                        }
                }
    
                if (!error) {
                        if (multipart) {
                                /* write boundary-header */
                                buffer *b;
        
                                b = chunkqueue_get_append_buffer(con->write_queue);
        
                                buffer_copy_string(b, "\r\n--");
                                buffer_append_string(b, boundary);
        
                                /* write Content-Range */
                                buffer_append_string(b, "\r\nContent-Range: bytes ");
                                buffer_append_off_t(b, start);
                                buffer_append_string(b, "-");
                                buffer_append_off_t(b, end);
                                buffer_append_string(b, "/");
                                buffer_append_off_t(b, sce->st.st_size);
        
                                buffer_append_string(b, "\r\nContent-Type: ");
                                buffer_append_string_buffer(b, content_type);
        
                                /* write END-OF-HEADER */
                                buffer_append_string(b, "\r\n\r\n");
        
                                con->response.content_length += b->used - 1;

                        }

                        /* path = con->physical.path->ptr + p->conf.prefix->used - 1 + con->physical.doc_root->used - 1;      */
                        /*
                          fd = glusterfs_open (p->conf.handle, path, O_RDONLY);
                          if (fd < 0)
                          return HANDLER_ERROR;
                        */
                        /*      fn = buffer_init_string (path); */
                        if ((size_t)sce->st.st_size >= size) {
                                chunkqueue_append_glusterfs_file(con, ctx->fd,
                                                                 start,
                                                                 end - start,
                                                                 size);
                        } else {
                                if (!start) {
                                        buffer *mem = buffer_init ();
                                        mem->ptr = ctx->buf;
                                        mem->used = mem->size = sce->st.st_size;
                                        http_chunk_append_buffer (srv, con, mem);
                                        ctx->buf = NULL;
                                } else {
                                        chunkqueue_append_mem (con->write_queue,
                                                               ((char *)ctx->buf) + start,
                                                               end - start + 1);
                                }
                        }

                        con->response.content_length += end - start + 1;
                }
        }

        if (ctx->buf) {
                free (ctx->buf);
                ctx->buf = NULL;
        }

        /* something went wrong */
        if (error) return -1;
  
        if (multipart) {
                /* add boundary end */
                buffer *b;
    
                b = chunkqueue_get_append_buffer(con->write_queue);
    
                buffer_copy_string_len(b, "\r\n--", 4);
                buffer_append_string(b, boundary);
                buffer_append_string_len(b, "--\r\n", 4);
    
                con->response.content_length += b->used - 1;
    
                /* set header-fields */

                buffer_copy_string(p->range_buf, "multipart/byteranges; boundary=");
                buffer_append_string(p->range_buf, boundary);

                /* overwrite content-type */
                response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"),
                                          CONST_BUF_LEN(p->range_buf));
        } else {
                /* add Content-Range-header */
    
                buffer_copy_string(p->range_buf, "bytes ");
                buffer_append_off_t(p->range_buf, start);
                buffer_append_string(p->range_buf, "-");
                buffer_append_off_t(p->range_buf, end);
                buffer_append_string(p->range_buf, "/");
                buffer_append_off_t(p->range_buf, sce->st.st_size);
    
                response_header_insert(srv, con, CONST_STR_LEN("Content-Range"),
                                       CONST_BUF_LEN(p->range_buf));
        }
        
        /* ok, the file is set-up */
        return 0;
}

PHYSICALPATH_FUNC(mod_glusterfs_handle_physical) {
        plugin_data *p = p_d;
        stat_cache_entry *sce;
        mod_glusterfs_ctx_t *plugin_ctx = NULL;
        size_t size = 0;

        if (con->http_status != 0) return HANDLER_GO_ON;
        if (con->uri.path->used == 0) return HANDLER_GO_ON;
        if (con->physical.path->used == 0) return HANDLER_GO_ON;

        if (con->mode != DIRECT) return HANDLER_GO_ON;

        /*
          network_backend_write = srv->network_backend_write;
          srv->network_backend_write = mod_glusterfs_network_backend_write;
        */

        switch (con->request.http_method) {
        case HTTP_METHOD_GET:
        case HTTP_METHOD_POST:
        case HTTP_METHOD_HEAD:
                break;

        default:
                return HANDLER_GO_ON;
        }

        mod_glusterfs_patch_connection(srv, con, p);
        if (!p->conf.prefix || p->conf.prefix->used == 0) {
                return HANDLER_GO_ON;
        }

        if (!p->conf.document_root || p->conf.document_root->used == 0) {
                log_error_write(srv, __FILE__, __LINE__, "s",
                                "glusterfs.document-root is not specified");
                con->http_status = 500;
                return HANDLER_FINISHED;
        }

        if (p->conf.handle <= 0) {
                glusterfs_init_params_t ctx;

                if (!p->conf.specfile || p->conf.specfile->used == 0) {
                        return HANDLER_GO_ON;
                }
                memset (&ctx, 0, sizeof (ctx));

                ctx.specfile = p->conf.specfile->ptr;
                ctx.logfile = p->conf.logfile->ptr;
                ctx.loglevel = p->conf.loglevel->ptr;
                ctx.lookup_timeout = ctx.stat_timeout = p->conf.cache_timeout;

                p->conf.handle = (long)glusterfs_init (&ctx);

                if (p->conf.handle <= 0) {
                        con->http_status = 500;
                        log_error_write(srv, __FILE__, __LINE__,  "sbs",
                                        "glusterfs initialization failed, please check your configuration. Glusterfs logfile ",
                                        p->conf.logfile,
                                        "might contain details");
                        return HANDLER_FINISHED;
                }
        }

        size = 0;
        if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr) 
                size = atoi (p->conf.xattr_file_size->ptr);

        if (!con->plugin_ctx[p->id]) {
/* FIXME: what if multiple files are requested from a single connection? */ 
/* TODO: check whether this works fine for HTTP protocol 1.1 */

                buffer *tmp_buf = buffer_init_buffer (con->physical.basedir);

                plugin_ctx = calloc (1, sizeof (*plugin_ctx));
                /* ERR_ABORT (plugin_ctx); */
                con->plugin_ctx[p->id] = plugin_ctx;
    
                buffer_append_string_buffer (tmp_buf, p->conf.prefix);
                buffer_path_simplify (tmp_buf, tmp_buf);

                plugin_ctx->prefix = tmp_buf->used - 1;
                if (tmp_buf->ptr[plugin_ctx->prefix - 1] == '/')
                        plugin_ctx->prefix--;

                buffer_free (tmp_buf);
        } else 
                /*FIXME: error!! error!! */
                plugin_ctx = con->plugin_ctx[p->id];


        if (size) 
        {
                plugin_ctx->buf = malloc (size);
                /* ERR_ABORT (plugin_ctx->buf); */
        }

        plugin_ctx->glusterfs_path = buffer_init ();
        buffer_copy_string_buffer (plugin_ctx->glusterfs_path,
                                   p->conf.document_root);
        buffer_append_string (plugin_ctx->glusterfs_path, "/");
        buffer_append_string (plugin_ctx->glusterfs_path,
                              con->physical.path->ptr + plugin_ctx->prefix);
        buffer_path_simplify (plugin_ctx->glusterfs_path,
                              plugin_ctx->glusterfs_path);
 
        if (glusterfs_stat_cache_get_entry (srv, con,
                                            (glusterfs_handle_t )p->conf.handle,
                                            plugin_ctx->glusterfs_path,
                                            con->physical.path, plugin_ctx->buf,
                                            size, &sce) == HANDLER_ERROR) {
                if (errno == ENOENT)
                        con->http_status = 404;
                else 
                        con->http_status = 403;

                free (plugin_ctx->buf);
                buffer_free (plugin_ctx->glusterfs_path);
                plugin_ctx->glusterfs_path = NULL;
                plugin_ctx->buf = NULL;

                free (plugin_ctx);
                con->plugin_ctx[p->id] = NULL;

                return HANDLER_FINISHED;
        }

        if (!(S_ISREG (sce->st.st_mode) && (size_t)sce->st.st_size < size)) {
                free (plugin_ctx->buf);
                plugin_ctx->buf = NULL;
        }

        return HANDLER_GO_ON;
}

static int http_chunk_append_len(server *srv, connection *con, size_t len) {
        size_t i, olen = len, j;
        buffer *b;

        b = srv->tmp_chunk_len;

        if (len == 0) {
                buffer_copy_string(b, "0");
        } else {
                for (i = 0; i < 8 && len; i++) {
                        len >>= 4;
                }
    
                /* i is the number of hex digits we have */
                buffer_prepare_copy(b, i + 1);
    
                for (j = i-1, len = olen; j+1 > 0; j--) {
                        b->ptr[j] = (len & 0xf) + (((len & 0xf) <= 9) ? 
                                                   '0' : 'a' - 10);
                        len >>= 4;
                }
                b->used = i;
                b->ptr[b->used++] = '\0';
        }
  
        buffer_append_string(b, "\r\n");
        chunkqueue_append_buffer(con->write_queue, b);
  
        return 0;
}

int http_chunk_append_glusterfs_file_chunk(server *srv, connection *con,
                                           glusterfs_file_t fd, off_t offset,
                                           off_t len, size_t buf_size) {
        chunkqueue *cq;

        if (!con) return -1;

        cq = con->write_queue;

        if (con->response.transfer_encoding & HTTP_TRANSFER_ENCODING_CHUNKED) {
                http_chunk_append_len(srv, con, len);
        }

        chunkqueue_append_glusterfs_file (con, fd, offset, len, buf_size);

        if ((con->response.transfer_encoding & HTTP_TRANSFER_ENCODING_CHUNKED) 
            && (len > 0)) {
                chunkqueue_append_mem(cq, "\r\n", 2 + 1);
        }

        return 0;
}

int http_chunk_append_glusterfs_mem(server *srv, connection *con,
                                    const char * mem, size_t len,
                                    size_t buf_size) 
{
        chunkqueue *cq = NULL;
        buffer *buf = NULL;
 
        if (!con) return -1;
  
        cq = con->write_queue;
  
        if (len == 0) {
                free (mem);
                if (con->response.transfer_encoding 
                    & HTTP_TRANSFER_ENCODING_CHUNKED) {
                        chunkqueue_append_mem(cq, "0\r\n\r\n", 5 + 1);
                } else {
                        chunkqueue_append_mem(cq, "", 1);
                }
                return 0;
        }

        if (con->response.transfer_encoding & HTTP_TRANSFER_ENCODING_CHUNKED) {
                http_chunk_append_len(srv, con, len - 1);
        }
  
        buf = buffer_init ();

        buf->used = len + 1;
        buf->size = buf_size
        buf->ptr = (char *)mem;
        chunkqueue_append_buffer_weak (cq, buf);

        if (con->response.transfer_encoding & HTTP_TRANSFER_ENCODING_CHUNKED) {
                chunkqueue_append_mem(cq, "\r\n", 2 + 1);
        }

        return 0;
}



URIHANDLER_FUNC(mod_glusterfs_subrequest) {
        plugin_data *p = p_d;
        stat_cache_entry *sce = NULL;
        int s_len;
        char allow_caching = 1;
        size_t size = 0;
        mod_glusterfs_ctx_t *ctx = con->plugin_ctx[p->id];

        /* someone else has done a decision for us */
        if (con->http_status != 0) return HANDLER_GO_ON;
        if (con->uri.path->used == 0) return HANDLER_GO_ON;
        if (con->physical.path->used == 0) return HANDLER_GO_ON;
  
        /* someone else has handled this request */
        if (con->mode != DIRECT) return HANDLER_GO_ON;
  
        /* we only handle GET, POST and HEAD */
        switch(con->request.http_method) {
        case HTTP_METHOD_GET:
        case HTTP_METHOD_POST:
        case HTTP_METHOD_HEAD:
                break;
        default:
                return HANDLER_GO_ON;
        }
  
        mod_glusterfs_patch_connection(srv, con, p);
  
        if (!p->conf.prefix || !p->conf.prefix->used)
                return HANDLER_GO_ON;

        s_len = con->uri.path->used - 1;
        /* ignore certain extensions */
        /*
          for (k = 0; k < p->conf.exclude_exts->used; k++) {
          data_string *ds;
          ds = (data_string *)p->conf.exclude_exts->data[k];
          
          if (ds->value->used == 0) continue;
    
          if (!strncmp (ds->value->ptr, con->uri.path->ptr, strlen (ds->value->ptr)))
          break;
          }
  
          if (k == p->conf.exclude_exts->used) {
          return HANDLER_GO_ON;
          }
        */

        if (con->conf.log_request_handling) {
                log_error_write(srv, __FILE__, __LINE__,  "s",
                                "-- serving file from glusterfs");
        }

        if (HANDLER_ERROR == stat_cache_get_entry(srv, con, con->physical.path,
                                                  &sce)) {
                con->http_status = 403;
                
                log_error_write(srv, __FILE__, __LINE__, "sbsb",
                                "not a regular file:", con->uri.path,
                                "->", con->physical.path);
    
                free (ctx);
                con->plugin_ctx[p->id] = NULL;

                return HANDLER_FINISHED;
        }

        if (con->uri.path->ptr[s_len] == '/' || !S_ISREG(sce->st.st_mode)) {
                free (ctx);
                con->plugin_ctx[p->id] = NULL;
                return HANDLER_FINISHED;
        }

        if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr)
                size = atoi (p->conf.xattr_file_size->ptr);

        if ((size_t)sce->st.st_size > size) {
                ctx->fd = glusterfs_open ((glusterfs_handle_t ) ((unsigned long)p->conf.handle),
                                          ctx->glusterfs_path->ptr, O_RDONLY,
                                          0);
    
                if (((long)ctx->fd) == 0) {
                        con->http_status = 403;
                        free (ctx);
                        con->plugin_ctx[p->id] = NULL;
                        return HANDLER_FINISHED;
                }
        }

        buffer_free (ctx->glusterfs_path);
        ctx->glusterfs_path = NULL;

        /* we only handline regular files */
#ifdef HAVE_LSTAT
        if ((sce->is_symlink == 1) && !con->conf.follow_symlink) {
                con->http_status = 403;
          
                if (con->conf.log_request_handling) {
                        log_error_write(srv, __FILE__, __LINE__,  "s",
                                        "-- access denied due symlink restriction");
                        log_error_write(srv, __FILE__, __LINE__,  "sb",
                                        "Path         :", con->physical.path);
                }
    
                buffer_reset(con->physical.path);
                free (ctx);
                con->plugin_ctx[p->id] = NULL;
                return HANDLER_FINISHED;
        }
#endif
        if (!S_ISREG(sce->st.st_mode)) {
                con->http_status = 404;
    
                if (con->conf.log_file_not_found) {
                        log_error_write(srv, __FILE__, __LINE__, "sbsb",
                                        "not a regular file:", con->uri.path,
                                        "->", sce->name);
                }
    
                free (ctx);
                con->plugin_ctx[p->id] = NULL;

                return HANDLER_FINISHED;
        }

        /* mod_compress might set several data directly, don't overwrite them */

        /* set response content-type, if not set already */
  
        if (NULL == array_get_element(con->response.headers, "Content-Type")) {
                if (buffer_is_empty(sce->content_type)) {
                        /* we are setting application/octet-stream, but also announce that
                         * this header field might change in the seconds few requests 
                         *
                         * This should fix the aggressive caching of FF and the script download
                         * seen by the first installations
                         */
                        response_header_overwrite(srv, con,
                                                  CONST_STR_LEN("Content-Type"),
                                                  CONST_STR_LEN("application/octet-stream"));
      
                        allow_caching = 0;
                } else {
                        response_header_overwrite(srv, con,
                                                  CONST_STR_LEN("Content-Type"),
                                                  CONST_BUF_LEN(sce->content_type));
                }
        }
  
        if (con->conf.range_requests) {
                response_header_overwrite(srv, con,
                                          CONST_STR_LEN("Accept-Ranges"),
                                          CONST_STR_LEN("bytes"));
        }

        /* TODO: Allow Cachable requests */     
#if 0
        if (allow_caching) {
                if (p->conf.etags_used && con->etag_flags != 0 
                    && !buffer_is_empty(sce->etag)) {
                        if (NULL == array_get_element(con->response.headers,
                                                      "ETag")) {
                                /* generate e-tag */
                                etag_mutate(con->physical.etag, sce->etag);
        
                                response_header_overwrite(srv, con,
                                                          CONST_STR_LEN("ETag"),
                                                          CONST_BUF_LEN(con->physical.etag));
                        }
                }
    
                /* prepare header */
                if (NULL == (ds = (data_string *)array_get_element(con->response.headers,
                                                                   "Last-Modified"))) {
                        mtime = strftime_cache_get(srv, sce->st.st_mtime);
                        response_header_overwrite(srv, con, CONST_STR_LEN("Last-Modified"),
                                                  CONST_BUF_LEN(mtime));
                } else {
                        mtime = ds->value;
                }
    
                if (HANDLER_FINISHED == http_response_handle_cachable(srv, con,
                                                                      mtime)) {
                        free (ctx);
                        con->plugin_ctx[p->id] = NULL;
                        return HANDLER_FINISHED;
                }
        }
#endif 
  
        /*TODO: Read about etags */
        if (con->request.http_range && con->conf.range_requests) {
                int do_range_request = 1;
                data_string *ds = NULL;
                buffer *mtime = NULL;
                /* check if we have a conditional GET */
    
                /* prepare header */
                if (NULL == (ds = (data_string *)array_get_element(con->response.headers,
                                                                   "Last-Modified"))) {
                        mtime = strftime_cache_get(srv, sce->st.st_mtime);
                        response_header_overwrite(srv, con, CONST_STR_LEN("Last-Modified"),
                                                  CONST_BUF_LEN(mtime));
                } else {
                        mtime = ds->value;
                }

                if (NULL != (ds = (data_string *)array_get_element(con->request.headers,
                                                                   "If-Range"))) {
                        /* if the value is the same as our ETag, we do a Range-request,
                         * otherwise a full 200 */
            
                        if (ds->value->ptr[0] == '"') {
                                /**
                                 * client wants a ETag
                                 */
                                if (!con->physical.etag) {
                                        do_range_request = 0;
                                } else if (!buffer_is_equal(ds->value,
                                                            con->physical.etag)) {
                                        do_range_request = 0;
                                }
                        } else if (!mtime) {
                                /**
                                 * we don't have a Last-Modified and can match the If-Range: 
                                 *
                                 * sending all
                                 */
                                do_range_request = 0;
                        } else if (!buffer_is_equal(ds->value, mtime)) {
                                do_range_request = 0;
                        }
                }
    
                if (do_range_request) {
                        /* content prepared, I'm done */
                        con->file_finished = 1;
            
                        if (0 == http_response_parse_range(srv, con, p)) {
                                con->http_status = 206;
                        }

                        free (ctx);
                        con->plugin_ctx[p->id] = NULL;
                        return HANDLER_FINISHED;
                }
        }
  
        /* if we are still here, prepare body */

        /* we add it here for all requests
         * the HEAD request will drop it afterwards again
         */
        /*TODO check whether 1 should be subtracted */

        if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr)
                size = atoi (p->conf.xattr_file_size->ptr);

        if (size <= (size_t)sce->st.st_size) {
                http_chunk_append_glusterfs_file_chunk (srv, con, ctx->fd, 0,
                                                        sce->st.st_size, size);
        } else {
                http_chunk_append_glusterfs_mem (srv, con, ctx->buf,
                                                 sce->st.st_size, size);
        }
        
        con->http_status = 200;
        con->file_finished = 1;
  
        free (ctx);
        con->plugin_ctx[p->id] = NULL;

        return HANDLER_FINISHED;
}

#if 0
URIHANDLER_FUNC(mod_glusterfs_request_done)
{
        mod_glusterfs_iobuf_t *cur = first, *prev;
        while (cur) {
                prev =  cur;
                glusterfs_free (cur->buf);
                cur = cur->next;
                free (prev);
        }
        first = NULL
                }
#endif

/* this function is called at dlopen() time and inits the callbacks */
CONNECTION_FUNC(mod_glusterfs_connection_reset) 
{
        (void) p_d;
        (void) con;
        if (!network_backend_write)
                network_backend_write = srv->network_backend_write;

        srv->network_backend_write = mod_glusterfs_network_backend_write;

        return HANDLER_GO_ON;
}

int mod_glusterfs_plugin_init(plugin *p) {
        p->version     = LIGHTTPD_VERSION_ID;
        p->name        = buffer_init_string("glusterfs");
        p->init        = mod_glusterfs_init;
        p->handle_physical = mod_glusterfs_handle_physical;
        p->handle_subrequest_start = mod_glusterfs_subrequest;
        //      p->handle_request_done = mod_glusterfs_request_done;
        p->set_defaults  = mod_glusterfs_set_defaults;
        p->connection_reset = mod_glusterfs_connection_reset;
        p->cleanup     = mod_glusterfs_free;
  
        p->data        = NULL;
  
        return 0;
}


/* mod_glusterfs_stat_cache */
static stat_cache_entry * stat_cache_entry_init(void) {
        stat_cache_entry *sce = NULL;
  
        sce = calloc(1, sizeof(*sce));
        /* ERR_ABORT (sce); */
  
        sce->name = buffer_init();
        sce->etag = buffer_init();
        sce->content_type = buffer_init();

        return sce;
}

#ifdef HAVE_FAM_H
static fam_dir_entry * fam_dir_entry_init(void) {
        fam_dir_entry *fam_dir = NULL;
  
        fam_dir = calloc(1, sizeof(*fam_dir));
        /* ERR_ABORT (fam_dir); */
  
        fam_dir->name = buffer_init();
  
        return fam_dir;
}

static void fam_dir_entry_free(void *data) {
        fam_dir_entry *fam_dir = data;

        if (!fam_dir) return;

        FAMCancelMonitor(fam_dir->fc, fam_dir->req);

        buffer_free(fam_dir->name);
        free(fam_dir->req);

        free(fam_dir);
}
#endif

#ifdef HAVE_XATTR
static int stat_cache_attr_get(buffer *buf, char *name) {
        int attrlen;
        int ret;
  
        attrlen = 1024;
        buffer_prepare_copy(buf, attrlen);
        attrlen--;
        if(0 == (ret = attr_get(name, "Content-Type", buf->ptr, &attrlen, 0))) {
                buf->used = attrlen + 1;
                buf->ptr[attrlen] = '\0';
        }
        return ret;
}
#endif

/* the famous DJB hash function for strings */
static uint32_t hashme(buffer *str) {
        uint32_t hash = 5381;
        const char *s;
        for (s = str->ptr; *s; s++) {
                hash = ((hash << 5) + hash) + *s;
        }
  
        hash &= ~(1 << 31); /* strip the highest bit */
  
        return hash;
}


#ifdef HAVE_LSTAT
static int stat_cache_lstat(server *srv, buffer *dname, struct stat *lst) {
        if (lstat(dname->ptr, lst) == 0) {
                return S_ISLNK(lst->st_mode) ? 0 : 1;
        }
        else {
                log_error_write(srv, __FILE__, __LINE__, "sbs",
                                "lstat failed for:",
                                dname, strerror(errno));
        };
        return -1;
}
#endif

/***
 *
 *
 *
 * returns:
 *  - HANDLER_FINISHED on cache-miss (don't forget to reopen the file)
 *  - HANDLER_ERROR on stat() failed -> see errno for problem
 */

handler_t glusterfs_stat_cache_get_entry(server *srv, 
                                         connection *con, 
                                         glusterfs_handle_t handle, 
                                         buffer *glusterfs_path,
                                         buffer *name, 
                                         void *buf, 
                                         size_t size, 
                                         stat_cache_entry **ret_sce) 
{
#ifdef HAVE_FAM_H
        fam_dir_entry *fam_dir = NULL;
        int dir_ndx = -1;
        splay_tree *dir_node = NULL;
#endif
        stat_cache_entry *sce = NULL;
        stat_cache *sc;
        struct stat st; 
        size_t k;
#ifdef DEBUG_STAT_CACHE
        size_t i;
#endif
        int file_ndx;
        splay_tree *file_node = NULL;

        *ret_sce = NULL;
        memset (&st, 0, sizeof (st));

        /*
         * check if the directory for this file has changed
         */

        sc = srv->stat_cache;

        buffer_copy_string_buffer(sc->hash_key, name);
        buffer_append_long(sc->hash_key, con->conf.follow_symlink);

        file_ndx = hashme(sc->hash_key);
        sc->files = splaytree_splay(sc->files, file_ndx);

#ifdef DEBUG_STAT_CACHE
        for (i = 0; i < ctrl.used; i++) {
                if (ctrl.ptr[i] == file_ndx) break;
        }
#endif

        if (sc->files && (sc->files->key == file_ndx)) {
#ifdef DEBUG_STAT_CACHE
                /* it was in the cache */
                assert(i < ctrl.used);
#endif

                /* we have seen this file already and
                 * don't stat() it again in the same second */

                file_node = sc->files;

                sce = file_node->data;

                /* check if the name is the same, we might have a collision */

                if (buffer_is_equal(name, sce->name)) {
                        if (srv->srvconf.stat_cache_engine 
                            == STAT_CACHE_ENGINE_SIMPLE) {
                                if (sce->stat_ts == srv->cur_ts && !buf) {
                                        *ret_sce = sce;
                                        return HANDLER_GO_ON;
                                }
                        }
                } else {
                        /* oops, a collision,
                         *
                         * file_node is used by the FAM check below to see if we know this file
                         * and if we can save a stat().
                         *
                         * BUT, the sce is not reset here as the entry into the cache is ok, we
                         * it is just not pointing to our requested file.
                         *
                         *  */

                        file_node = NULL;
                }
        } else {
#ifdef DEBUG_STAT_CACHE
                if (i != ctrl.used) {
                        fprintf(stderr, "%s.%d: %08x was already inserted but not found in cache, %s\n",
                                __FILE__, __LINE__, file_ndx, name->ptr);
                }
                assert(i == ctrl.used);
#endif
        }
        /*
         * *lol*
         * - open() + fstat() on a named-pipe results in a (intended) hang.
         * - stat() if regular file + open() to see if we can read from it is better
         *
         * */
        if (-1 == glusterfs_get (handle, glusterfs_path->ptr, buf, size, &st)) {
                return HANDLER_ERROR;
        }

        if (NULL == sce) {
                int osize = 0;

                if (sc->files) {
                        osize = sc->files->size;
                }

                sce = stat_cache_entry_init();
                buffer_copy_string_buffer(sce->name, name);

                sc->files = splaytree_insert(sc->files, file_ndx, sce);
#ifdef DEBUG_STAT_CACHE
                if (ctrl.size == 0) {
                        ctrl.size = 16;
                        ctrl.used = 0;
                        ctrl.ptr = malloc(ctrl.size * sizeof(*ctrl.ptr));
                        /* ERR_ABORT (ctrl.ptr); */
                } else if (ctrl.size == ctrl.used) {
                        ctrl.size += 16;
                        ctrl.ptr = realloc(ctrl.ptr, ctrl.size * sizeof(*ctrl.ptr));
                        /* ERR_ABORT (ctrl.ptr); */
                }

                ctrl.ptr[ctrl.used++] = file_ndx;

                assert(sc->files);
                assert(sc->files->data == sce);
                assert(osize + 1 == splaytree_size(sc->files));
#endif
        }

        sce->st = st;
        sce->stat_ts = srv->cur_ts;

        /* catch the obvious symlinks
         *
         * this is not a secure check as we still have a race-condition between
         * the stat() and the open. We can only solve this by
         * 1. open() the file
         * 2. fstat() the fd
         *
         * and keeping the file open for the rest of the time. But this can
         * only be done at network level.
         *
         * per default it is not a symlink
         * */
#ifdef HAVE_LSTAT
        sce->is_symlink = 0;

        /* we want to only check for symlinks if we should block symlinks.
         */
        if (!con->conf.follow_symlink) {
                if (stat_cache_lstat(srv, name, &lst)  == 0) {
#ifdef DEBUG_STAT_CACHE
                        log_error_write(srv, __FILE__, __LINE__, "sb",
                                        "found symlink", name);
#endif
                        sce->is_symlink = 1;
                }

                /*
                 * we assume "/" can not be symlink, so
                 * skip the symlink stuff if our path is /
                 **/
                else if ((name->used > 2)) {
                        buffer *dname;
                        char *s_cur;

                        dname = buffer_init();
                        buffer_copy_string_buffer(dname, name);

                        while ((s_cur = strrchr(dname->ptr,'/'))) {
                                *s_cur = '\0';
                                dname->used = s_cur - dname->ptr + 1;
                                if (dname->ptr == s_cur) {
#ifdef DEBUG_STAT_CACHE
                                        log_error_write(srv, __FILE__, __LINE__,
                                                        "s", "reached /");
#endif
                                        break;
                                }
#ifdef DEBUG_STAT_CACHE
                                log_error_write(srv, __FILE__, __LINE__, "sbs",
                                                "checking if", dname, "is a symlink");
#endif
                                if (stat_cache_lstat(srv, dname, &lst)  == 0) {
                                        sce->is_symlink = 1;
#ifdef DEBUG_STAT_CACHE
                                        log_error_write(srv, __FILE__, __LINE__,
                                                        "sb",
                                                        "found symlink", dname);
#endif
                                        break;
                                };
                        };
                        buffer_free(dname);
                };
        };
#endif

        if (S_ISREG(st.st_mode)) {
                /* determine mimetype */
                buffer_reset(sce->content_type);

                for (k = 0; k < con->conf.mimetypes->used; k++) {
                        data_string *ds = (data_string *)con->conf.mimetypes->data[k];
                        buffer *type = ds->key;

                        if (type->used == 0) continue;

                        /* check if the right side is the same */
                        if (type->used > name->used) continue;

                        if (0 == strncasecmp(name->ptr + name->used - type->used,
                                             type->ptr, type->used - 1)) {
                                buffer_copy_string_buffer(sce->content_type,
                                                          ds->value);
                                break;
                        }
                }
                etag_create(sce->etag, &(sce->st), con->etag_flags);
#ifdef HAVE_XATTR
                if (con->conf.use_xattr && buffer_is_empty(sce->content_type)) {
                        stat_cache_attr_get(sce->content_type, name->ptr);
                }
#endif
        } else if (S_ISDIR(st.st_mode)) {
                etag_create(sce->etag, &(sce->st), con->etag_flags);
        }

#ifdef HAVE_FAM_H
        if (sc->fam &&
            (srv->srvconf.stat_cache_engine == STAT_CACHE_ENGINE_FAM)) {
                /* is this directory already registered ? */
                if (!dir_node) {
                        fam_dir = fam_dir_entry_init();
                        fam_dir->fc = sc->fam;

                        buffer_copy_string_buffer(fam_dir->name, sc->dir_name);

                        fam_dir->version = 1;

                        fam_dir->req = calloc(1, sizeof(FAMRequest));
                        /* ERR_ABORT (fam_dir->req); */

                        if (0 != FAMMonitorDirectory(sc->fam, fam_dir->name->ptr,
                                                     fam_dir->req, fam_dir)) {

                                log_error_write(srv, __FILE__, __LINE__, "sbsbs",
                                                "monitoring dir failed:",
                                                fam_dir->name, 
                                                "file:", name,
                                                FamErrlist[FAMErrno]);

                                fam_dir_entry_free(fam_dir);
                        } else {
                                int osize = 0;

                                if (sc->dirs) {
                                        osize = sc->dirs->size;
                                }

                                sc->dirs = splaytree_insert(sc->dirs, dir_ndx,
                                                            fam_dir);
                                assert(sc->dirs);
                                assert(sc->dirs->data == fam_dir);
                                assert(osize == (sc->dirs->size - 1));
                        }
                } else {
                        fam_dir = dir_node->data;
                }

                /* bind the fam_fc to the stat() cache entry */

                if (fam_dir) {
                        sce->dir_version = fam_dir->version;
                        sce->dir_ndx     = dir_ndx;
                }
        }
#endif

        *ret_sce = sce;

        return HANDLER_GO_ON;
}

/**
 * remove stat() from cache which havn't been stat()ed for
 * more than 10 seconds
 *
 *
 * walk though the stat-cache, collect the ids which are too old
 * and remove them in a second loop
 */

static int stat_cache_tag_old_entries(server *srv, splay_tree *t, int *keys,
                                      size_t *ndx) {
        stat_cache_entry *sce;

        if (!t) return 0;

        stat_cache_tag_old_entries(srv, t->left, keys, ndx);
        stat_cache_tag_old_entries(srv, t->right, keys, ndx);

        sce = t->data;

        if (srv->cur_ts - sce->stat_ts > 2) {
                keys[(*ndx)++] = t->key;
        }

        return 0;
}
