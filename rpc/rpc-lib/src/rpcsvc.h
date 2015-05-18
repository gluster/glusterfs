/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _RPCSVC_H
#define _RPCSVC_H

#include "event.h"
#include "rpc-transport.h"
#include "logging.h"
#include "dict.h"
#include "mem-pool.h"
#include "list.h"
#include "iobuf.h"
#include "xdr-rpc.h"
#include "glusterfs.h"
#include "xlator.h"
#include "rpcsvc-common.h"

#include <pthread.h>
#include <sys/uio.h>
#include <inttypes.h>
#include <rpc/rpc_msg.h>
#include "compat.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif

#define RPCSVC_DEFAULT_OUTSTANDING_RPC_LIMIT 64 /* Default for protocol/server */
#define RPCSVC_DEF_NFS_OUTSTANDING_RPC_LIMIT 16 /* Default for nfs/server */
#define RPCSVC_MAX_OUTSTANDING_RPC_LIMIT 65536
#define RPCSVC_MIN_OUTSTANDING_RPC_LIMIT 0 /* No limit i.e. Unlimited */

#define GF_RPCSVC       "rpc-service"
#define RPCSVC_THREAD_STACK_SIZE ((size_t)(1024 * GF_UNIT_KB))

#define RPCSVC_FRAGHDR_SIZE  4       /* 4-byte RPC fragment header size */
#define RPCSVC_DEFAULT_LISTEN_PORT      GF_DEFAULT_BASE_PORT
#define RPCSVC_DEFAULT_MEMFACTOR        8
#define RPCSVC_EVENTPOOL_SIZE_MULT      1024
#define RPCSVC_POOLCOUNT_MULT           64
#define RPCSVC_CONN_READ        (128 * GF_UNIT_KB)
#define RPCSVC_PAGE_SIZE        (128 * GF_UNIT_KB)
#define RPC_ROOT_UID             0
#define RPC_ROOT_GID             0
#define RPC_NOBODY_UID           65534
#define RPC_NOBODY_GID           65534

/* RPC Record States */
#define RPCSVC_READ_FRAGHDR     1
#define RPCSVC_READ_FRAG        2
/* The size in bytes, if crossed by a fragment will be handed over to the
 * vectored actor so that it can allocate its buffers the way it wants.
 * In our RPC layer, we assume that vectored RPC requests/records are never
 * spread over multiple RPC fragments since that prevents us from determining
 * whether the record should be handled in RPC layer completely or handed to
 * the vectored handler.
 */
#define RPCSVC_VECTORED_FRAGSZ  4096
#define RPCSVC_VECTOR_READCRED          1003
#define RPCSVC_VECTOR_READVERFSZ        1004
#define RPCSVC_VECTOR_READVERF          1005
#define RPCSVC_VECTOR_IGNORE            1006
#define RPCSVC_VECTOR_READVEC           1007
#define RPCSVC_VECTOR_READPROCHDR       1008

#define rpcsvc_record_vectored_baremsg(rs) (((rs)->state == RPCSVC_READ_FRAG) && (rs)->vecstate == 0)
#define rpcsvc_record_vectored_cred(rs) ((rs)->vecstate == RPCSVC_VECTOR_READCRED)
#define rpcsvc_record_vectored_verfsz(rs) ((rs)->vecstate == RPCSVC_VECTOR_READVERFSZ)
#define rpcsvc_record_vectored_verfread(rs) ((rs)->vecstate == RPCSVC_VECTOR_READVERF)
#define rpcsvc_record_vectored_ignore(rs) ((rs)->vecstate == RPCSVC_VECTOR_IGNORE)
#define rpcsvc_record_vectored_readvec(rs) ((rs)->vecstate == RPCSVC_VECTOR_READVEC)
#define rpcsvc_record_vectored_readprochdr(rs) ((rs)->vecstate == RPCSVC_VECTOR_READPROCHDR)
#define rpcsvc_record_vectored(rs) ((rs)->fragsize > RPCSVC_VECTORED_FRAGSZ)
/* Includes bytes up to and including the credential length field. The credlen
 * will be followed by @credlen bytes of credential data which will have to be
 * read separately by the vectored reader. After the credentials comes the
 * verifier which will also have to be read separately including the 8 bytes of
 * verf flavour and verflen.
 */
#define RPCSVC_BARERPC_MSGSZ    32
#define rpcsvc_record_readfraghdr(rs)   ((rs)->state == RPCSVC_READ_FRAGHDR)
#define rpcsvc_record_readfrag(rs)      ((rs)->state == RPCSVC_READ_FRAG)

#define RPCSVC_LOWVERS  2
#define RPCSVC_HIGHVERS 2


#if 0
#error "defined in /usr/include/rpc/auth.h"

#define AUTH_NONE	0		/* no authentication */
#define	AUTH_NULL	0		/* backward compatibility */
#define	AUTH_SYS	1		/* unix style (uid, gids) */
#define	AUTH_UNIX	AUTH_SYS
#define	AUTH_SHORT	2		/* short hand unix style */
#define AUTH_DES	3		/* des style (encrypted timestamps) */
#define AUTH_DH		AUTH_DES	/* Diffie-Hellman (this is DES) */
#define AUTH_KERB       4               /* kerberos style */
#endif /* */

typedef struct rpcsvc_program rpcsvc_program_t;

struct rpcsvc_notify_wrapper {
        struct list_head  list;
        void             *data;
        rpcsvc_notify_t   notify;
};
typedef struct rpcsvc_notify_wrapper rpcsvc_notify_wrapper_t;


typedef struct rpcsvc_request rpcsvc_request_t;

typedef struct {
        rpc_transport_t         *trans;
        rpcsvc_t                *svc;
        /* FIXME: remove address from this structure. Instead use get_myaddr
         * interface implemented by individual transports.
         */
        struct sockaddr_storage  sa;
        struct list_head         list;
} rpcsvc_listener_t;

struct rpcsvc_config {
        int    max_block_size;
};

typedef struct rpcsvc_auth_data {
        int             flavour;
        int             datalen;
        char            authdata[GF_MAX_AUTH_BYTES];
} rpcsvc_auth_data_t;

#define rpcsvc_auth_flavour(au)    ((au).flavour)

typedef struct drc_client drc_client_t;
typedef struct drc_cached_op drc_cached_op_t;

/* The container for the RPC call handed up to an actor.
 * Dynamically allocated. Lives till the call reply is completely
 * transmitted.
 * */
struct rpcsvc_request {
        /* connection over which this request came. */
        rpc_transport_t       *trans;

        rpcsvc_t              *svc;

        rpcsvc_program_t      *prog;

        /* The identifier for the call from client.
         * Needed to pair the reply with the call.
         */
        uint32_t                xid;

        int                     prognum;

        int                     progver;

        int                     procnum;

        int                     type;

        /* Uid and gid filled by the rpc-auth module during the authentication
         * phase.
         */
        uid_t                   uid;
        gid_t                   gid;
        pid_t                   pid;

        gf_lkowner_t            lk_owner;
        uint64_t                gfs_id;

        /* Might want to move this to AUTH_UNIX specific state since this array
         * is not available for every authentication scheme.
         */
        gid_t                   *auxgids;
        gid_t                   auxgidsmall[SMALL_GROUP_COUNT];
        gid_t                   *auxgidlarge;
        int                     auxgidcount;


        /* The RPC message payload, contains the data required
         * by the program actors. This is the buffer that will need to
         * be de-xdred by the actor.
         */
        struct iovec            msg[MAX_IOVEC];
        int                     count;

        struct iobref          *iobref;

        /* Status of the RPC call, whether it was accepted or denied. */
        int                     rpc_status;

        /* In case, the call was denied, the RPC error is stored here
         * till the reply is sent.
         */
        int                     rpc_err;

        /* In case the failure happened because of an authentication problem
         * , this value needs to be assigned the correct auth error number.
         */
        int                     auth_err;

        /* There can be cases of RPC requests where the reply needs to
         * be built from multiple sources. E.g. where even the NFS reply
         * can contain a payload, as in the NFSv3 read reply. Here the RPC header
         * ,NFS header and the read data are brought together separately from
         * different buffers, so we need to stage the buffers temporarily here
         * before all of them get added to the connection's transmission list.
         */
        struct list_head        txlist;

        /* While the reply record is being built, this variable keeps track
         * of how many bytes have been added to the record.
         */
        size_t                  payloadsize;

        /* The credentials extracted from the rpc request */
        rpcsvc_auth_data_t      cred;

        /* The verified extracted from the rpc request. In request side
         * processing this contains the verifier sent by the client, on reply
         * side processing, it is filled with the verified that will be
         * sent to the client.
         */
        rpcsvc_auth_data_t      verf;

	/* Execute this request's actor function as a synctask? */
	gf_boolean_t            synctask;

        /* Container for a RPC program wanting to store a temp
         * request-specific item.
         */
        void                    *private;

        /* Container for transport to store request-specific item */
        void                    *trans_private;

        /* we need to ref the 'iobuf' in case of 'synctasking' it */
        struct iobuf            *hdr_iobuf;

        /* pointer to cached reply for use in DRC */
        drc_cached_op_t         *reply;
};

#define rpcsvc_request_program(req) ((rpcsvc_program_t *)((req)->prog))
#define rpcsvc_request_procnum(req) (((req)->procnum))
#define rpcsvc_request_program_private(req) (((rpcsvc_program_t *)((req)->prog))->private)
#define rpcsvc_request_accepted(req)    ((req)->rpc_status == MSG_ACCEPTED)
#define rpcsvc_request_accepted_success(req) ((req)->rpc_err == SUCCESS)
#define rpcsvc_request_prog_minauth(req) (rpcsvc_request_program(req)->min_auth)
#define rpcsvc_request_cred_flavour(req) (rpcsvc_auth_flavour(req->cred))
#define rpcsvc_request_verf_flavour(req) (rpcsvc_auth_flavour(req->verf))
#define rpcsvc_request_service(req)      ((req)->svc)
#define rpcsvc_request_uid(req)         ((req)->uid)
#define rpcsvc_request_gid(req)         ((req)->gid)
#define rpcsvc_request_private(req)     ((req)->private)
#define rpcsvc_request_xid(req)         ((req)->xid)
#define rpcsvc_request_set_private(req,prv)  (req)->private = (void *)(prv)
#define rpcsvc_request_iobref_ref(req)  (iobref_ref ((req)->iobref))
#define rpcsvc_request_record_ref(req)  (iobuf_ref ((req)->recordiob))
#define rpcsvc_request_record_unref(req) (iobuf_unref ((req)->recordiob))
#define rpcsvc_request_record_iob(req)   ((req)->recordiob)
#define rpcsvc_request_set_vecstate(req, state)  ((req)->vecstate = state)
#define rpcsvc_request_vecstate(req) ((req)->vecstate)
#define rpcsvc_request_transport(req) ((req)->trans)
#define rpcsvc_request_transport_ref(req) (rpc_transport_ref((req)->trans))
#define RPC_AUTH_ROOT_SQUASH(req)                                       \
        do {                                                            \
                int gidcount = 0;                                       \
                if (req->svc->root_squash) {                            \
                        if (req->uid == RPC_ROOT_UID)                   \
                                req->uid = req->svc->anonuid;           \
                        if (req->gid == RPC_ROOT_GID)                   \
                                req->gid = req->svc->anongid;           \
                                                                        \
                        for (gidcount = 0; gidcount < req->auxgidcount; \
                             ++gidcount) {                              \
                                if (!req->auxgids[gidcount])            \
                                        req->auxgids[gidcount] =        \
                                                req->svc->anongid;      \
                        }                                               \
                }                                                       \
        } while (0);

#define RPCSVC_ACTOR_SUCCESS    0
#define RPCSVC_ACTOR_ERROR      (-1)
#define RPCSVC_ACTOR_IGNORE     (-2)

/* Functor for every type of protocol actor
 * must be defined like this.
 *
 * See the request structure for info on how to handle the request
 * in the program actor.
 *
 * On successful santify checks inside the actor, it should return
 * RPCSVC_ACTOR_SUCCESS.
 * On an error, on which the RPC layer is expected to return a reply, the actor
 * should return RPCSVC_ACTOR_ERROR.
 *
 */
typedef int (*rpcsvc_actor) (rpcsvc_request_t *req);
typedef int (*rpcsvc_vector_sizer) (int state, ssize_t *readsize,
                                    char *base_addr, char *curr_addr);

/* Every protocol actor will also need to specify the function the RPC layer
 * will use to serialize or encode the message into XDR format just before
 * transmitting on the connection.
 */
typedef void *(*rpcsvc_encode_reply) (void *msg);

/* Once the reply has been transmitted, the message will have to be de-allocated
 * , so every actor will need to provide a function that deallocates the message
 * it had allocated as a response.
 */
typedef void (*rpcsvc_deallocate_reply) (void *msg);

#define RPCSVC_NAME_MAX            32
/* The descriptor for each procedure/actor that runs
 * over the RPC service.
 */
typedef struct rpcsvc_actor_desc {
        char                    procname[RPCSVC_NAME_MAX];
        int                     procnum;
        rpcsvc_actor            actor;

        /* Handler for cases where the RPC requests fragments are large enough
         * to benefit from being decoded into aligned memory addresses. While
         * decoding the request in a non-vectored manner, due to the nature of
         * the XDR scheme, RPC cannot guarantee memory aligned addresses for
         * the resulting message-specific structures. Allowing a specialized
         * handler for letting the RPC program read the data from the network
         * directly into its aligned buffers.
         */
        rpcsvc_vector_sizer     vector_sizer;

        /* Can actor be ran on behalf an unprivileged requestor? */
        gf_boolean_t            unprivileged;
        drc_op_type_t           op_type;
} rpcsvc_actor_t;

/* Describes a program and its version along with the function pointers
 * required to handle the procedures/actors of each program/version.
 * Never changed ever by any thread so no need for a lock.
 */
struct rpcsvc_program {
        char                    progname[RPCSVC_NAME_MAX];
        int                     prognum;
        int                     progver;
        /* FIXME */
        dict_t                 *options;        /* An opaque dictionary
                                                 * populated by the program
                                                 * (probably from xl->options)
                                                 * which contain enough
                                                 * information for transport to
                                                 * initialize. As a part of
                                                 * cleanup, the members of
                                                 * options which are of interest
                                                 * to transport should be put
                                                 * into a structure for better
                                                 * readability and structure
                                                 * should replace options member
                                                 * here.
                                                 */
        uint16_t                progport;       /* Registered with portmap */
#if 0
        int                     progaddrfamily; /* AF_INET or AF_INET6 */
        char                    *proghost;      /* Bind host, can be NULL */
#endif
        rpcsvc_actor_t          *actors;        /* All procedure handlers */
        int                     numactors;      /* Num actors in actor array */
        int                     proghighvers;   /* Highest ver for program
                                                   supported by the system. */
        int                     proglowvers;    /* Lowest ver */

        /* Program specific state handed to actors */
        void                    *private;


        /* This upcall is provided by the program during registration.
         * It is used to notify the program about events like connection being
         * destroyed etc. The rpc program may take appropriate actions, for eg.,
         * in the case of connection being destroyed, it should cleanup its
         * state stored in the connection.
         */
        rpcsvc_notify_t         notify;

        /* An integer that identifies the min auth strength that is required
         * by this protocol, for eg. MOUNT3 needs AUTH_UNIX at least.
         * See RFC 1813, Section 5.2.1.
         */
        int                     min_auth;

	/* Execute actor function as a synctask? */
	gf_boolean_t            synctask;

        /* list member to link to list of registered services with rpcsvc */
        struct list_head        program;
};

typedef struct rpcsvc_cbk_program {
        char                 *progname;
        int                   prognum;
        int                   progver;
} rpcsvc_cbk_program_t;
/* All users of RPC services should use this API to register their
 * procedure handlers.
 */
extern int
rpcsvc_program_register (rpcsvc_t *svc, rpcsvc_program_t *program);

extern int
rpcsvc_program_unregister (rpcsvc_t *svc, rpcsvc_program_t *program);

/* This will create and add a listener to listener pool. Programs can
 * use any of the listener in this pool. A single listener can be used by
 * multiple programs and vice versa. There can also be a one to one mapping
 * between a program and a listener. After registering a program with rpcsvc,
 * the program has to be associated with a listener using
 * rpcsvc_program_register_portmap.
 */
/* FIXME: can multiple programs registered on same port? */
extern int32_t
rpcsvc_create_listeners (rpcsvc_t *svc, dict_t *options, char *name);

void
rpcsvc_listener_destroy (rpcsvc_listener_t *listener);

extern int
rpcsvc_program_register_portmap (rpcsvc_program_t *newprog, uint32_t port);

extern int
rpcsvc_program_unregister_portmap (rpcsvc_program_t *newprog);

extern int
rpcsvc_register_portmap_enabled (rpcsvc_t *svc);

/* Inits the global RPC service data structures.
 * Called in main.
 */
extern rpcsvc_t *
rpcsvc_init (xlator_t *xl, glusterfs_ctx_t *ctx, dict_t *options,
             uint32_t poolcount);

extern int
rpcsvc_reconfigure_options (rpcsvc_t *svc, dict_t *options);

int
rpcsvc_register_notify (rpcsvc_t *svc, rpcsvc_notify_t notify, void *mydata);

/* unregister a notification callback @notify with data @mydata from svc.
 * returns the number of notification callbacks unregistered.
 */
int
rpcsvc_unregister_notify (rpcsvc_t *svc, rpcsvc_notify_t notify, void *mydata);

int
rpcsvc_transport_submit (rpc_transport_t *trans, struct iovec *rpchdr,
                         int rpchdrcount, struct iovec *proghdr,
                         int proghdrcount, struct iovec *progpayload,
                         int progpayloadcount, struct iobref *iobref,
                         void *priv);

int
rpcsvc_submit_message (rpcsvc_request_t *req, struct iovec *proghdr,
                       int hdrcount, struct iovec *payload, int payloadcount,
                       struct iobref *iobref);

int
rpcsvc_submit_generic (rpcsvc_request_t *req, struct iovec *proghdr,
                       int hdrcount, struct iovec *payload, int payloadcount,
                       struct iobref *iobref);

extern int
rpcsvc_error_reply (rpcsvc_request_t *req);

#define RPCSVC_PEER_STRLEN      1024
#define RPCSVC_AUTH_ACCEPT      1
#define RPCSVC_AUTH_REJECT      2
#define RPCSVC_AUTH_DONTCARE    3

extern int
rpcsvc_transport_peername (rpc_transport_t *trans, char *hostname, int hostlen);

extern int
rpcsvc_transport_peeraddr (rpc_transport_t *trans, char *addrstr, int addrlen,
                           struct sockaddr_storage *returnsa, socklen_t sasize);

extern int
rpcsvc_auth_check (rpcsvc_t *svc, char *volname, char *ipaddr);

extern int
rpcsvc_transport_privport_check (rpcsvc_t *svc, char *volname, uint16_t port);

#define rpcsvc_request_seterr(req, err)                 (req)->rpc_err = err
#define rpcsvc_request_set_autherr(req, err)            (req)->auth_err = err

extern int rpcsvc_submit_vectors (rpcsvc_request_t *req);

extern int rpcsvc_request_attach_vector (rpcsvc_request_t *req,
                                         struct iovec msgvec, struct iobuf *iob,
                                         struct iobref *ioref, int finalvector);


typedef int (*auth_init_trans) (rpc_transport_t *trans, void *priv);
typedef int (*auth_init_request) (rpcsvc_request_t *req, void *priv);
typedef int (*auth_request_authenticate) (rpcsvc_request_t *req, void *priv);

/* This structure needs to be registered by every authentication scheme.
 * Our authentication schemes are stored per connection because
 * each connection will end up using a different authentication scheme.
 */
typedef struct rpcsvc_auth_ops {
        auth_init_trans               transport_init;
        auth_init_request             request_init;
        auth_request_authenticate     authenticate;
} rpcsvc_auth_ops_t;

typedef struct rpcsvc_auth_flavour_desc {
        char                    authname[RPCSVC_NAME_MAX];
        int                     authnum;
        rpcsvc_auth_ops_t       *authops;
        void                    *authprivate;
} rpcsvc_auth_t;

typedef void * (*rpcsvc_auth_initer_t) (rpcsvc_t *svc, dict_t *options);

struct rpcsvc_auth_list {
        struct list_head        authlist;
        rpcsvc_auth_initer_t    init;
        /* Should be the name with which we identify the auth scheme given
         * in the volfile options.
         * This should be different from the authname in rpc_auth_t
         * in way that makes it easier to specify this scheme in the volfile.
         * This is because the technical names of the schemes can be a bit
         * arcane.
         */
        char                    name[RPCSVC_NAME_MAX];
        rpcsvc_auth_t           *auth;
        int                     enable;
};

extern int
rpcsvc_auth_request_init (rpcsvc_request_t *req, struct rpc_msg *callmsg);

extern int
rpcsvc_auth_init (rpcsvc_t *svc, dict_t *options);

extern int
rpcsvc_auth_reconf (rpcsvc_t *svc, dict_t *options);

extern int
rpcsvc_auth_transport_init (rpc_transport_t *xprt);

extern int
rpcsvc_authenticate (rpcsvc_request_t *req);

extern int
rpcsvc_auth_array (rpcsvc_t *svc, char *volname, int *autharr, int arrlen);

/* If the request has been sent using AUTH_UNIX, this function returns the
 * auxiliary gids as an array, otherwise, it returns NULL.
 * Move to auth-unix specific source file when we need to modularize the
 * authentication code even further to support mode auth schemes.
 */
extern gid_t *
rpcsvc_auth_unix_auxgids (rpcsvc_request_t *req, int *arrlen);

extern char *
rpcsvc_volume_allowed (dict_t *options, char *volname);

int rpcsvc_request_submit (rpcsvc_t *rpc, rpc_transport_t *trans,
                           rpcsvc_cbk_program_t *prog, int procnum,
                           void *req, glusterfs_ctx_t *ctx,
                           xdrproc_t xdrproc);

int rpcsvc_callback_submit (rpcsvc_t *rpc, rpc_transport_t *trans,
                            rpcsvc_cbk_program_t *prog, int procnum,
                            struct iovec *proghdr, int proghdrcount);

rpcsvc_actor_t *
rpcsvc_program_actor (rpcsvc_request_t *req);

int
rpcsvc_transport_unix_options_build (dict_t **options, char *filepath);
int
rpcsvc_set_allow_insecure (rpcsvc_t *svc, dict_t *options);
int
rpcsvc_set_addr_namelookup (rpcsvc_t *svc, dict_t *options);
int
rpcsvc_set_root_squash (rpcsvc_t *svc, dict_t *options);
int
rpcsvc_set_outstanding_rpc_limit (rpcsvc_t *svc, dict_t *options, int defvalue);

int
rpcsvc_set_throttle_on (rpcsvc_t *svc);

int
rpcsvc_set_throttle_off (rpcsvc_t *svc);

gf_boolean_t
rpcsvc_get_throttle (rpcsvc_t *svc);

int
rpcsvc_auth_array (rpcsvc_t *svc, char *volname, int *autharr, int arrlen);
rpcsvc_vector_sizer
rpcsvc_get_program_vector_sizer (rpcsvc_t *svc, uint32_t prognum,
                                 uint32_t progver, int procnum);
#endif
