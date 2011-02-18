/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>
#include <sys/resource.h>


#include "server.h"
#include "server-helpers.h"
#include "glusterfs3-xdr.h"
#include "call-stub.h"
#include "statedump.h"
#include "defaults.h"
#include "authenticate.h"
#include "rpcsvc.h"

struct iobuf *
gfs_serialize_reply (rpcsvc_request_t *req, void *arg, gfs_serialize_t sfunc,
                     struct iovec *outmsg)
{
        struct iobuf            *iob = NULL;
        ssize_t                  retlen = -1;

        /* First, get the io buffer into which the reply in arg will
         * be serialized.
         */
        iob = iobuf_get (req->svc->ctx->iobuf_pool);
        if (!iob) {
                gf_log ("", GF_LOG_ERROR, "Failed to get iobuf");
                goto ret;
        }

        iobuf_to_iovec (iob, outmsg);
        /* Use the given serializer to translate the give C structure in arg
         * to XDR format which will be written into the buffer in outmsg.
         */
        /* retlen is used to received the error since size_t is unsigned and we
         * need -1 for error notification during encoding.
         */
        retlen = sfunc (*outmsg, arg);
        if (retlen == -1) {
                /* Failed to Encode 'GlusterFS' msg in RPC is not exactly
                   failure of RPC return values.. client should get
                   notified about this, so there are no missing frames */
                gf_log ("", GF_LOG_ERROR, "Failed to encode message");
                req->rpc_err = GARBAGE_ARGS;
                retlen = 0;
        }

        outmsg->iov_len = retlen;
ret:
        if (retlen == -1) {
                iobuf_unref (iob);
                iob = NULL;
        }

        return iob;
}



/* Generic reply function for NFSv3 specific replies. */
int
server_submit_reply (call_frame_t *frame, rpcsvc_request_t *req, void *arg,
                     struct iovec *payload, int payloadcount,
                     struct iobref *iobref, gfs_serialize_t sfunc)
{
        struct iobuf           *iob        = NULL;
        int                     ret        = -1;
        struct iovec            rsp        = {0,};
        server_state_t         *state      = NULL;
        char                    new_iobref = 0;

        if (!req) {
                goto ret;
        }

        if (frame) {
                state = CALL_STATE (frame);
                frame->local = NULL;
        }

        if (!iobref) {
                iobref = iobref_new ();
                if (!iobref) {
                        gf_log ("", GF_LOG_ERROR, "out of memory");
                        goto ret;
                }

                new_iobref = 1;
        }

        iob = gfs_serialize_reply (req, arg, sfunc, &rsp);
        if (!iob) {
                gf_log ("", GF_LOG_ERROR, "Failed to serialize reply");
                goto ret;
        }

        iobref_add (iobref, iob);

        /* Then, submit the message for transmission. */
        ret = rpcsvc_submit_generic (req, &rsp, 1, payload, payloadcount,
                                     iobref);

        /* TODO: this is demo purpose only */
        /* ret = rpcsvc_callback_submit (req->svc, req->trans, req->prog,
                                         GF_CBK_NULL, &rsp, 1);
        */
        /* Now that we've done our job of handing the message to the RPC layer
         * we can safely unref the iob in the hope that RPC layer must have
         * ref'ed the iob on receiving into the txlist.
         */
        iobuf_unref (iob);
        if (ret == -1) {
                gf_log ("", GF_LOG_ERROR, "Reply submission failed");
                goto ret;
        }

        ret = 0;
ret:
        if (state) {
                free_state (state);
        }

        if (frame) {
                STACK_DESTROY (frame->root);
        }

        if (new_iobref) {
                iobref_unref (iobref);
        }

        return ret;
}

/* */
int
xdr_to_glusterfs_req (rpcsvc_request_t *req, void *arg, gfs_serialize_t sfunc)
{
        int                     ret = -1;

        if (!req)
                return -1;

        ret = sfunc (req->msg[0], arg);

        if (ret > 0)
                ret = 0;

        return ret;
}

int
server_fd (xlator_t *this)
{
         server_conf_t        *conf = NULL;
         server_connection_t  *trav = NULL;
         char                 key[GF_DUMP_MAX_BUF_LEN];
         int                  i = 1;
         int                  ret = -1;

         if (!this)
                 return -1;

         conf = this->private;
         if (!conf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "conf null in xlator");
                return -1;
         }

         gf_proc_dump_add_section("xlator.protocol.server.conn");

         ret = pthread_mutex_trylock (&conf->mutex);
         if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to dump fdtable"
                " errno: %d", errno);
                return -1;
        }

         list_for_each_entry (trav, &conf->conns, list) {
                 if (trav->id) {
                         gf_proc_dump_build_key(key,
                                          "xlator.protocol.server.conn",
                                          "%d.id", i);
                         gf_proc_dump_write(key, "%s", trav->id);
                 }

                 gf_proc_dump_build_key(key,"xlator.protocol.server.conn",
                                        "%d.ref",i)
                 gf_proc_dump_write(key, "%d", trav->ref);
                 if (trav->bound_xl) {
                         gf_proc_dump_build_key(key,
                                          "xlator.protocol.server.conn",
                                          "%d.bound_xl", i);
                         gf_proc_dump_write(key, "%s", trav->bound_xl->name);
                 }

                 gf_proc_dump_build_key(key,
                                        "xlator.protocol.server.conn",
                                         "%d.id", i);
                 fdtable_dump(trav->fdtable,key);
                 i++;
         }
        pthread_mutex_unlock (&conf->mutex);


        return 0;
 }

int
server_priv (xlator_t *this)
{
        server_conf_t    *conf = NULL;
        rpc_transport_t  *xprt = NULL;
        char              key[GF_DUMP_MAX_BUF_LEN] = {0,};
        uint64_t          total_read = 0;
        uint64_t          total_write = 0;

        conf = this->private;
        if (!conf)
                return 0;

        list_for_each_entry (xprt, &conf->xprt_list, list) {
                total_read  += xprt->total_bytes_read;
                total_write += xprt->total_bytes_write;
        }

        gf_proc_dump_build_key(key, "server", "total-bytes-read");
        gf_proc_dump_write(key, "%"PRIu64, total_read);

        gf_proc_dump_build_key(key, "server", "total-bytes-write");
        gf_proc_dump_write(key, "%"PRIu64, total_write);

        return 0;
}

int
server_inode (xlator_t *this)
{
         server_conf_t        *conf = NULL;
         server_connection_t  *trav = NULL;
         char                 key[GF_DUMP_MAX_BUF_LEN];
         int                  i = 1;
         int                  ret = -1;

         if (!this)
                 return -1;

         conf = this->private;
         if (!conf) {
                gf_log (this->name, GF_LOG_WARNING,
                        "conf null in xlator");
                return -1;
         }

         ret = pthread_mutex_trylock (&conf->mutex);
         if (ret) {
                gf_log("", GF_LOG_WARNING, "Unable to dump itable"
                " errno: %d", errno);
                return -1;
        }

        list_for_each_entry (trav, &conf->conns, list) {
                 if (trav->bound_xl && trav->bound_xl->itable) {
                         gf_proc_dump_build_key(key,
                                          "xlator.protocol.server.conn",
                                          "%d.bound_xl.%s",
                                          i, trav->bound_xl->name);
                         inode_table_dump(trav->bound_xl->itable,key);
                         i++;
                 }
        }
        pthread_mutex_unlock (&conf->mutex);


        return 0;
}


static void
get_auth_types (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t   *auth_dict = NULL;
        char     *saveptr = NULL;
        char     *tmp = NULL;
        char     *key_cpy = NULL;
        int32_t   ret = -1;

        auth_dict = data;
        key_cpy = gf_strdup (key);
        GF_VALIDATE_OR_GOTO("server", key_cpy, out);

        tmp = strtok_r (key_cpy, ".", &saveptr);
        ret = strcmp (tmp, "auth");
        if (ret == 0) {
                tmp = strtok_r (NULL, ".", &saveptr);
                if (strcmp (tmp, "ip") == 0) {
                        /* TODO: backward compatibility, remove when
                           newer versions are available */
                        tmp = "addr";
                        gf_log ("server", GF_LOG_WARNING,
                                "assuming 'auth.ip' to be 'auth.addr'");
                }
                ret = dict_set_dynptr (auth_dict, tmp, NULL, 0);
                if (ret < 0) {
                        gf_log ("server", GF_LOG_DEBUG,
                                "failed to dict_set_dynptr");
                }
        }

        GF_FREE (key_cpy);
out:
        return;
}


int
validate_auth_options (xlator_t *this, dict_t *dict)
{
        int            error = 0;
        xlator_list_t *trav = NULL;
        data_pair_t   *pair = NULL;
        char          *tail = NULL;

        trav = this->children;
        while (trav) {
                error = -1;
                for (pair = dict->members_list; pair; pair = pair->next) {
                        tail = strtail (pair->key, "auth.");
                        if (!tail)
                                continue;
                        /* fast fwd thru module type */
                        tail = strchr (tail, '.');
                        if (!tail)
                                continue;
                        tail++;

                        tail = strtail (tail, trav->xlator->name);
                        if (!tail)
                                continue;

                        if (*tail == '.') {
                                error = 0;
                                break;
                        }
                }
                if (-1 == error) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "volume '%s' defined as subvolume, but no "
                                "authentication defined for the same",
                                trav->xlator->name);
                        break;
                }
                trav = trav->next;
        }

        return error;
}


int
server_rpc_notify (rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                   void *data)
{
        xlator_t            *this = NULL;
        rpc_transport_t     *xprt = NULL;
        server_connection_t *conn = NULL;
        server_conf_t       *conf = NULL;


        if (!xl || !data) {
                gf_log ("server", GF_LOG_WARNING,
                        "Calling rpc_notify without initializing");
                goto out;
        }

        this = xl;
        xprt = data;
        conf = this->private;

        switch (event) {
        case RPCSVC_EVENT_ACCEPT:
        {
                /* Have a structure per new connection */
                /* TODO: Should we create anything here at all ? * /
                conn = create_server_conn_state (this, xprt);
                if (!conn)
                        goto out;

                xprt->protocol_private = conn;
                */
                INIT_LIST_HEAD (&xprt->list);

                list_add_tail (&xprt->list, &conf->xprt_list);

                break;
        }
        case RPCSVC_EVENT_DISCONNECT:
                conn = get_server_conn_state (this, xprt);
                if (conn)
                        server_connection_cleanup (this, conn);

                gf_log (this->name, GF_LOG_INFO,
                        "disconnected connection from %s",
                        xprt->peerinfo.identifier);

                list_del (&xprt->list);

                break;
        case RPCSVC_EVENT_TRANSPORT_DESTROY:
                conn = get_server_conn_state (this, xprt);
                if (conn)
                        server_connection_put (this, conn);
                break;
        default:
                break;
        }

out:
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_server_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}

int 
validate_options (xlator_t *this, dict_t *options, char **op_errstr)
{
        int               inode_lru_limit = 0;
        char              errstr[1024] = {0, };
        dict_t           *auth_modules =  NULL;
        int               ret = 0;
        data_t           *data;
        gf_boolean_t      trace;



        if (dict_get_int32 ( options, "inode-lru-limit", &inode_lru_limit) == 0){
                if (!(inode_lru_limit < (1 * GF_UNIT_MB)  && 
                      inode_lru_limit >1 )) {
                        gf_log (this->name, GF_LOG_DEBUG, "Validate inode-lru"
                                        "-limit %d, was WRONG", inode_lru_limit);
                        snprintf (errstr,1024, "Error, Greater than max value %d "
                                        ,inode_lru_limit);

                        *op_errstr = gf_strdup (errstr);
                        ret = -1;
                        goto out;
                      }
        }
        
        data = dict_get (options, "trace");
        if (data) {
                ret = gf_string2boolean (data->data, &trace);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "'trace' takes on only boolean values. "
                                                "Neglecting option");
                        snprintf (errstr,1024, "Error, trace takes only boolean"
                                               "values");
                        *op_errstr = gf_strdup (errstr);
                        ret = -1;
                        goto out;
                }
        }
        
        auth_modules = dict_new ();
        if (!auth_modules) {
                gf_log (this->name, GF_LOG_ERROR, "Out of memory");
                ret = -1;
                goto out;
        }

        dict_foreach (options, get_auth_types, auth_modules);
        ret = validate_auth_options (this, options);
        if (ret == -1) {
                /* logging already done in validate_auth_options function. */
                snprintf (errstr,1024, "authentication values are incorrect");
                *op_errstr = gf_strdup (errstr);
                goto out;
        }

        ret = gf_auth_init (this, auth_modules);
out:
        if (auth_modules)
                dict_unref (auth_modules);

        return ret;
}

static void
_copy_auth_opt (dict_t *unused,
                char *key,
                data_t *value,
                void *xl_dict)
{
        char *auth_option_pattern[] = { "auth.addr.*.allow",
                                        "auth.addr.*.reject"};
        if (fnmatch ( auth_option_pattern[0], key, 0) != 0)
                dict_set ((dict_t *)xl_dict, key, (value));
        
        if (fnmatch ( auth_option_pattern[1], key, 0) != 0)
                dict_set ((dict_t *)xl_dict, key, (value));
}


int
reconfigure (xlator_t *this, dict_t *options)
{

	server_conf_t	         *conf =NULL;
        rpcsvc_t                 *rpc_conf;
        rpcsvc_listener_t        *listeners;
	int		          inode_lru_limit;
	gf_boolean_t	          trace;
	data_t		         *data;
	int		          ret = 0;

	conf = this->private;

        if (!conf) {
                gf_log (this->name, GF_LOG_DEBUG, "conf == null!!!");
                goto out;
        }
	if (dict_get_int32 ( options, "inode-lru-limit", &inode_lru_limit) == 0){
		conf->inode_lru_limit = inode_lru_limit;
		gf_log (this->name, GF_LOG_TRACE, "Reconfigured inode-lru-limit"
			" to %d", conf->inode_lru_limit);
	}

	data = dict_get (options, "trace");
	if (data) {
                ret = gf_string2boolean (data->data, &trace);
                if (ret != 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"'trace' takes on only boolean values. "
                                "Neglecting option");
			ret = -1;
                        goto out;			
		}
		conf->trace = trace;
		gf_log (this->name, GF_LOG_TRACE, "Reconfigured trace"
			" to %d", conf->trace);
		
	}
        if (!conf->auth_modules)
                conf->auth_modules = dict_new ();

        dict_foreach (options, get_auth_types, conf->auth_modules);
        ret = validate_auth_options (this, options);
        if (ret == -1) {
                /* logging already done in validate_auth_options function. */
                goto out;
        }
        dict_foreach (options, _copy_auth_opt, this->options); 

        ret = gf_auth_init (this, conf->auth_modules);
        if (ret) {
                dict_unref (conf->auth_modules);
                goto out;
        }
        
        rpc_conf = conf->rpc;
        if (!rpc_conf) {
                gf_log (this->name, GF_LOG_ERROR, "No rpc_conf !!!!");
                goto out;
        }

        list_for_each_entry (listeners, &(rpc_conf->listeners), list) {
                if (listeners->trans != NULL) {
                        if (listeners->trans->reconfigure ) 
                                listeners->trans->reconfigure (listeners->trans, options);
                        else
                               gf_log (this->name, GF_LOG_ERROR, 
                                       "Reconfigure not found for transport" );
                }
        }
        
        

out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

int
init (xlator_t *this)
{
        int32_t            ret      = -1;
        server_conf_t     *conf     = NULL;
        rpcsvc_listener_t *listener = NULL;

        if (!this)
                goto out;

        if (this->children == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "protocol/server should have subvolume");
                goto out;
        }

        if (this->parents != NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "protocol/server should not have parent volumes");
                goto out;
        }

        conf = GF_CALLOC (1, sizeof (server_conf_t), gf_server_mt_server_conf_t);
        GF_VALIDATE_OR_GOTO(this->name, conf, out);

        INIT_LIST_HEAD (&conf->conns);
        INIT_LIST_HEAD (&conf->xprt_list);
        pthread_mutex_init (&conf->mutex, NULL);

        ret = server_build_config (this, conf);
        if (ret)
                goto out;

        ret = dict_get_str (this->options, "config-directory", &conf->conf_dir);
        if (ret)
                conf->conf_dir = CONFDIR;

        /* Authentication modules */
        conf->auth_modules = dict_new ();
        GF_VALIDATE_OR_GOTO(this->name, conf->auth_modules, out);

        dict_foreach (this->options, get_auth_types, conf->auth_modules);
        ret = validate_auth_options (this, this->options);
        if (ret == -1) {
                /* logging already done in validate_auth_options function. */
                goto out;
        }

        ret = gf_auth_init (this, conf->auth_modules);
        if (ret) {
                dict_unref (conf->auth_modules);
                goto out;
        }

        /* RPC related */
        //conf->rpc = rpc_svc_init (&conf->rpc_conf);
        conf->rpc = rpcsvc_init (this->ctx, this->options);
        if (conf->rpc == NULL) {
                ret = -1;
                goto out;
        }

        ret = rpcsvc_create_listeners (conf->rpc, this->options,
                                       this->name);
        if (ret < 1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "creation of listener failed");
                ret = -1;
                goto out;
        }

        ret = rpcsvc_register_notify (conf->rpc, server_rpc_notify, this);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "registration of notify with rpcsvc failed");
                goto out;
        }

        glusterfs3_1_fop_prog.options = this->options;
        ret = rpcsvc_program_register (conf->rpc, &glusterfs3_1_fop_prog);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "registration of program (name:%s, prognum:%d, "
                        "progver:%d) failed", glusterfs3_1_fop_prog.progname,
                        glusterfs3_1_fop_prog.prognum,
                        glusterfs3_1_fop_prog.progver);
                goto out;
        }

        gluster_handshake_prog.options = this->options;
        ret = rpcsvc_program_register (conf->rpc, &gluster_handshake_prog);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "registration of program (name:%s, prognum:%d, "
                        "progver:%d) failed", gluster_handshake_prog.progname,
                        gluster_handshake_prog.prognum,
                        gluster_handshake_prog.progver);
                rpcsvc_program_unregister (conf->rpc, &glusterfs3_1_fop_prog);
                goto out;
        }

#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;

                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "WARNING: Failed to set 'ulimit -n 1M': %s",
                                strerror(errno));
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set max open fd to 64k: %s",
                                        strerror(errno));
                        } else {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "max open fd set to 64k");
                        }
                }
        }
#endif
        this->private = conf;

        ret = 0;
out:
        if (ret) {
                if (this != NULL) {
                        this->fini (this);
                }

                if (listener != NULL) {
                        rpcsvc_listener_destroy (listener);
                }
        }

        return ret;
}


void
fini (xlator_t *this)
{
#if 0
        server_conf_t *conf = NULL;

        conf = this->private;

        if (conf) {
                if (conf->rpc) {
                        /* TODO: memory leak here, have to free RPC */
                        /*
                        if (conf->rpc->conn) {
                                rpcsvc_conn_destroy (conf->rpc->conn);
                        }
                        rpcsvc_fini (conf->rpc);
                        */
                        ;
                }

                if (conf->auth_modules)
                        dict_unref (conf->auth_modules);

                GF_FREE (conf);
        }

        this->private = NULL;
#endif
        return;
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int          ret = 0;
        switch (event) {
        default:
                default_notify (this, event, data);
                break;
        }

        return ret;
}


struct xlator_fops fops = {
};

struct xlator_cbks cbks = {
};

struct xlator_dumpops dumpops = {
        .priv  = server_priv,
        .fd    = server_fd,
        .inode = server_inode,
};


struct volume_options options[] = {
        { .key   = {"transport-type"},
          .value = {"rpc", "rpc-over-rdma", "tcp", "socket", "ib-verbs",
                    "unix", "ib-sdp", "tcp/server", "ib-verbs/server", "rdma",
                    "rdma*([ \t]),*([ \t])socket",
                    "rdma*([ \t]),*([ \t])tcp",
                    "tcp*([ \t]),*([ \t])rdma",
                    "socket*([ \t]),*([ \t])rdma"},
          .type  = GF_OPTION_TYPE_STR
        },
        { .key   = {"volume-filename.*"},
          .type  = GF_OPTION_TYPE_PATH,
        },
        { .key   = {"transport.*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"rpc*"},
          .type  = GF_OPTION_TYPE_ANY,
        },
        { .key   = {"inode-lru-limit"},
          .type  = GF_OPTION_TYPE_INT,
          .min   = 0,
          .max   = (1 * GF_UNIT_MB)
        },
        { .key   = {"verify-volfile-checksum"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"trace"},
          .type  = GF_OPTION_TYPE_BOOL
        },
        { .key   = {"config-directory",
                    "conf-dir"},
          .type  = GF_OPTION_TYPE_PATH,
        },

        { .key   = {NULL} },
};
