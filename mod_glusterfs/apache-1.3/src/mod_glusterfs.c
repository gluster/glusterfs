#include <httpd.h>
#include <http_config.h>
#include <http_core.h>
#include <http_request.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_main.h>
#include <util_script.h>
#include <libglusterfsclient.h>
#include <sys/uio.h>
#include <pthread.h>

#define GLUSTERFS_INVALID_LOGLEVEL "mod_glusterfs: Unrecognized log-level \"%s\", possible values are \"DEBUG|WARNING|ERROR|CRITICAL|NONE\"\n"

#define GLUSTERFS_HANDLER "glusterfs-handler"
#define GLUSTERFS_CHUNK_SIZE 131072 //65536

module MODULE_VAR_EXPORT glusterfs_module;
extern module core_module;

/*TODO: verify error returns to server core */

typedef struct {
  
#ifdef GPROF
    char *gprof_dir;
#endif

    /* Name translations --- we want the core to be able to do *something*
     * so it's at least a minimally functional web server on its own (and
     * can be tested that way).  But let's keep it to the bare minimum:
     */
    char *ap_document_root;
  
    /* Access control */

    char *access_name;
    array_header *sec;
    array_header *sec_url;
} core_server_config;

typedef struct glusterfs_dir_config {
  char *logfile;
  char *loglevel;
  char *specfile;
  char *mount_dir;
  char *buf;
  size_t window_size;
  size_t xattr_file_size;
  uint32_t lookup_timeout;
  uint32_t stat_timeout;
  libglusterfs_handle_t handle;
} glusterfs_dir_config_t;

typedef struct glusterfs_async_local {
  int op_ret;
  int op_errno;
  char async_read_complete;
  off_t length;
  off_t read_bytes;
  //off_t offset;
  // int fd;
  //int callcnt;
  //  off_t read_bytes;
  glusterfs_read_buf_t *buf;
  request_rec *request;
  pthread_mutex_t lock;
  pthread_cond_t cond;
}glusterfs_async_local_t;

#define GLUSTERFS_CMD_PERMS ACCESS_CONF

static glusterfs_dir_config_t *
mod_glusterfs_dconfig(request_rec *r)
{
  return (glusterfs_dir_config_t *) ap_get_module_config(r->per_dir_config, &glusterfs_module);
}

static 
const char *add_window_size(cmd_parms *cmd, void *dummy, char *arg)
{
  glusterfs_dir_config_t *dir_config = dummy;
  dir_config->window_size = atoi (arg);
  return NULL;
}

static 
const char *add_xattr_file_size(cmd_parms *cmd, void *dummy, char *arg)
{
  glusterfs_dir_config_t *dir_config = dummy;
  dir_config->xattr_file_size = atoi (arg);
  return NULL;
}

static
const char *set_lookup_cache_timeout(cmd_parms *cmd, void *dummy, char *arg)
{
  glusterfs_dir_config_t *dir_config = dummy;
  dir_config->lookup_timeout = atoi (arg);
  return NULL;
}

static
const char *set_stat_cache_timeout(cmd_parms *cmd, void *dummy, char *arg)
{
  glusterfs_dir_config_t *dir_config = dummy;
  dir_config->stat_timeout = atoi (arg);
  return NULL;
}

static
const char *set_loglevel(cmd_parms *cmd, void *dummy, char *arg)
{
  glusterfs_dir_config_t *dir_config = dummy;
  char *error = NULL;
  if (strncasecmp (arg, "DEBUG", strlen ("DEBUG")) 
      && strncasecmp (arg, "WARNING", strlen ("WARNING")) 
      && strncasecmp (arg, "CRITICAL", strlen ("CRITICAL")) 
      && strncasecmp (arg, "NONE", strlen ("NONE")) 
      && strncasecmp (arg, "ERROR", strlen ("ERROR")))
    error = GLUSTERFS_INVALID_LOGLEVEL;
  else
    dir_config->loglevel = arg;

  return error;
}

static 
const char *add_logfile(cmd_parms *cmd, void *dummy, char *arg)
{
    /* This one's pretty generic... */

  glusterfs_dir_config_t *dir_config = dummy;
  /*    
  const char *err = ap_check_cmd_context(cmd, NOT_IN_LIMIT);
  if (err != NULL) {
    return err;
  }
  */
  dir_config->logfile = arg;

  return NULL;
}

static 
const char *add_specfile(cmd_parms *cmd, void *dummy, char *arg)
{
  /* This one's pretty generic... */
  glusterfs_dir_config_t *dir_config = dummy;
  
  /*  
  const char *err = ap_check_cmd_context(cmd, NOT_IN_LIMIT);
  if (err != NULL) {
    return err;
  }
  */

  dir_config->specfile = arg;

  return NULL;
}

static void *
mod_glusterfs_create_dir_config(pool *p, char *dirspec)
{
  glusterfs_dir_config_t *dir_config = NULL;
  /*    char *dname = dirspec; */

  dir_config = (glusterfs_dir_config_t *) ap_pcalloc(p, sizeof(*dir_config));

  dir_config->mount_dir = dirspec;
  dir_config->logfile = dir_config->specfile = (char *)0;
  dir_config->loglevel = NULL;
  dir_config->handle = (libglusterfs_handle_t) 0;
  dir_config->window_size = 0;
  dir_config->buf = NULL;

  return (void *) dir_config;
}

static void 
mod_glusterfs_child_init(server_rec *s, pool *p)
{
  void **mod_config;
  int n, i;
  core_server_config *mod_core_config = ap_get_module_config (s->module_config,
							      &core_module);
  glusterfs_dir_config_t *dir_config = NULL;
  glusterfs_init_ctx_t ctx;
  
  n = mod_core_config->sec_url->nelts;
  mod_config = (void **)mod_core_config->sec_url->elts;
  for (i = 0; i < n; i++) {
    dir_config = ap_get_module_config (mod_config[i], &glusterfs_module);

    if (dir_config) {
      memset (&ctx, 0, sizeof (ctx));

      ctx.logfile = dir_config->logfile;
      ctx.loglevel = dir_config->loglevel;
      ctx.lookup_timeout = dir_config->lookup_timeout;
      ctx.stat_timeout = dir_config->stat_timeout;
      ctx.specfile = dir_config->specfile;

      dir_config->handle = glusterfs_init (&ctx);
    }
    dir_config = NULL;
  }
}

static void 
mod_glusterfs_child_exit(server_rec *s, pool *p)
{
  void **mod_config;
  int n, i;
  core_server_config *mod_core_config = ap_get_module_config (s->module_config,
							      &core_module);
  glusterfs_dir_config_t *dir_config = NULL;
  
  n = mod_core_config->sec_url->nelts;
  mod_config = (void **)mod_core_config->sec_url->elts;
  for (i = 0; i < n; i++) {
    dir_config = ap_get_module_config (mod_config[i], &glusterfs_module);
    if (dir_config && dir_config->handle) {
      glusterfs_fini (dir_config->handle);
      dir_config->handle = 0;
    }
    dir_config = NULL;
  }
}

static int mod_glusterfs_fixup(request_rec *r)
{
  glusterfs_dir_config_t *dir_config;
  int access_status;
  int ret;
  char *path = NULL;

  dir_config = mod_glusterfs_dconfig(r);

  if (dir_config && dir_config->mount_dir && !(strncmp (ap_pstrcat (r->pool, dir_config->mount_dir, "/", NULL), r->uri, strlen (dir_config->mount_dir) + 1) && !r->handler)) 
    r->handler = ap_pstrdup (r->pool, GLUSTERFS_HANDLER);

  if (!r->handler || (r->handler && strcmp (r->handler, GLUSTERFS_HANDLER)))
    return DECLINED;

  if (dir_config->mount_dir)
    path = r->uri + strlen (dir_config->mount_dir);

  memset (&r->finfo, 0, sizeof (r->finfo));

  dir_config->buf = calloc (1, dir_config->xattr_file_size);
  ERR_ABORT (dir_config->buf);

  ret = glusterfs_lookup (dir_config->handle, path, dir_config->buf, dir_config->xattr_file_size, &r->finfo);

  if (ret == -1 || r->finfo.st_size > dir_config->xattr_file_size || S_ISDIR (r->finfo.st_mode)) {
    FREE (dir_config->buf);
    dir_config->buf = NULL;

    if (ret == -1) {
      int error = HTTP_NOT_FOUND;
      char *emsg = NULL;
      if (r->path_info == NULL) {
	emsg = ap_pstrcat(r->pool, strerror (errno), r->filename, NULL);
      }
      else {
	emsg = ap_pstrcat(r->pool, strerror (errno), r->filename, r->path_info, NULL);
      }
      ap_log_rerror(APLOG_MARK, APLOG_ERR|APLOG_NOERRNO, r, "%s", emsg);
      if (errno != ENOENT) {
	error = HTTP_INTERNAL_SERVER_ERROR;
      }
      return error;
    }
  }
    
  if (r->uri && strlen (r->uri) && r->uri[strlen(r->uri) - 1] == '/') 
    r->handler = NULL;

  r->filename = ap_pstrcat (r->pool, r->filename, r->path_info, NULL);

  if ((access_status = ap_find_types(r)) != 0) {
    return DECLINED;
  }

  return OK;
}


int 
mod_glusterfs_readv_async_cbk (glusterfs_read_buf_t *buf,
			       void *cbk_data)
{
  glusterfs_async_local_t *local = cbk_data;

  /*
  if (op_ret > 0) {
    int i;
    for (i = 0; i < count; i++) {
      if (ap_rwrite (vector[i].iov_base, vector[i].iov_len, local->request) < 0) {
	  op_ret = -1;
	break;
      }
    }
  }
  */
  pthread_mutex_lock (&local->lock);
  {
    local->async_read_complete = 1;
    local->buf = buf;
    /*
    local->op_ret = op_ret;
    local->op_errno = op_errno;

    if (op_ret > 0)
      local->read_bytes += op_ret;
    */

    pthread_cond_signal (&local->cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}


static int
mod_glusterfs_read_async (request_rec *r, int fd, off_t window, off_t offset, off_t length)
{
  glusterfs_async_local_t local;
  off_t end;
  int nbytes;
  int complete;
  pthread_cond_init (&local.cond, NULL);
  pthread_mutex_init (&local.lock, NULL);
  
  //local.fd = fd;
  memset (&local, 0, sizeof (local));
  local.request = r;

  if (length > 0)
    end = offset + length;

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
      while (!local.async_read_complete) {
      	pthread_cond_wait (&local.cond, &local.lock);
      }

      local.op_ret = local.buf->op_ret;
      local.op_errno = local.buf->op_errno;

      local.async_read_complete = 0;
      buf = local.buf;

      if (length < 0)
	complete = (local.buf->op_ret <= 0);
      else {
	local.read_bytes += local.buf->op_ret;
	complete = ((local.read_bytes == length) || (local.buf->op_ret < 0));
      }
    }
    pthread_mutex_unlock (&local.lock);

    for (i = 0; i < buf->count; i++) {
      if (ap_rwrite (buf->vector[i].iov_base, buf->vector[i].iov_len, r) < 0) {
	  local.op_ret = -1;
	  complete = 1;
	  break;
      }
    }      

    glusterfs_free (buf);

    offset += nbytes;
  } while (!complete);

  /*
  pthread_mutex_lock (&local.lock);
  {
    off_t nbytes = 0;
    off_t end, init_value = offset;

    if (length > 0)
      end = offset + length;

    local.complete = 0;
    //	  local.callcnt = 0;
  
    while (offset <= r->finfo.st_size && (offset - init_value)< window && (length > 0 ? offset < end : 1)) {

      if (length > 0) {
	nbytes = end - offset;
	if (nbytes > GLUSTERFS_CHUNK_SIZE)
	  nbytes = GLUSTERFS_CHUNK_SIZE;
      } else
	nbytes = GLUSTERFS_CHUNK_SIZE;

      local.callcnt ++;
      glusterfs_read_async(fd, 
			   nbytes,
			   offset,
			   mod_glusterfs_readv_async_cbk,
			   (void *)&local);
      
      offset += nbytes;
    }
    do {
      while (!local.complete) {
	pthread_cond_wait (&local.cond, &local.lock);
      }
      local.complete = 0;
      
      if (length > 0) {
	if (!local.callcnt && offset >= end) {
	  glusterfs_close (fd);
	  break;
	}

	if (offset < end) {
	  nbytes = end - offset;
	  if (nbytes > GLUSTERFS_CHUNK_SIZE)
	    nbytes = GLUSTERFS_CHUNK_SIZE;

	  local.callcnt ++;
	  glusterfs_read_async(fd, 
			       nbytes,
			       offset,
			       mod_glusterfs_readv_async_cbk,
			       (void *)&local);
	  offset += nbytes;
	}
      } else {
	if (local.op_ret <= 0)
	  break;
	
	local.callcnt++;
	glusterfs_read_async(fd, 
			     GLUSTERFS_CHUNK_SIZE,
			     offset,
			     mod_glusterfs_readv_async_cbk,
			     (void *)&local);
	
	offset += GLUSTERFS_CHUNK_SIZE;
      }
    } while (1);
  }
  pthread_mutex_unlock (&local.lock);
  */
	
  return (local.op_ret < 0 ? SERVER_ERROR : OK);
}

static int 
mod_glusterfs_read_sync (request_rec *r, int fd, off_t length)
{ 
  int error = OK;
  off_t read_bytes;
  char buf [GLUSTERFS_CHUNK_SIZE];

  while ((read_bytes = glusterfs_read (fd, buf, GLUSTERFS_CHUNK_SIZE)) && read_bytes != -1) {
    ap_rwrite (buf, read_bytes, r);
  }
  if (read_bytes) {
    error = SERVER_ERROR;
  }
  return error;
}

static int 
mod_glusterfs_handler(request_rec *r)
{
  glusterfs_dir_config_t *dir_config;
  char *path = NULL;
  int error = OK;
  int rangestatus = 0;
  int errstatus = OK;
  int fd;
  
  if (!r->handler || (r->handler && strcmp (r->handler, GLUSTERFS_HANDLER)))
    return DECLINED;

  if (r->uri[0] == '\0' || r->uri[strlen(r->uri) - 1] == '/') {
    return DECLINED;
  }
  
  dir_config = mod_glusterfs_dconfig (r);
  
  if (r->method_number != M_GET) {
    return METHOD_NOT_ALLOWED;
  }

  if (!dir_config->handle) {
    ap_log_rerror (APLOG_MARK, APLOG_ERR, r,
		   "glusterfs initialization failed\n");
    return FORBIDDEN;
  }

  ap_update_mtime(r, r->finfo.st_mtime);
  ap_set_last_modified(r);
  ap_set_etag(r);
  ap_table_setn(r->headers_out, "Accept-Ranges", "bytes");
  if (((errstatus = ap_meets_conditions(r)) != OK)
      || (errstatus = ap_set_content_length(r, r->finfo.st_size))) {
    return errstatus;
  }
  rangestatus =  ap_set_byterange(r);
  ap_send_http_header(r);

  if (r->finfo.st_size <= dir_config->xattr_file_size && dir_config->buf) {
    if (!r->header_only) {
      error = OK;
      ap_log_rerror (APLOG_MARK, APLOG_NOTICE, r, 
		     "fetching data from glusterfs through xattr interface\n");
      
      if (!rangestatus) {
	if (ap_rwrite (dir_config->buf, r->finfo.st_size, r) < 0) {
	  error = HTTP_INTERNAL_SERVER_ERROR;
	}
      } else {
	long offset, length;
	while (ap_each_byterange (r, &offset, &length)) {
	  if (ap_rwrite (dir_config->buf + offset, length, r) < 0) {
	    error = HTTP_INTERNAL_SERVER_ERROR;
	    break;
	  }
	}
      }
    }

    FREE (dir_config->buf);
    dir_config->buf = NULL;

    return error;
  }

  path = r->uri + strlen (dir_config->mount_dir);
  fd = glusterfs_open (dir_config->handle, path , O_RDONLY, 0);
  
  if (fd == -1) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, r,
		  "file permissions deny server access: %s", r->filename);
    return FORBIDDEN;
  }
  
  if (!r->header_only) {
    if (dir_config->window_size > 0) {
      if (!rangestatus) {
	mod_glusterfs_read_async (r, fd, dir_config->window_size, 0, -1);
      } else {
	long offset, length;
	while (ap_each_byterange(r, &offset, &length)) {
	  mod_glusterfs_read_async (r, fd, dir_config->window_size, offset, length);
	}
      }
    }else {
      if (!rangestatus) {
	mod_glusterfs_read_sync (r, fd, -1);
      } else {
	long offset, length;
	while (ap_each_byterange (r, &offset, &length)) {
	  mod_glusterfs_read_sync (r, fd, length);
	}
      }
    }
  }
  
  glusterfs_close (fd);
  return error;
}

static const command_rec mod_glusterfs_cmds[] =
{
    {"GlusterfsLogfile", add_logfile, NULL,
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Glusterfs Logfile"},
    {"GlusterfsLoglevel", set_loglevel, NULL,
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Glusterfs Loglevel:anyone of none, critical, error, warning, debug"},
    {"GlusterfsLookupCacheTimeout", set_lookup_cache_timeout, NULL,
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Timeout value in seconds for caching lookups"},
    {"GlusterfsStatCacheTimeout", set_stat_cache_timeout, NULL,
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Timeout value in seconds for caching stats"},
    {"GlusterfsVolumeSpecfile", add_specfile, NULL,
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Glusterfs Specfile required to access contents of this directory"},
    {"GlusterfsAsyncWindowSize", add_window_size, NULL,
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Size of data to be read/written before sending next bunch of asynchronous read/write calls to glusterfs"},
    {"GlusterfsXattrFileSize", add_xattr_file_size, NULL, 
     GLUSTERFS_CMD_PERMS, TAKE1,
     "Maximum size of the file to be fetched using xattr interface of glusterfs"},
    {NULL}
};

static const handler_rec mod_glusterfs_handlers[] =
{
    {GLUSTERFS_HANDLER, mod_glusterfs_handler},
    {NULL}
};

module glusterfs_module =
{
    STANDARD_MODULE_STUFF,
    NULL, 
    mod_glusterfs_create_dir_config,  /* per-directory config creator */
    NULL,
    NULL,       /* server config creator */
    NULL,        /* server config merger */
    mod_glusterfs_cmds,               /* command table */
    mod_glusterfs_handlers,           /* [7] list of handlers */
    NULL,  /* [2] filename-to-URI translation */
    NULL,      /* [5] check/validate user_id */
    NULL,       /* [6] check user_id is valid *here* */
    NULL,     /* [4] check access by host address */
    NULL,       /* [7] MIME type checker/setter */
    mod_glusterfs_fixup,        /* [8] fixups */
    NULL,             /* [10] logger */
#if MODULE_MAGIC_NUMBER >= 19970103
    NULL,      /* [3] header parser */
#endif
#if MODULE_MAGIC_NUMBER >= 19970719
    mod_glusterfs_child_init,         /* process initializer */
#endif
#if MODULE_MAGIC_NUMBER >= 19970728
    mod_glusterfs_child_exit,         /* process exit/cleanup */
#endif
#if MODULE_MAGIC_NUMBER >= 19970902
    NULL   /* [1] post read_request handling */
#endif
};
