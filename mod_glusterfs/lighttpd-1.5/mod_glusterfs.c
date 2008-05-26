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
#include "response.h"

#include "fdevent.h"
#include "joblist.h"
#include "http_req_range.h"
#include "connections.h"
#include "configfile.h"

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


/* plugin config for all request/connections */

typedef struct {
  buffer *logfile;
  buffer *loglevel;
  buffer *specfile;
  buffer *prefix;
  buffer *xattr_file_size;
  array *exclude_exts;
  unsigned short cache_timeout;

  /* FIXME: its a pointer, hence cant be short */
  unsigned long handle;
} plugin_config;

static network_status_t (*network_backend_write)(struct server *srv, connection *con, iosocket *sock, chunkqueue *cq);

typedef struct {
  PLUGIN_DATA;
  buffer *range_buf;
  plugin_config **config_storage;
  http_req_range *ranges;
  plugin_config conf;
} plugin_data;

typedef struct glusterfs_async_local {
  int op_ret;
  int op_errno;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  connection *con;
  server *srv;
  plugin_data *p;

  union {
    struct {
      char async_read_complete;
      off_t length;
      size_t read_bytes;
      glusterfs_read_buf_t *buf;
    }readv;

    struct {
      buffer *name;
      buffer *hash_key;
      size_t size;
    }lookup;
  }fop;
} glusterfs_async_local_t;

typedef struct {
  unsigned long fd;
  void *buf;
  off_t response_content_length;
  int prefix;
}mod_glusterfs_ctx_t;

typedef struct {
  chunkqueue *cq;
  glusterfs_read_buf_t *buf;
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

static stat_cache_entry * 
stat_cache_entry_init(void) 
{
  stat_cache_entry *sce = NULL;

  sce = calloc(1, sizeof(*sce));
  ERR_ABORT (sce);

  sce->name = buffer_init();
  sce->etag = buffer_init();
  sce->content_type = buffer_init();

  return sce;
}

int chunkqueue_append_glusterfs_mem (chunkqueue *cq, const char * mem, size_t len) {
  buffer *buf = NULL;
 
  buf = chunkqueue_get_append_buffer (cq);

  if (buf->ptr)
    FREE (buf->ptr);

  buf->used = len + 1;
  buf->ptr = (char *)mem;
  buf->size = len;

  return 0;
}

static int
glusterfs_lookup_async_cbk (int op_ret, 
			    int op_errno,
			    void *buf,
			    struct stat *st,
			    void *cbk_data)
{
  glusterfs_async_local_t *local = cbk_data;

  mod_glusterfs_ctx_t *ctx = NULL;
  ctx = local->con->plugin_ctx[local->p->id];

  assert (ctx->buf== buf);

  if (op_ret || !(S_ISREG (st->st_mode) && (size_t)st->st_size <= local->fop.lookup.size)) {

    FREE (ctx->buf);
    ctx->buf = NULL;

    if (op_ret) {
      if (op_errno == ENOENT)
	local->con->http_status = 404;
      else 
	local->con->http_status = 403;
    }
  }

  if (!op_ret) {
    stat_cache_entry *sce = NULL;
    stat_cache *sc = local->srv->stat_cache;

    sce = (stat_cache_entry *)g_hash_table_lookup(sc->files, local->fop.lookup.hash_key);

    if (!sce) {
      sce = stat_cache_entry_init();

      buffer_copy_string_buffer(sce->name, local->fop.lookup.name);
      g_hash_table_insert(sc->files, buffer_init_string(BUF_STR(local->fop.lookup.hash_key)), sce);
    }

    sce->state = STAT_CACHE_ENTRY_STAT_FINISHED;
    sce->stat_ts = time (NULL);
    memcpy (&sce->st, st, sizeof (*st));
  }

  g_async_queue_push (local->srv->joblist_queue, local->con);
  /*
    joblist_append (local->srv, local->con);
    kill (getpid(), SIGUSR1);
  */
  FREE (local);
  return 0;
}

static handler_t
glusterfs_stat_cache_get_entry_async (server *srv, 
				      connection *con, 
				      plugin_data *p,
				      int prefix,
				      buffer *name,
				      void *buf,
				      size_t size,
				      stat_cache_entry **ret_sce)
{
  stat_cache_entry *sce = NULL;
  stat_cache *sc;
  glusterfs_async_local_t *local = NULL;

  *ret_sce = NULL;

  /*
   * check if the directory for this file has changed
   */

  sc = srv->stat_cache;

  buffer_copy_string_buffer(sc->hash_key, name);
  buffer_append_long(sc->hash_key, con->conf.follow_symlink);

  if ((sce = (stat_cache_entry *)g_hash_table_lookup(sc->files, sc->hash_key))) {
    /* know this entry already */

    if (sce->state == STAT_CACHE_ENTRY_STAT_FINISHED && 
	!buf) {
      /* verify that this entry is still fresh */

      *ret_sce = sce;

      return HANDLER_GO_ON;
    }
  }


  /*
   * *lol*
   * - open() + fstat() on a named-pipe results in a (intended) hang.
   * - stat() if regular file + open() to see if we can read from it is better
   *
   * */

  /* pass a job to the stat-queue */

  local = calloc (1, sizeof (*local));
  ERR_ABORT (local);
  local->con = con;
  local->srv = srv;
  local->p = p;
  local->fop.lookup.name = buffer_init_buffer (name);
  local->fop.lookup.hash_key = buffer_init_buffer (sc->hash_key);
  local->fop.lookup.size = size;

  if (glusterfs_lookup_async ((libglusterfs_handle_t )p->conf.handle, name->ptr + prefix, buf, size, glusterfs_lookup_async_cbk, (void *) local)) {
    FREE (local);
    return HANDLER_ERROR;
  }

  return HANDLER_WAIT_FOR_EVENT;
}

int 
mod_glusterfs_readv_async_cbk (glusterfs_read_buf_t *buf,
			       void *cbk_data)
{
  glusterfs_async_local_t *local = cbk_data;
  pthread_mutex_lock (&local->lock);
  {
    local->fop.readv.async_read_complete = 1;
    local->fop.readv.buf = buf;

    pthread_cond_signal (&local->cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

network_status_t 
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
  unsigned long fd = (unsigned long)glusterfs_chunk->file.name;
  network_status_t ret;

  pthread_cond_init (&local.cond, NULL);
  pthread_mutex_init (&local.lock, NULL);
  
  //local.fd = fd;
  memset (&local, 0, sizeof (local));

  if (length > 0)
    end = offset + length;

  cq = chunkqueue_init ();
  if (!cq) {
    con->http_status = 500;
    return NETWORK_STATUS_FATAL_ERROR;
  }

  do {
    glusterfs_read_buf_t *buf;
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
      while (!local.fop.readv.async_read_complete) {
      	pthread_cond_wait (&local.cond, &local.lock);
      }

      local.op_ret = local.fop.readv.buf->op_ret;
      local.op_errno = local.fop.readv.buf->op_errno;

      local.fop.readv.async_read_complete = 0;
      buf = local.fop.readv.buf;

      if ((int)length < 0)
	complete = (local.fop.readv.buf->op_ret <= 0);
      else {
	local.fop.readv.read_bytes += local.fop.readv.buf->op_ret;
	complete = ((local.fop.readv.read_bytes == length) || (local.fop.readv.buf->op_ret <= 0));
      }
    }
    pthread_mutex_unlock (&local.lock);

    if (local.op_ret > 0) {
      for (i = 0; i < buf->count; i++) {
	buffer *nw_write_buf = chunkqueue_get_append_buffer (cq);

	nw_write_buf->used = nw_write_buf->size = buf->vector[i].iov_len + 1;
	nw_write_buf->ptr = buf->vector[i].iov_base;

	//	buffer_copy_memory (nw_write_buf, buf->vector[i].iov_base, buf->vector[i].iov_len + 1);
	offset += local.op_ret;
      }
  
      ret = network_backend_write (srv, con, con->sock, cq);
  
      if (chunkqueue_written (cq) != local.op_ret) {
	mod_glusterfs_chunkqueue *gf_cq;
	glusterfs_chunk->file.start = offset;
	if ((int)glusterfs_chunk->file.length > 0)
	  glusterfs_chunk->file.length -= local.fop.readv.read_bytes;

	gf_cq = calloc (1, sizeof (*gf_cq));
	ERR_ABORT (qf_cq);
	gf_cq->cq = cq;
	gf_cq->buf = buf;
	gf_cq->length = local.op_ret;
	glusterfs_chunk->file.mmap.start = (char *)gf_cq;
	return ret;
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

  return (local.op_ret < 0 ? NETWORK_STATUS_FATAL_ERROR : NETWORK_STATUS_SUCCESS);
}

network_status_t mod_glusterfs_network_backend_write(struct server *srv, connection *con, iosocket *sock, chunkqueue *cq)
{
  chunk *c, *prev, *first;
  int chunks_written = 0;
  int error = 0;
  network_status_t ret;

  for (first = prev = c = cq->first; c; c = c->next, chunks_written++) {

    if (c->type == MEM_CHUNK && c->mem->used && !c->mem->ptr) {
      if (cq->first != c) {
	prev->next = NULL;

	/* call stored network_backend_write */
	ret = network_backend_write (srv, con, sock, cq);

	prev->next = c;
	if (ret != NETWORK_STATUS_SUCCESS) {
	  cq->first = first;
	  return ret;
	}
      } 
      cq->first = c->next;

      if (c->file.fd < 0) {
	error = HANDLER_ERROR;
	break;
      }

      if (c->file.mmap.start) {
	chunk *tmp;
	size_t len;
	mod_glusterfs_chunkqueue *gf_cq = (mod_glusterfs_chunkqueue *)c->file.mmap.start;

	ret = network_backend_write (srv, con, sock, gf_cq->cq);

	if ((len = (size_t)chunkqueue_written (gf_cq->cq)) != gf_cq->length) {
	  gf_cq->length -= len;
	  cq->first = first;
	  chunkqueue_remove_finished_chunks (gf_cq->cq);
	  return ret;
	}

	for (tmp = gf_cq->cq->first ; tmp; tmp = tmp->next)
	  tmp->mem->ptr = NULL;

	chunkqueue_free (gf_cq->cq);
	glusterfs_free (gf_cq->buf);
	FREE (gf_cq);
	c->file.mmap.start = NULL;
      }
      
      ret = mod_glusterfs_read_async (srv, con, c); //c->file.fd, c->file.start, -1);//c->file.length);
      if (c->file.mmap.start) {
	/* pending chunkqueue from mod_glusterfs_read_async to be written to network */
	cq->first = first;
	return ret;
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

      FREE(c);
    }     
    prev = c;
  }

  ret = network_backend_write (srv, con, sock, cq);

  cq->first = first;

  return ret;
}

#if 0
int chunkqueue_append_glusterfs_file (chunkqueue *cq, unsigned long fd, off_t offset, off_t len)
{
  chunk *c = NULL;
  c = chunkqueue_get_append_tempfile (cq);
  
  if (c->file.is_temp) {
    close (c->file.fd);
    unlink (c->file.name->ptr);
  }

  c->type = MEM_CHUNK;

  c->mem = buffer_init ();
  c->mem->used = len + 1;
  c->mem->ptr = NULL;
  c->offset = 0;

  /*  buffer_copy_string_buffer (c->file.name, fn); */
  c->file.start = offset;
  c->file.length = len;
  /*  buffer_free (c->file.name); */

  /* identify chunk as glusterfs related */
  c->file.mmap.start = MAP_FAILED;
  /*  c->file.mmap.length = c->file.mmap.offset = len;*/

  return 0;
}
#endif

int chunkqueue_append_dummy_mem_chunk (chunkqueue *cq, off_t len)
{
  chunk *c = NULL;
  c = chunkqueue_get_append_tempfile (cq);
  
  if (c->file.is_temp) {
    close (c->file.fd);
    unlink (c->file.name->ptr);
    c->file.is_temp = 0;
  }

  c->type = MEM_CHUNK;

  c->mem->used = len + 1;
  c->offset = len;
  c->mem->ptr = NULL;

  return 0;
}

int chunkqueue_append_glusterfs_file (chunkqueue *cq, unsigned long fd, off_t offset, off_t len)
{
  chunk *c = NULL;
  c = chunkqueue_get_append_tempfile (cq);
  
  if (c->file.is_temp) {
    close (c->file.fd);
    unlink (c->file.name->ptr);
    c->file.is_temp = 0;
  }

  c->type = MEM_CHUNK;

  c->mem = buffer_init ();
  c->mem->used = len + 1;
  c->mem->ptr = NULL;
  c->offset = 0;

  /*  buffer_copy_string_buffer (c->file.name, fn); */
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

  UNUSED (srv);
  p = calloc(1, sizeof(*p));
  ERR_ABORT (p);
  network_backend_write = NULL;
  p->ranges = http_request_range_init();
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
      array_free (s->exclude_exts);
  
      FREE (s);
    }
    FREE (p->config_storage);
  }
  buffer_free (p->range_buf);
  http_request_range_free (p->ranges);

  FREE (p);
  
  return HANDLER_GO_ON;
}

SETDEFAULTS_FUNC(mod_glusterfs_set_defaults) {
  plugin_data *p = p_d;
  size_t i = 0;
  
  config_values_t cv[] = {
    { "glusterfs.logfile",              NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },
    
    { "glusterfs.loglevel",             NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },		
    { "glusterfs.volume-specfile",             NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },		
    { "glusterfs.cache-timeout",              NULL, T_CONFIG_SHORT, T_CONFIG_SCOPE_CONNECTION },
    
    { "glusterfs.exclude-extensions",   NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },
    
    /*TODO: get the prefix from config_conext and remove glusterfs.prefix from conf file */
    { "glusterfs.prefix", NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },
    
    { "glusterfs.xattr-interface-size-limit", NULL, T_CONFIG_STRING, T_CONFIG_SCOPE_CONNECTION },
    
    { NULL,                          NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
  };
  
  p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));
  ERR_ABORT (p->config_storage);
  p->range_buf = buffer_init ();
  
  for (i = 0; i < srv->config_context->used; i++) {
    plugin_config *s;

    s = calloc(1, sizeof(plugin_config));
    ERR_ABORT (s);
    s->logfile = buffer_init ();
    s->loglevel = buffer_init ();
    s->specfile = buffer_init ();
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
    p->config_storage[i] = s;
    
    if (0 != config_insert_values_global(srv, ((data_config *)srv->config_context->data[i])->value, cv)) {
      return HANDLER_FINISHED;
    }
  }
  
  return HANDLER_GO_ON;
}

#define PATCH(x) \
	p->conf.x = s->x;

static int mod_glusterfs_patch_connection(server *srv, connection *con, plugin_data *p) {
  size_t i, j;
  plugin_config *s;

  /* skip the first, the global context */
  /* glusterfs related config can only occur inside $HTTP["url"] == "<glusterfs-prefix>" */
  for (i = 1; i < srv->config_context->used; i++) {
    data_config *dc = (data_config *)srv->config_context->data[i];
    s = p->config_storage[i];

    /* condition didn't match */
    if (!config_check_cond(srv, con, dc)) continue;
    
    /* merge config */
    for (j = 0; j < dc->value->used; j++) {
      data_unset *du = dc->value->data[j];
      
      if (buffer_is_equal_string (du->key, CONST_STR_LEN("glusterfs.logfile"))) {
	PATCH (logfile);
      } else if (buffer_is_equal_string (du->key, CONST_STR_LEN("glusterfs.loglevel"))) {
	PATCH (loglevel);
      } else if (buffer_is_equal_string (du->key, CONST_STR_LEN ("glusterfs.volume-specfile"))) {
	PATCH (specfile);
      } else if (buffer_is_equal_string (du->key, CONST_STR_LEN("glusterfs.cache-timeout"))) {
	PATCH (cache_timeout);
      } else if (buffer_is_equal_string (du->key, CONST_STR_LEN ("glusterfs.exclude-extensions"))) {
	PATCH (exclude_exts);
      } else if (buffer_is_equal_string (du->key, CONST_STR_LEN ("glusterfs.prefix"))) {
	PATCH (prefix);
      } else if (buffer_is_equal_string (du->key, CONST_STR_LEN ("glusterfs.xattr-interface-size-limit"))) {
	PATCH (xattr_file_size);
      }
    }
  }
  return 0;
}

#undef PATCH

static int http_response_parse_range(server *srv, connection *con, plugin_data *p) {
  int multipart = 0;
  char *boundary = "fkj49sn38dcn3";
  data_string *ds;
  stat_cache_entry *sce = NULL;
  buffer *content_type = NULL;
  buffer *range = NULL;
  http_req_range *ranges, *r;
  mod_glusterfs_ctx_t *ctx = con->plugin_ctx[p->id];
  size_t size = 0;

  if (!ctx) {
    return -1;
  }

  if (NULL != (ds = (data_string *)array_get_element(con->request.headers, CONST_STR_LEN("Range")))) {
    range = ds->value;
  } else {
    /* we don't have a Range header */

    return -1;
  }

  if (HANDLER_ERROR == stat_cache_get_entry(srv, con, con->physical.path, &sce)) {
    SEGFAULT();
  }

  ctx->response_content_length = con->response.content_length = 0;

  if (NULL != (ds = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Content-Type")))) {
    content_type = ds->value;
  }

  /* start the range-header parser
   * bytes=<num>  */

  ranges = p->ranges;
  http_request_range_reset(ranges);
  switch (http_request_range_parse(range, ranges)) {
  case PARSE_ERROR:
    return -1; /* no range valid Range Header */
  case PARSE_SUCCESS:
    break;
  default:
    TRACE("%s", "foobar");
    return -1;
  }

  if (ranges->next) {
    multipart = 1;
  }

  if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr) {
    size = atoi (p->conf.xattr_file_size->ptr);
  }

  /* patch the '-1' */
  for (r = ranges; r; r = r->next) {
    if (r->start == -1) {
      /* -<end>
       *
       * the last <end> bytes  */
      r->start = sce->st.st_size - r->end;
      r->end = sce->st.st_size - 1;
    }
    if (r->end == -1) {
      /* <start>-
       * all but the first <start> bytes */

      r->end = sce->st.st_size - 1;
    }

    if (r->end > sce->st.st_size - 1) {
      /* RFC 2616 - 14.35.1
       *
       * if last-byte-pos not present or > size-of-file
       * take the size-of-file
       *
       *  */
      r->end = sce->st.st_size - 1;
    }

    if (r->start > sce->st.st_size - 1) {
      /* RFC 2616 - 14.35.1
       *
       * if first-byte-pos > file-size, 416
       */

      con->http_status = 416;
      return -1;
    }

    if (r->start > r->end) {
      /* RFC 2616 - 14.35.1
       *
       * if last-byte-pos is present, it has to be >= first-byte-pos
       *
       * invalid ranges have to be handle as no Range specified
       *  */

      return -1;
    }
  }

  if (r) {
    /* we ran into an range violation */
    return -1;
  }

  if (multipart) {
    buffer *b;
    for (r = ranges; r; r = r->next) {
      /* write boundary-header */

      b = chunkqueue_get_append_buffer(con->send);

      buffer_copy_string(b, "\r\n--");
      buffer_append_string(b, boundary);

      /* write Content-Range */
      buffer_append_string(b, "\r\nContent-Range: bytes ");
      buffer_append_off_t(b, r->start);
      buffer_append_string(b, "-");
      buffer_append_off_t(b, r->end);
      buffer_append_string(b, "/");
      buffer_append_off_t(b, sce->st.st_size);

      buffer_append_string(b, "\r\nContent-Type: ");
      buffer_append_string_buffer(b, content_type);

      /* write END-OF-HEADER */
      buffer_append_string(b, "\r\n\r\n");

      con->response.content_length += b->used - 1;
      ctx->response_content_length += b->used - 1;
      con->send->bytes_in += b->used - 1;

      if ((size_t)sce->st.st_size > size) {
	chunkqueue_append_glusterfs_file(con->send_raw, ctx->fd, r->start, r->end - r->start + 1);
	con->send_raw->bytes_in += (r->end - r->start + 1);
	chunkqueue_append_dummy_mem_chunk (con->send, r->end - r->start + 1);
      } else {
	chunkqueue_append_mem (con->send, ((char *)ctx->buf) + r->start, r->end - r->start + 1); 
	FREE (ctx->buf);
	ctx->buf = NULL;
      }
		
      con->response.content_length += r->end - r->start + 1;
      ctx->response_content_length += r->end - r->start + 1;
      con->send->bytes_in += r->end - r->start + 1;
    }

    /* add boundary end */
    b = chunkqueue_get_append_buffer(con->send);

    buffer_copy_string_len(b, "\r\n--", 4);
    buffer_append_string(b, boundary);
    buffer_append_string_len(b, "--\r\n", 4);

    con->response.content_length += b->used - 1;
    ctx->response_content_length += b->used - 1;
    con->send->bytes_in += b->used - 1;

    /* set header-fields */

    buffer_copy_string(p->range_buf, "multipart/byteranges; boundary=");
    buffer_append_string(p->range_buf, boundary);

    /* overwrite content-type */
    response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"), CONST_BUF_LEN(p->range_buf));

  } else {
    r = ranges;

    chunkqueue_append_glusterfs_file(con->send_raw, ctx->fd, r->start, r->end - r->start + 1);
    con->send_raw->bytes_in += (r->end - r->start + 1);
    chunkqueue_append_dummy_mem_chunk (con->send, r->end - r->start + 1);
    con->response.content_length += r->end - r->start + 1;
    ctx->response_content_length += r->end - r->start + 1;
    con->send->bytes_in += r->end - r->start + 1;

    buffer_copy_string(p->range_buf, "bytes ");
    buffer_append_off_t(p->range_buf, r->start);
    buffer_append_string(p->range_buf, "-");
    buffer_append_off_t(p->range_buf, r->end);
    buffer_append_string(p->range_buf, "/");
    buffer_append_off_t(p->range_buf, sce->st.st_size);

    response_header_insert(srv, con, CONST_STR_LEN("Content-Range"), CONST_BUF_LEN(p->range_buf));
  }

  /* ok, the file is set-up */
  return 0;
}

PHYSICALPATH_FUNC(mod_glusterfs_handle_physical) {
  plugin_data *p = p_d;
  stat_cache_entry *sce;
  size_t size = 0;
  handler_t ret = 0;
  mod_glusterfs_ctx_t *plugin_ctx = NULL;

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

  if (!p->conf.prefix || !p->conf.prefix->ptr) {
    return HANDLER_GO_ON;
  }

  if (p->conf.handle <= 0) {
    glusterfs_init_ctx_t ctx;

    if (!p->conf.specfile || p->conf.specfile->used == 0) {
      return HANDLER_GO_ON;
    }
    memset (&ctx, 0, sizeof (ctx));

    ctx.specfile = p->conf.specfile->ptr;
    ctx.logfile = p->conf.logfile->ptr;
    ctx.loglevel = p->conf.loglevel->ptr;
    ctx.lookup_timeout = ctx.stat_timeout = p->conf.cache_timeout;

    p->conf.handle = (unsigned long)glusterfs_init (&ctx);

    if (p->conf.handle <= 0) {
      con->http_status = 500;
      return HANDLER_FINISHED;
    }
  }

  size = 0;
  if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr) 
    size = atoi (p->conf.xattr_file_size->ptr);

  if (!con->plugin_ctx[p->id]) {
    buffer *tmp_buf = buffer_init_buffer (con->physical.basedir);

    plugin_ctx = calloc (1, sizeof (*plugin_ctx));
    ERR_ABORT (plugin_ctx);
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
      ERR_ABORT (plugin_ctx->buf);
    }

  ret = glusterfs_stat_cache_get_entry_async (srv, con, p, plugin_ctx->prefix, con->physical.path, plugin_ctx->buf, size, &sce);

  if (ret == HANDLER_ERROR) {
    FREE (plugin_ctx->buf);
    plugin_ctx->buf = NULL;

    con->http_status = 500;
    ret = HANDLER_FINISHED;
  }

  return ret;
}

/* set con->response.content_length, which was reset to 0 earlier by mod_chunked since we append glusterfs chunks directly to con->send_raw instead of con->send*/

URIHANDLER_FUNC(mod_glusterfs_response_header) {
  plugin_data *p = p_d;
  mod_glusterfs_ctx_t *ctx = con->plugin_ctx[p->id];

  mod_glusterfs_patch_connection (srv, con, p);
  if (!p->conf.prefix || !p->conf.prefix->ptr)
    return HANDLER_GO_ON;

  if (p->conf.prefix->used == 0 ) {
    if (p->conf.handle <= 0) {
      con->http_status = 500;
      return HANDLER_FINISHED;
    }
    else
      return HANDLER_GO_ON;
  }
  con->response.content_length = ctx->response_content_length;
  return HANDLER_GO_ON;
}

URIHANDLER_FUNC(mod_glusterfs_subrequest) {
  plugin_data *p = p_d;
  stat_cache_entry *sce = NULL;
  int s_len;
  unsigned long fd;
  char allow_caching = 1;
  size_t size = 0;
  char *path;
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
  
  if (!p->conf.prefix || !p->conf.prefix->ptr)
    return HANDLER_GO_ON;

  if (!ctx) {
    con->http_status = 500;
    return HANDLER_FINISHED;
  }

  if (p->conf.prefix->used == 0 ) {
    if (p->conf.handle <= 0) {
      con->http_status = 500;
      return HANDLER_FINISHED;
    }
    else
      return HANDLER_GO_ON;
  }

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
    log_error_write(srv, __FILE__, __LINE__,  "s",  "-- serving file from glusterfs");
  }

  if (HANDLER_ERROR == stat_cache_get_entry(srv, con, con->physical.path, &sce)) {
    con->http_status = 403;

    /* this might happen if the sce is removed from stat-cache after a successful glusterfs_lookup */
    if (ctx->buf) {
      FREE (ctx->buf);
      ctx->buf = NULL;
    }
		
    log_error_write(srv, __FILE__, __LINE__, "sbsb",
		    "not a regular file:", con->uri.path,
		    "->", con->physical.path);
    
    return HANDLER_FINISHED;
  }

  if (con->uri.path->ptr[s_len] == '/' || !S_ISREG(sce->st.st_mode)) {
    return HANDLER_FINISHED;
  }

  if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr)
    size = atoi (p->conf.xattr_file_size->ptr);

  if ((size_t)sce->st.st_size > size) {
    
    path = con->physical.path->ptr + ctx->prefix;
    fd = glusterfs_open ((libglusterfs_handle_t ) ((unsigned long)p->conf.handle), path, O_RDONLY, 0);
    
    if (!fd) {
      con->http_status = 403;
      return HANDLER_FINISHED;
    }
    ctx->fd = fd;
  }

  /* we only handline regular files */
#ifdef HAVE_LSTAT
  if ((sce->is_symlink == 1) && !con->conf.follow_symlink) {
    con->http_status = 403;
	  
    if (con->conf.log_request_handling) {
      log_error_write(srv, __FILE__, __LINE__,  "s",  "-- access denied due symlink restriction");
      log_error_write(srv, __FILE__, __LINE__,  "sb", "Path         :", con->physical.path);
    }
    
    buffer_reset(con->physical.path);
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
    
    return HANDLER_FINISHED;
  }

  /* mod_compress might set several data directly, don't overwrite them */

  /* set response content-type, if not set already */
  
  if (NULL == array_get_element(con->response.headers, CONST_STR_LEN("Content-Type"))) {
    if (buffer_is_empty(sce->content_type)) {
      /* we are setting application/octet-stream, but also announce that
       * this header field might change in the seconds few requests 
       *
       * This should fix the aggressive caching of FF and the script download
       * seen by the first installations
       */
      response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"), CONST_STR_LEN("application/octet-stream"));
      
      allow_caching = 0;
    } else {
      response_header_overwrite(srv, con, CONST_STR_LEN("Content-Type"), CONST_BUF_LEN(sce->content_type));
    }
  }
  
  if (con->conf.range_requests) {
    response_header_overwrite(srv, con, CONST_STR_LEN("Accept-Ranges"), CONST_STR_LEN("bytes"));
  }

  /* TODO: Allow Cachable requests */	
#if 0
  if (allow_caching) {
    if (p->conf.etags_used && con->etag_flags != 0 && !buffer_is_empty(sce->etag)) {
      if (NULL == array_get_element(con->response.headers, "ETag")) {
	/* generate e-tag */
	etag_mutate(con->physical.etag, sce->etag);
	
	response_header_overwrite(srv, con, CONST_STR_LEN("ETag"), CONST_BUF_LEN(con->physical.etag));
      }
    }
    
    /* prepare header */
    if (NULL == (ds = (data_string *)array_get_element(con->response.headers, "Last-Modified"))) {
      mtime = strftime_cache_get(srv, sce->st.st_mtime);
      response_header_overwrite(srv, con, CONST_STR_LEN("Last-Modified"), CONST_BUF_LEN(mtime));
    } else {
      mtime = ds->value;
    }
    
    if (HANDLER_FINISHED == http_response_handle_cachable(srv, con, mtime)) {
      return HANDLER_FINISHED;
    }
  }
#endif 
  
  /*TODO: Read about etags */
  if (NULL != array_get_element(con->request.headers, CONST_STR_LEN("Range")) && con->conf.range_requests) {
    int do_range_request = 1;
    data_string *ds = NULL;
    buffer *mtime = NULL;
    /* check if we have a conditional GET */
    
    /* prepare header */
    if (NULL == (ds = (data_string *)array_get_element(con->response.headers, CONST_STR_LEN("Last-Modified")))) {
      mtime = strftime_cache_get(srv, sce->st.st_mtime);
      response_header_overwrite(srv, con, CONST_STR_LEN("Last-Modified"), CONST_BUF_LEN(mtime));
    } else {
      mtime = ds->value;
    }

    if (NULL != (ds = (data_string *)array_get_element(con->request.headers, CONST_STR_LEN("If-Range")))) {
      /* if the value is the same as our ETag, we do a Range-request,
       * otherwise a full 200 */
	    
      if (ds->value->ptr[0] == '"') {
	/**
	 * client wants a ETag
	 */
	if (!con->physical.etag) {
	  do_range_request = 0;
	} else if (!buffer_is_equal(ds->value, con->physical.etag)) {
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
      con->send->is_closed = 1; 
	    
      if (0 == http_response_parse_range(srv, con, p)) {
	con->http_status = 206;
      }
      return HANDLER_FINISHED;
    }
  }
  
  /* if we are still here, prepare body */

  /* we add it here for all requests
   * the HEAD request will drop it afterwards again
   */

  if (p->conf.xattr_file_size && p->conf.xattr_file_size->ptr)
    size = atoi (p->conf.xattr_file_size->ptr);

  if (size < (size_t)sce->st.st_size) {
    chunkqueue_append_glusterfs_file (con->send_raw, fd, 0, sce->st.st_size);
    con->send_raw->bytes_in += sce->st.st_size;
    chunkqueue_append_dummy_mem_chunk (con->send, sce->st.st_size);
  } else {
    if (!ctx->buf) {
      con->http_status = 404;
      return HANDLER_ERROR;
    }
    chunkqueue_append_glusterfs_mem (con->send, ctx->buf, sce->st.st_size);
    ctx->buf = NULL;
  }
  ctx->response_content_length = con->response.content_length = sce->st.st_size;
  
  con->send->is_closed = 1;
  con->send->bytes_in = sce->st.st_size;

  return HANDLER_FINISHED;
}

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

URIHANDLER_FUNC(mod_glusterfs_response_done) {
  plugin_data *p = p_d;
  UNUSED (srv);
  mod_glusterfs_ctx_t *ctx = con->plugin_ctx[p->id];
	
  con->plugin_ctx[p->id] = NULL;
  FREE (ctx);
  return HANDLER_GO_ON;
}

#if 0
URIHANDLER_FUNC(mod_glusterfs_filter_response_content) {
  plugin_data *p = p_d;
  chunk *prev = NULL, *c = NULL, *first = NULL;

  mod_glusterfs_patch_connection (srv, con, p);
  if (!p->conf.prefix)
    return HANDLER_GO_ON;

  if (p->conf.prefix->used == 0 ) {
    if (p->conf.handle <= 0) {
      con->http_status = 500;
      return HANDLER_FINISHED;
    }
    else
      return HANDLER_GO_ON;
  }

  first = con->send->first;
  for (prev = c = con->send->first; c; c = c->next) {
    if (c->type == MEM_CHUNK && c->mem->used && !c->mem->ptr) {
      c->file.name = NULL;
      c->mem->used = 0;
      buffer_free (c->mem);
      c->mem = NULL;

      c->type = FILE_CHUNK;
      c->offset = c->file.length = 0;
      c->file.name = NULL;

      if (first == c)
	first = c->next;

      if (con->send->last == c)
	con->send->last = NULL;

      prev->next = c->next;

      FREE(c);
    }
    prev = c;
  }
  chunkqueue_remove_finished_chunks (con->send);
  return HANDLER_GO_ON;
}
#endif

int mod_glusterfs_plugin_init(plugin *p) {
  p->version     = LIGHTTPD_VERSION_ID;
  p->name        = buffer_init_string("glusterfs");
  p->init        = mod_glusterfs_init;
  p->handle_physical = mod_glusterfs_handle_physical;
  p->handle_start_backend = mod_glusterfs_subrequest;
  //  p->handle_response_header = mod_glusterfs_response_header;
  //  p->handle_filter_response_content = mod_glusterfs_filter_response_content;
  //	p->handle_request_done = mod_glusterfs_request_done;
  p->handle_response_done = mod_glusterfs_response_done;
  p->set_defaults  = mod_glusterfs_set_defaults;
  p->connection_reset = mod_glusterfs_connection_reset;
  p->cleanup     = mod_glusterfs_free;
  
  p->data        = NULL;
  
  return 0;
}
