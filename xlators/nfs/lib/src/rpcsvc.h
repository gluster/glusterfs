/*
  Copyright (c) 2010-2011-2011-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _NFS_RPCSVC_H
#define _NFS_RPCSVC_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "event.h"
#include "logging.h"
#include "dict.h"
#include "mem-pool.h"
#include "list.h"
#include "iobuf.h"
#include "xdr-rpc.h"
#include "glusterfs.h"
#include "xlator.h"

#include <pthread.h>
#include <sys/uio.h>

#ifdef GF_DARWIN_HOST_OS
#include <nfs/rpcv2.h>
#define NGRPS RPCAUTH_UNIXGIDS
#endif

#define GF_RPCSVC       "nfsrpc"
#define RPCSVC_THREAD_STACK_SIZE ((size_t)(1024 * GF_UNIT_KB))

#define RPCSVC_DEFAULT_MEMFACTOR        15
#define RPCSVC_EVENTPOOL_SIZE_MULT      1024
#define RPCSVC_POOLCOUNT_MULT           35
#define RPCSVC_CONN_READ        (128 * GF_UNIT_KB)
#define RPCSVC_PAGE_SIZE        (128 * GF_UNIT_KB)

/* Defines for RPC record and fragment assembly */

#define RPCSVC_FRAGHDR_SIZE  4       /* 4-byte RPC fragment header size */

/* Given the 4-byte fragment header, returns non-zero if this fragment
 * is the last fragment for the RPC record being assemebled.
 * RPC Record marking standard defines a 32 bit value as the fragment
 * header with the MSB signifying whether the fragment is the last
 * fragment for the record being asembled.
 */
#define RPCSVC_LASTFRAG(fraghdr) ((uint32_t)(fraghdr & 0x80000000U))

/* Given the 4-byte fragment header, extracts the bits that contain
 * the fragment size.
 */
#define RPCSVC_FRAGSIZE(fraghdr) ((uint32_t)(fraghdr & 0x7fffffffU))

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

#define nfs_rpcsvc_record_vectored_baremsg(rs) (((rs)->state == RPCSVC_READ_FRAG) && (rs)->vecstate == 0)
#define nfs_rpcsvc_record_vectored_cred(rs) ((rs)->vecstate == RPCSVC_VECTOR_READCRED)
#define nfs_rpcsvc_record_vectored_verfsz(rs) ((rs)->vecstate == RPCSVC_VECTOR_READVERFSZ)
#define nfs_rpcsvc_record_vectored_verfread(rs) ((rs)->vecstate == RPCSVC_VECTOR_READVERF)
#define nfs_rpcsvc_record_vectored_ignore(rs) ((rs)->vecstate == RPCSVC_VECTOR_IGNORE)
#define nfs_rpcsvc_record_vectored_readvec(rs) ((rs)->vecstate == RPCSVC_VECTOR_READVEC)
#define nfs_rpcsvc_record_vectored_readprochdr(rs) ((rs)->vecstate == RPCSVC_VECTOR_READPROCHDR)
#define nfs_rpcsvc_record_vectored(rs) ((rs)->fragsize > RPCSVC_VECTORED_FRAGSZ)
/* Includes bytes up to and including the credential length field. The credlen
 * will be followed by @credlen bytes of credential data which will have to be
 * read separately by the vectored reader. After the credentials comes the
 * verifier which will also have to be read separately including the 8 bytes of
 * verf flavour and verflen.
 */
#define RPCSVC_BARERPC_MSGSZ    32
#define nfs_rpcsvc_record_readfraghdr(rs)   ((rs)->state == RPCSVC_READ_FRAGHDR)
#define nfs_rpcsvc_record_readfrag(rs)      ((rs)->state == RPCSVC_READ_FRAG)

#define nfs_rpcsvc_conn_rpcsvc(conn)        ((conn)->stage->svc)
#define RPCSVC_LOWVERS  2
#define RPCSVC_HIGHVERS 2

typedef struct rpc_svc_program rpcsvc_program_t;
/* A Stage is the event handler thread together with
 * the connections being served by this thread.
 * It is called a stage because all the actors, i.e, protocol actors,
 * defined by higher level users of the RPC layer, are executed here.
 */
typedef struct rpc_svc_stage_context {
        pthread_t               tid;
        struct event_pool       *eventpool;     /* Per-stage event-pool */
        void                    *svc;           /* Ref to the rpcsvc_t */
} rpcsvc_stage_t;


/* RPC Records and Fragments assembly state.
 * This is per-connection state that is used to determine
 * how much data has come in, how much more needs to be read
 * and where it needs to be read.
 *
 * All this state is then used to re-assemble network buffers into
 * RPC fragments, which are then re-assembled into RPC records.
 *
 * See RFC 1831: "RPC: Remote Procedure Call Protocol Specification Version 2",
 * particularly the section on Record Marking Standard.
 */
typedef struct rpcsvc_record_state {

        /* Pending messages storage
         * This memory area is currently being used to assemble
         * the latest RPC record.
         *
         * Note that this buffer contains the data other than the
         * fragment headers received from the network. This is so that we can
         * directly pass this buffer to higher layers without requiring to
         * perform memory copies and marshalling of data.
         */
        struct iobuf            *activeiob;

        struct iobuf            *vectoriob;
        /* The pointer into activeiob memory, into which will go the
         * contents from the next read from the network.
         */
        char                    *fragcurrent;

        /* Size of the currently incomplete RPC fragment.
         * This is filled in when the fragment header comes in.
         * Even though only the 31 least significant bits are used from the
         * fragment header, we use a 32 bit variable to store the size.
         */
        uint32_t               fragsize;

        /* The fragment header is always read in here so that
         * the RPC messages contained in a RPC records can be processed
         * separately without copying them out of the activeiob above.
         */
        char                    fragheader[RPCSVC_FRAGHDR_SIZE];
        char                    *hdrcurrent;

        /* Bytes remaining to come in for the current fragment. */
        uint32_t                remainingfrag;

        /* It is possible for the frag header to be split over separate
         * read calls, so we need to keep track of how much is left.
         */
        uint32_t                remainingfraghdr;

        /* Record size, the total size of the RPC record, i.e. the total
         * of all fragment sizes received till now. Does not include the size
         * of a partial fragment which is continuing to be assembled right now.
         */
        int                     recordsize;

        /* Current state of the record */
        int                     state;

        /* Current state of the vectored reading process. */
        int                     vecstate;

        /* Set to non-zero when the currently partial or complete fragment is
         * the last fragment being received for the current RPC record.
         */
        uint32_t                islastfrag;

} rpcsvc_record_state_t;


#define RPCSVC_CONNSTATE_CONNECTED      1
#define RPCSVC_CONNSTATE_DISCONNECTED   2

#define nfs_rpcsvc_conn_check_active(conn) ((conn)->connstate==RPCSVC_CONNSTATE_CONNECTED)

typedef struct rpcsvc_request rpcsvc_request_t;
/* Contains the state for each connection that is used for transmitting and
 * receiving RPC messages.
 *
 * There is also an eventidx because each connection's fd is added to the event
 * pool of the stage to which a connection belongs.
 * Anything that can be accessed by a RPC program must be synced through
 * connlock.
 */
typedef struct rpc_conn_state {

        /* Transport or connection state */

         /* Once we start working on RDMA support, this TCP specific state will
         * have to be abstracted away.
         */
        int                     sockfd;
        int                     eventidx;
        int                     windowsize;

        /* Reference to the stage which is handling this
         * connection.
         */
        rpcsvc_stage_t          *stage;

        /* RPC Records and Fragments assembly state.
         * All incoming data is staged here before being
         * called a full RPC message.
         */
        rpcsvc_record_state_t   rstate;

         /* It is possible that a client disconnects while
         * the higher layer RPC service is busy in a call.
         * In this case, we cannot just free the conn
         * structure, since the higher layer service could
         * still have a reference to it.
         * The refcount avoids freeing until all references
         * have been given up, although the connection is clos()ed at the first
         * call to unref.
         */
        int                     connref;
        pthread_mutex_t         connlock;
        int                     connstate;

        /* List of buffers awaiting transmission */
        /* Accesses to txbufs between multiple threads calling
         * rpcsvc_submit is synced through connlock. Prefer spinlock over
         * mutex because this is a low overhead op that needs simple
         * appending to the tx list.
         */
        struct list_head        txbufs;

        /* Mem pool for the txbufs above. */
        struct mem_pool         *txpool;

        /* Memory pool for rpcsvc_request_t */
        struct mem_pool         *rxpool;

        /* The request which hasnt yet been handed to the RPC program because
         * this request is being treated as a vector request and so needs some
         * more data to be got from the network.
         */
        rpcsvc_request_t        *vectoredreq;
} rpcsvc_conn_t;


#define RPCSVC_MAX_AUTH_BYTES   400
typedef struct rpcsvc_auth_data {
        int             flavour;
        int             datalen;
        char            authdata[RPCSVC_MAX_AUTH_BYTES];
} rpcsvc_auth_data_t;

#define nfs_rpcsvc_auth_flavour(au)    ((au).flavour)

/* The container for the RPC call handed up to an actor.
 * Dynamically allocated. Lives till the call reply is completely
 * transmitted.
 * */
struct rpcsvc_request {
        /* Connection over which this request came. */
        rpcsvc_conn_t           *conn;

        /* The identifier for the call from client.
         * Needed to pair the reply with the call.
         */
        uint32_t                xid;

        int                     prognum;

        int                     progver;

        int                     procnum;
        /* Uid and gid filled by the rpc-auth module during the authentication
         * phase.
         */
        uid_t                   uid;
        gid_t                   gid;

        /* Might want to move this to AUTH_UNIX specifix state since this array
         * is not available for every authenticatino scheme.
         */
        gid_t                   auxgids[NGRPS];
        int                     auxgidcount;


        /* The RPC message payload, contains the data required
         * by the program actors. This is the buffer that will need to
         * be de-xdred by the actor.
         */
        struct iovec            msg;

        /* The full message buffer allocated to store the RPC headers.
         * This buffer is ref'd when allocated why RPC svc and unref'd after
         * the buffer is handed to the actor. That means if the actor or any
         * higher layer wants to keep this buffer around, they too must ref it
         * right after entering the program actor.
         */
        struct iobuf            *recordiob;

        /* Status of the RPC call, whether it was accepted or denied. */
        int                     rpc_stat;

        /* In case, the call was denied, the RPC error is stored here
         * till the reply is sent.
         */
        int                     rpc_err;

        /* In case the failure happened because of an authentication problem
         * , this value needs to be assigned the correct auth error number.
         */
        int                     auth_err;

        /* There can be cases of RPC requests where the reply needs to
         * be built from multiple sources. For eg. where even the NFS reply can
         * contain a payload, as in the NFSv3 read reply. Here the RPC header
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

        /* Container for a RPC program wanting to store a temp
         * request-specific item.
         */
        void                    *private;

        /* To save a ref to the program for which this request is. */
        rpcsvc_program_t        *program;
};

#define nfs_rpcsvc_request_program(req) ((rpcsvc_program_t *)((req)->program))
#define nfs_rpcsvc_request_program_private(req) ((req)->program->private)
#define nfs_rpcsvc_request_conn(req)        (req)->conn
#define nfs_rpcsvc_program_xlator(prg)      ((prg)->actorxl)
#define nfs_rpcsvc_request_actorxl(rq)      (nfs_rpcsvc_request_program(rq))->actorxl
#define nfs_rpcsvc_request_accepted(req)    ((req)->rpc_stat == MSG_ACCEPTED)
#define nfs_rpcsvc_request_accepted_success(req) ((req)->rpc_err == SUCCESS)
#define nfs_rpcsvc_request_uid(req)         ((req)->uid)
#define nfs_rpcsvc_request_gid(req)         ((req)->gid)
#define nfs_rpcsvc_stage_service(stg)       ((rpcsvc_t *)((stg)->svc))
#define nfs_rpcsvc_conn_stage(conn)         ((conn)->stage)
#define nfs_rpcsvc_request_service(req)     (nfs_rpcsvc_stage_service(nfs_rpcsvc_conn_stage(nfs_rpcsvc_request_conn(req))))
#define nfs_rpcsvc_request_prog_minauth(req) (nfs_rpcsvc_request_program(req)->min_auth)
#define nfs_rpcsvc_request_cred_flavour(req) (nfs_rpcsvc_auth_flavour(req->cred))
#define nfs_rpcsvc_request_verf_flavour(req) (nfs_rpcsvc_auth_flavour(req->verf))

#define nfs_rpcsvc_request_uid(req)         ((req)->uid)
#define nfs_rpcsvc_request_gid(req)         ((req)->gid)
#define nfs_rpcsvc_request_private(req)     ((req)->private)
#define nfs_rpcsvc_request_xid(req)         ((req)->xid)
#define nfs_rpcsvc_request_set_private(req,prv)  (req)->private = (void *)(prv)
#define nfs_rpcsvc_request_record_iob(rq)   ((rq)->recordiob)
#define nfs_rpcsvc_request_record_ref(req)  (iobuf_ref ((req)->recordiob))
#define nfs_rpcsvc_request_record_unref(req) (iobuf_unref ((req)->recordiob))
#define nfs_rpcsvc_request_procnum(rq)      ((rq)->procnum)


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
typedef int (*rpcsvc_vector_actor) (rpcsvc_request_t *req, struct iobuf *iob);
typedef int (*rpcsvc_vector_sizer) (rpcsvc_request_t *req, ssize_t *readsize,
                                    int *newiob);

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
typedef struct rpc_svc_actor_desc {
        char                    procname[RPCSVC_NAME_MAX];
        int                     procnum;
        rpcsvc_actor            actor;

        /* Handler for cases where the RPC requests fragments are large enough
         * to benefit from being decoded into aligned memory addresses. While
         * decoding the request in a non-vectored manner, due to the nature of
         * the XDR scheme, RPC cannot guarantee memory aligned addresses for
         * the resulting message-specific structures. Allowing a specialized
         * handler for letting the RPC program read the data from the network
         * directly into its alligned buffers.
         */
        rpcsvc_vector_actor     vector_actor;
        rpcsvc_vector_sizer     vector_sizer;

} rpcsvc_actor_t;

/* Describes a program and its version along with the function pointers
 * required to handle the procedures/actors of each program/version.
 * Never changed ever by any thread so no need for a lock.
 */
struct rpc_svc_program {
        struct list_head        proglist;
        char                    progname[RPCSVC_NAME_MAX];
        int                     prognum;
        int                     progver;
        uint16_t                progport;       /* Registered with portmap */
        int                     progaddrfamily; /* AF_INET or AF_INET6 */
        char                    *proghost;      /* Bind host, can be NULL */
        rpcsvc_actor_t          *actors;        /* All procedure handlers */
        int                     numactors;      /* Num actors in actor array */
        int                     proghighvers;   /* Highest ver for program
                                                   supported by the system. */
        int                     proglowvers;    /* Lowest ver */

        /* Program specific state handed to actors */
        void                    *private;

        /* An integer that identifies the min auth strength that is required
         * by this protocol, for eg. MOUNT3 needs AUTH_UNIX at least.
         * See RFC 1813, Section 5.2.1.
         */
        int                     min_auth;

        /* The translator in whose context the actor must execute. This is
         * needed to setup THIS for memory accounting to work correctly.
         */
        xlator_t                *actorxl;
};


/* Contains global state required for all the RPC services.
 */
typedef struct rpc_svc_state {

        /* Contains the list of rpcsvc_stage_t
         * list of (program, version) handlers.
         * other options.
         */

        /* At this point, lock is not used to protect anything. Later, it'll
         * be used for protecting stages.
         */
        pthread_mutex_t         rpclock;

        /* This is the first stage that is inited, so that any RPC based
         * services that do not need multi-threaded support can just use the
         * service right away. This is not added to the stages list
         * declared later.
         * This is also the stage over which all service listeners are run.
         */
        rpcsvc_stage_t          *defaultstage;

        /* When we have multi-threaded RPC support, we'll use this to link
         * to the multiple Stages.
         */
        struct list_head        stages;         /* All stages */

        unsigned int            memfactor;

        /* List of the authentication schemes available. */
        struct list_head        authschemes;

        /* Reference to the options */
        dict_t                  *options;

        /* Allow insecure ports. */
        int                     allow_insecure;

        glusterfs_ctx_t         *ctx;

        gf_boolean_t            register_portmap;

        struct list_head        allprograms;

        /* Mempool for incoming connection objects. */
        struct mem_pool         *connpool;
} rpcsvc_t;


/* All users of RPC services should use this API to register their
 * procedure handlers.
 */
extern int
nfs_rpcsvc_program_register (rpcsvc_t *svc, rpcsvc_program_t program);

extern int
nfs_rpcsvc_program_unregister (rpcsvc_t *svc, rpcsvc_program_t program);

/* Inits the global RPC service data structures.
 * Called in main.
 */
extern rpcsvc_t *
nfs_rpcsvc_init (glusterfs_ctx_t *ctx, dict_t *options);


extern int
nfs_rpcsvc_submit_message (rpcsvc_request_t * req, struct iovec msg,
                           struct iobuf *iob);

int
nfs_rpcsvc_submit_generic (rpcsvc_request_t *req, struct iovec msgvec,
                           struct iobuf *msg);
#define nfs_rpcsvc_record_currentfrag_addr(rs) ((rs)->fragcurrent)
#define nfs_rpcsvc_record_currenthdr_addr(rs) ((rs)->hdrcurrent)

#define nfs_rpcsvc_record_update_currentfrag(rs, size)          \
                        do {                                    \
                                (rs)->fragcurrent += size;      \
                        } while (0)                             \

#define nfs_rpcsvc_record_update_currenthdr(rs, size)           \
                        do {                                    \
                                (rs)->hdrcurrent += size;       \
                        } while (0)                             \


/* These are used to differentiate between multiple txbufs which form
 * a single RPC record. For eg, one purpose we use these for is to
 * prevent dividing a RPC record over multiple TCP segments. Multiple
 * TCP segments are possible for a single RPC record because we generally do not
 * have control over how the kernel's TCP segments the buffers when putting
 * them on the wire. So, on Linux, we use these to set TCP_CORK to create
 * a single TCP segment from multiple txbufs that are part of the same RPC
 * record. This improves network performance by reducing tiny message
 * transmissions.
 */
#define RPCSVC_TXB_FIRST        0x1
#define RPCSVC_TXB_LAST         0x2

/* The list of buffers appended to a connection's pending
 * transmission list.
 */
typedef struct rpcsvc_txbuf {
        struct list_head        txlist;
        /* The iobuf which contains the full message to be transmitted */
        struct iobuf            *iob;

        /* For vectored messages from an RPC program, we need to be able
         * maintain a ref to an iobuf which we do not have access to directly
         * except through the iobref which in turn could've been passed to
         * the RPC program by a higher layer.
         *
         * So either the iob is defined or iobref is defined for a reply,
         * never both.
         */
        struct iobref           *iobref;
        /* In order to handle non-blocking writes, we'll need to keep track of
         * how much data from an iobuf has been written and where the next
         * transmission needs to start from. This iov.base points to the base of
         * the iobuf, iov.len is the size of iobuf being used for the message
         * from the total size in the iobuf.
         */
        struct iovec            buf;
        /* offset is the point from where the next transmission for this buffer
         * should start.
         */
        size_t                  offset;

        /* This is a special field that tells us what kind of transmission
         * behaviour to provide to a particular buffer.
         * See the RPCSVC_TXB_* defines for more info.
         */
        int                     txbehave;
} rpcsvc_txbuf_t;

extern int
nfs_rpcsvc_error_reply (rpcsvc_request_t *req);

#define RPCSVC_PEER_STRLEN      1024
#define RPCSVC_AUTH_ACCEPT      1
#define RPCSVC_AUTH_REJECT      2
#define RPCSVC_AUTH_DONTCARE    3

extern int
nfs_rpcsvc_conn_peername (rpcsvc_conn_t *conn, char *hostname, int hostlen);

extern int
nfs_rpcsvc_conn_peeraddr (rpcsvc_conn_t *conn, char *addrstr, int addrlen,
                          struct sockaddr *returnsa, socklen_t sasize);

extern int
nfs_rpcsvc_conn_peer_check (dict_t *options, char *volname,rpcsvc_conn_t *conn);

extern int
nfs_rpcsvc_conn_privport_check (rpcsvc_t *svc, char *volname,
                                rpcsvc_conn_t *conn);
#define nfs_rpcsvc_request_seterr(req, err)                 (req)->rpc_err = err
#define nfs_rpcsvc_request_set_autherr(req, err)                        \
        do {                                                            \
                (req)->auth_err = err;                                  \
                (req)->rpc_stat = MSG_DENIED;                           \
        } while (0)                                                     \

extern void
nfs_rpcsvc_conn_deinit (rpcsvc_conn_t *conn);
extern void nfs_rpcsvc_conn_ref (rpcsvc_conn_t *conn);
extern void nfs_rpcsvc_conn_unref (rpcsvc_conn_t *conn);

extern int nfs_rpcsvc_submit_vectors (rpcsvc_request_t *req);

extern int nfs_rpcsvc_request_attach_vector (rpcsvc_request_t *req,
                                             struct iovec msgvec,
                                             struct iobuf *iob,
                                             struct iobref *ioref,
                                             int finalvector);
extern int
nfs_rpcsvc_request_attach_vectors (rpcsvc_request_t *req, struct iovec *payload,
                                   int vcount, struct iobref *piobref);

typedef int (*auth_init_conn) (rpcsvc_conn_t *conn, void *priv);
typedef int (*auth_init_request) (rpcsvc_request_t *req, void *priv);
typedef int (*auth_request_authenticate) (rpcsvc_request_t *req, void *priv);

/* This structure needs to be registered by every authentication scheme.
 * Our authentication schemes are stored per connection because
 * each connection will end up using a different authentication scheme.
 */
typedef struct rpcsvc_auth_ops {
        auth_init_conn                conn_init;
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
nfs_rpcsvc_auth_request_init (rpcsvc_request_t *req);

extern int
nfs_rpcsvc_auth_init (rpcsvc_t *svc, dict_t *options);

extern int
nfs_rpcsvc_auth_conn_init (rpcsvc_conn_t *conn);

extern int
nfs_rpcsvc_authenticate (rpcsvc_request_t *req);

extern int
nfs_rpcsvc_auth_array (rpcsvc_t *svc, char *volname, int *autharr, int arrlen);

/* If the request has been sent using AUTH_UNIX, this function returns the
 * auxiliary gids as an array, otherwise, it returns NULL.
 * Move to auth-unix specific source file when we need to modularize the
 * authentication code even further to support mode auth schemes.
 */
extern gid_t *
nfs_rpcsvc_auth_unix_auxgids (rpcsvc_request_t *req, int *arrlen);

extern int
nfs_rpcsvc_combine_gen_spec_volume_checks (int gen, int spec);

extern char *
nfs_rpcsvc_volume_allowed (dict_t *options, char *volname);
#endif
