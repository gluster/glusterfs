/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdint.h>

#include "compat-errno.h"


static int32_t gf_error_to_errno_array[1024];
static int32_t gf_errno_to_error_array[1024];

static int32_t gf_compat_errno_init_done;

#ifdef GF_SOLARIS_HOST_OS
static void
init_compat_errno_arrays ()
{
/*      ENOMSG  35      / * No message of desired type          */
        gf_error_to_errno_array[GF_ERROR_CODE_NOMSG] = ENOMSG;
        gf_errno_to_error_array[ENOMSG] = GF_ERROR_CODE_NOMSG;

/*      EIDRM   36      / * Identifier removed                  */
        gf_error_to_errno_array[GF_ERROR_CODE_IDRM] = EIDRM;
        gf_errno_to_error_array[EIDRM] = GF_ERROR_CODE_IDRM;

/*      ECHRNG  37      / * Channel number out of range         */
        gf_error_to_errno_array[GF_ERROR_CODE_CHRNG] = ECHRNG;
        gf_errno_to_error_array[ECHRNG] = GF_ERROR_CODE_CHRNG;

/*      EL2NSYNC 38     / * Level 2 not synchronized            */
        gf_error_to_errno_array[GF_ERROR_CODE_L2NSYNC] = EL2NSYNC;
        gf_errno_to_error_array[EL2NSYNC] = GF_ERROR_CODE_L2NSYNC;

/*      EL3HLT  39      / * Level 3 halted                      */
        gf_error_to_errno_array[GF_ERROR_CODE_L3HLT] = EL3HLT;
        gf_errno_to_error_array[EL3HLT] = GF_ERROR_CODE_L3HLT;

/*      EL3RST  40      / * Level 3 reset                       */
        gf_error_to_errno_array[GF_ERROR_CODE_L3RST] = EL3RST;
        gf_errno_to_error_array[EL3RST] = GF_ERROR_CODE_L3RST;

/*      ELNRNG  41      / * Link number out of range            */
        gf_error_to_errno_array[GF_ERROR_CODE_LNRNG] = ELNRNG;
        gf_errno_to_error_array[ELNRNG] = GF_ERROR_CODE_LNRNG;

/*      EUNATCH 42      / * Protocol driver not attached                */
        gf_error_to_errno_array[GF_ERROR_CODE_UNATCH] = EUNATCH;
        gf_errno_to_error_array[EUNATCH] = GF_ERROR_CODE_UNATCH;

/*      ENOCSI  43      / * No CSI structure available          */
        gf_error_to_errno_array[GF_ERROR_CODE_NOCSI] = ENOCSI;
        gf_errno_to_error_array[ENOCSI] = GF_ERROR_CODE_NOCSI;

/*      EL2HLT  44      / * Level 2 halted                      */
        gf_error_to_errno_array[GF_ERROR_CODE_L2HLT] = EL2HLT;
        gf_errno_to_error_array[EL2HLT] = GF_ERROR_CODE_L2HLT;

/*      EDEADLK 45      / * Deadlock condition.                 */
        gf_error_to_errno_array[GF_ERROR_CODE_DEADLK] = EDEADLK;
        gf_errno_to_error_array[EDEADLK] = GF_ERROR_CODE_DEADLK;

/*      ENOLCK  46      / * No record locks available.          */
        gf_error_to_errno_array[GF_ERROR_CODE_NOLCK] = ENOLCK;
        gf_errno_to_error_array[ENOLCK] = GF_ERROR_CODE_NOLCK;

/*      ECANCELED 47    / * Operation canceled                  */
        gf_error_to_errno_array[GF_ERROR_CODE_CANCELED] = ECANCELED;
        gf_errno_to_error_array[ECANCELED] = GF_ERROR_CODE_CANCELED;

/*      ENOTSUP 48      / * Operation not supported             */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTSUPP] = ENOTSUP;
        gf_errno_to_error_array[ENOTSUP] = GF_ERROR_CODE_NOTSUPP;

/* Filesystem Quotas */
/*      EDQUOT  49      / * Disc quota exceeded                 */
        gf_error_to_errno_array[GF_ERROR_CODE_DQUOT] = EDQUOT;
        gf_errno_to_error_array[EDQUOT] = GF_ERROR_CODE_DQUOT;

/* Convergent Error Returns */
/*      EBADE   50      / * invalid exchange                    */
        gf_error_to_errno_array[GF_ERROR_CODE_BADE] = EBADE;
        gf_errno_to_error_array[EBADE] = GF_ERROR_CODE_BADE;
/*      EBADR   51      / * invalid request descriptor          */
        gf_error_to_errno_array[GF_ERROR_CODE_BADR] = EBADR;
        gf_errno_to_error_array[EBADR] = GF_ERROR_CODE_BADR;
/*      EXFULL  52      / * exchange full                       */
        gf_error_to_errno_array[GF_ERROR_CODE_XFULL] = EXFULL;
        gf_errno_to_error_array[EXFULL] = GF_ERROR_CODE_XFULL;
/*      ENOANO  53      / * no anode                            */
        gf_error_to_errno_array[GF_ERROR_CODE_NOANO] = ENOANO;
        gf_errno_to_error_array[ENOANO] = GF_ERROR_CODE_NOANO;
/*      EBADRQC 54      / * invalid request code                        */
        gf_error_to_errno_array[GF_ERROR_CODE_BADRQC] = EBADRQC;
        gf_errno_to_error_array[EBADRQC] = GF_ERROR_CODE_BADRQC;
/*      EBADSLT 55      / * invalid slot                                */
        gf_error_to_errno_array[GF_ERROR_CODE_BADSLT] = EBADSLT;
        gf_errno_to_error_array[EBADSLT] = GF_ERROR_CODE_BADSLT;
/*      EDEADLOCK 56    / * file locking deadlock error         */
/* This is same as EDEADLK on linux */
        gf_error_to_errno_array[GF_ERROR_CODE_DEADLK] = EDEADLOCK;
        gf_errno_to_error_array[EDEADLOCK] = GF_ERROR_CODE_DEADLK;

/*      EBFONT  57      / * bad font file fmt                   */
        gf_error_to_errno_array[GF_ERROR_CODE_BFONT] = EBFONT;
        gf_errno_to_error_array[EBFONT] = GF_ERROR_CODE_BFONT;

/* Interprocess Robust Locks */
/*      EOWNERDEAD      58      / * process died with the lock */
        gf_error_to_errno_array[GF_ERROR_CODE_OWNERDEAD] = EOWNERDEAD;
        gf_errno_to_error_array[EOWNERDEAD] = GF_ERROR_CODE_OWNERDEAD;
/*      ENOTRECOVERABLE 59      / * lock is not recoverable */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTRECOVERABLE] = ENOTRECOVERABLE;
        gf_errno_to_error_array[ENOTRECOVERABLE] = GF_ERROR_CODE_NOTRECOVERABLE;

/* stream problems */
/*      ENOSTR  60      / * Device not a stream                 */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSTR] = ENOSTR;
        gf_errno_to_error_array[ENOSTR] = GF_ERROR_CODE_NOSTR;
/*      ENODATA 61      / * no data (for no delay io)           */
        gf_error_to_errno_array[GF_ERROR_CODE_NODATA] = ENODATA;
        gf_errno_to_error_array[ENODATA] = GF_ERROR_CODE_NODATA;
/*      ETIME   62      / * timer expired                       */
        gf_error_to_errno_array[GF_ERROR_CODE_TIME] = ETIME;
        gf_errno_to_error_array[ETIME] = GF_ERROR_CODE_TIME;
/*      ENOSR   63      / * out of streams resources            */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSR] = ENOSR;
        gf_errno_to_error_array[ENOSR] = GF_ERROR_CODE_NOSR;

/*      ENONET  64      / * Machine is not on the network       */
        gf_error_to_errno_array[GF_ERROR_CODE_NONET] = ENONET;
        gf_errno_to_error_array[ENONET] = GF_ERROR_CODE_NONET;
/*      ENOPKG  65      / * Package not installed               */
        gf_error_to_errno_array[GF_ERROR_CODE_NOPKG] = ENOPKG;
        gf_errno_to_error_array[ENOPKG] = GF_ERROR_CODE_NOPKG;
/*      EREMOTE 66      / * The object is remote                        */
        gf_error_to_errno_array[GF_ERROR_CODE_REMOTE] = EREMOTE;
        gf_errno_to_error_array[EREMOTE] = GF_ERROR_CODE_REMOTE;
/*      ENOLINK 67      / * the link has been severed           */
        gf_error_to_errno_array[GF_ERROR_CODE_NOLINK] = ENOLINK;
        gf_errno_to_error_array[ENOLINK] = GF_ERROR_CODE_NOLINK;
/*      EADV    68      / * advertise error                     */
        gf_error_to_errno_array[GF_ERROR_CODE_ADV] = EADV;
        gf_errno_to_error_array[EADV] = GF_ERROR_CODE_ADV;
/*      ESRMNT  69      / * srmount error                       */
        gf_error_to_errno_array[GF_ERROR_CODE_SRMNT] = ESRMNT;
        gf_errno_to_error_array[ESRMNT] = GF_ERROR_CODE_SRMNT;

/*      ECOMM   70      / * Communication error on send         */
        gf_error_to_errno_array[GF_ERROR_CODE_COMM] = ECOMM;
        gf_errno_to_error_array[ECOMM] = GF_ERROR_CODE_COMM;
/*      EPROTO  71      / * Protocol error                      */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTO] = EPROTO;
        gf_errno_to_error_array[EPROTO] = GF_ERROR_CODE_PROTO;

/* Interprocess Robust Locks */
/*      ELOCKUNMAPPED   72      / * locked lock was unmapped */
        gf_error_to_errno_array[GF_ERROR_CODE_LOCKUNMAPPED] = ELOCKUNMAPPED;
        gf_errno_to_error_array[ELOCKUNMAPPED] = GF_ERROR_CODE_LOCKUNMAPPED;

/*      ENOTACTIVE 73   / * Facility is not active              */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTACTIVE] = ENOTACTIVE;
        gf_errno_to_error_array[ENOTACTIVE] = GF_ERROR_CODE_NOTACTIVE;
/*      EMULTIHOP 74    / * multihop attempted                  */
        gf_error_to_errno_array[GF_ERROR_CODE_MULTIHOP] = EMULTIHOP;
        gf_errno_to_error_array[EMULTIHOP] = GF_ERROR_CODE_MULTIHOP;
/*      EBADMSG 77      / * trying to read unreadable message   */
        gf_error_to_errno_array[GF_ERROR_CODE_BADMSG] = EBADMSG;
        gf_errno_to_error_array[EBADMSG] = GF_ERROR_CODE_BADMSG;
/*      ENAMETOOLONG 78 / * path name is too long               */
        gf_error_to_errno_array[GF_ERROR_CODE_NAMETOOLONG] = ENAMETOOLONG;
        gf_errno_to_error_array[ENAMETOOLONG] = GF_ERROR_CODE_NAMETOOLONG;
/*      EOVERFLOW 79    / * value too large to be stored in data type */
        gf_error_to_errno_array[GF_ERROR_CODE_OVERFLOW] = EOVERFLOW;
        gf_errno_to_error_array[EOVERFLOW] = GF_ERROR_CODE_OVERFLOW;
/*      ENOTUNIQ 80     / * given log. name not unique          */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTUNIQ] = ENOTUNIQ;
        gf_errno_to_error_array[ENOTUNIQ] = GF_ERROR_CODE_NOTUNIQ;
/*      EBADFD  81      / * f.d. invalid for this operation     */
        gf_error_to_errno_array[GF_ERROR_CODE_BADFD] = EBADFD;
        gf_errno_to_error_array[EBADFD] = GF_ERROR_CODE_BADFD;
/*      EREMCHG 82      / * Remote address changed              */
        gf_error_to_errno_array[GF_ERROR_CODE_REMCHG] = EREMCHG;
        gf_errno_to_error_array[EREMCHG] = GF_ERROR_CODE_REMCHG;

/* shared library problems */
/*      ELIBACC 83      / * Can't access a needed shared lib.   */
        gf_error_to_errno_array[GF_ERROR_CODE_LIBACC] = ELIBACC;
        gf_errno_to_error_array[ELIBACC] = GF_ERROR_CODE_LIBACC;
/*      ELIBBAD 84      / * Accessing a corrupted shared lib.   */
        gf_error_to_errno_array[GF_ERROR_CODE_LIBBAD] = ELIBBAD;
        gf_errno_to_error_array[ELIBBAD] = GF_ERROR_CODE_LIBBAD;
/*      ELIBSCN 85      / * .lib section in a.out corrupted.    */
        gf_error_to_errno_array[GF_ERROR_CODE_LIBSCN] = ELIBSCN;
        gf_errno_to_error_array[ELIBSCN] = GF_ERROR_CODE_LIBSCN;
/*      ELIBMAX 86      / * Attempting to link in too many libs.        */
        gf_error_to_errno_array[GF_ERROR_CODE_LIBMAX] = ELIBMAX;
        gf_errno_to_error_array[ELIBMAX] = GF_ERROR_CODE_LIBMAX;
/*      ELIBEXEC 87     / * Attempting to exec a shared library.        */
        gf_error_to_errno_array[GF_ERROR_CODE_LIBEXEC] = ELIBEXEC;
        gf_errno_to_error_array[ELIBEXEC] = GF_ERROR_CODE_LIBEXEC;
/*      EILSEQ  88      / * Illegal byte sequence.              */
        gf_error_to_errno_array[GF_ERROR_CODE_ILSEQ] = EILSEQ;
        gf_errno_to_error_array[EILSEQ] = GF_ERROR_CODE_ILSEQ;
/*      ENOSYS  89      / * Unsupported file system operation   */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSYS] = ENOSYS;
        gf_errno_to_error_array[ENOSYS] = GF_ERROR_CODE_NOSYS;
/*      ELOOP   90      / * Symbolic link loop                  */
        gf_error_to_errno_array[GF_ERROR_CODE_LOOP] = ELOOP;
        gf_errno_to_error_array[ELOOP] = GF_ERROR_CODE_LOOP;
/*      ERESTART 91     / * Restartable system call             */
        gf_error_to_errno_array[GF_ERROR_CODE_RESTART] = ERESTART;
        gf_errno_to_error_array[ERESTART] = GF_ERROR_CODE_RESTART;
/*      ESTRPIPE 92     / * if pipe/FIFO, don't sleep in stream head */
        gf_error_to_errno_array[GF_ERROR_CODE_STRPIPE] = ESTRPIPE;
        gf_errno_to_error_array[ESTRPIPE] = GF_ERROR_CODE_STRPIPE;
/*      ENOTEMPTY 93    / * directory not empty                 */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTEMPTY] = ENOTEMPTY;
        gf_errno_to_error_array[ENOTEMPTY] = GF_ERROR_CODE_NOTEMPTY;
/*      EUSERS  94      / * Too many users (for UFS)            */
        gf_error_to_errno_array[GF_ERROR_CODE_USERS] = EUSERS;
        gf_errno_to_error_array[EUSERS] = GF_ERROR_CODE_USERS;

/* BSD Networking Software */
        /* argument errors */
/*      ENOTSOCK        95      / * Socket operation on non-socket */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTSOCK] = ENOTSOCK;
        gf_errno_to_error_array[ENOTSOCK] = GF_ERROR_CODE_NOTSOCK;
/*      EDESTADDRREQ    96      / * Destination address required */
        gf_error_to_errno_array[GF_ERROR_CODE_DESTADDRREQ] = EDESTADDRREQ;
        gf_errno_to_error_array[EDESTADDRREQ] = GF_ERROR_CODE_DESTADDRREQ;
/*      EMSGSIZE        97      / * Message too long */
        gf_error_to_errno_array[GF_ERROR_CODE_MSGSIZE] = EMSGSIZE;
        gf_errno_to_error_array[EMSGSIZE] = GF_ERROR_CODE_MSGSIZE;
/*      EPROTOTYPE      98      / * Protocol wrong type for socket */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTOTYPE] = EPROTOTYPE;
        gf_errno_to_error_array[EPROTOTYPE] = GF_ERROR_CODE_PROTOTYPE;
/*      ENOPROTOOPT     99      / * Protocol not available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOPROTOOPT] = ENOPROTOOPT;
        gf_errno_to_error_array[ENOPROTOOPT] = GF_ERROR_CODE_NOPROTOOPT;
/*      EPROTONOSUPPORT 120     / * Protocol not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTONOSUPPORT] = EPROTONOSUPPORT;
        gf_errno_to_error_array[EPROTONOSUPPORT] = GF_ERROR_CODE_PROTONOSUPPORT;
/*      ESOCKTNOSUPPORT 121     / * Socket type not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_SOCKTNOSUPPORT] = ESOCKTNOSUPPORT;
        gf_errno_to_error_array[ESOCKTNOSUPPORT] = GF_ERROR_CODE_SOCKTNOSUPPORT;

/*      EOPNOTSUPP      122     / * Operation not supported on socket */
        gf_error_to_errno_array[GF_ERROR_CODE_OPNOTSUPP] = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;
/*      EPFNOSUPPORT    123     / * Protocol family not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_PFNOSUPPORT] = EPFNOSUPPORT;
        gf_errno_to_error_array[EPFNOSUPPORT] = GF_ERROR_CODE_PFNOSUPPORT;
/*      EAFNOSUPPORT    124     / * Address family not supported by */
        /* protocol family */
        gf_error_to_errno_array[GF_ERROR_CODE_AFNOSUPPORT] = EAFNOSUPPORT;
        gf_errno_to_error_array[EAFNOSUPPORT] = GF_ERROR_CODE_AFNOSUPPORT;
/*      EADDRINUSE      125     / * Address already in use */
        gf_error_to_errno_array[GF_ERROR_CODE_ADDRINUSE] = EADDRINUSE;
        gf_errno_to_error_array[EADDRINUSE] = GF_ERROR_CODE_ADDRINUSE;
/*      EADDRNOTAVAIL   126     / * Can't assign requested address */
        /* operational errors */
        gf_error_to_errno_array[GF_ERROR_CODE_ADDRNOTAVAIL] = EADDRNOTAVAIL;
        gf_errno_to_error_array[EADDRNOTAVAIL] = GF_ERROR_CODE_ADDRNOTAVAIL;
/*      ENETDOWN        127     / * Network is down */
        gf_error_to_errno_array[GF_ERROR_CODE_NETDOWN] = ENETDOWN;
        gf_errno_to_error_array[ENETDOWN] = GF_ERROR_CODE_NETDOWN;
/*      ENETUNREACH     128     / * Network is unreachable */
        gf_error_to_errno_array[GF_ERROR_CODE_NETUNREACH] = ENETUNREACH;
        gf_errno_to_error_array[ENETUNREACH] = GF_ERROR_CODE_NETUNREACH;
/*      ENETRESET       129     / * Network dropped connection because */
        /* of reset */
        gf_error_to_errno_array[GF_ERROR_CODE_NETRESET] = ENETRESET;
        gf_errno_to_error_array[ENETRESET] = GF_ERROR_CODE_NETRESET;
/*      ECONNABORTED    130     / * Software caused connection abort */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNABORTED] = ECONNABORTED;
        gf_errno_to_error_array[ECONNABORTED] = GF_ERROR_CODE_CONNABORTED;
/*      ECONNRESET      131     / * Connection reset by peer */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNRESET] = ECONNRESET;
        gf_errno_to_error_array[ECONNRESET] = GF_ERROR_CODE_CONNRESET;
/*      ENOBUFS         132     / * No buffer space available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOBUFS] = ENOBUFS;
        gf_errno_to_error_array[ENOBUFS] = GF_ERROR_CODE_NOBUFS;
/*      EISCONN         133     / * Socket is already connected */
        gf_error_to_errno_array[GF_ERROR_CODE_ISCONN] = EISCONN;
        gf_errno_to_error_array[EISCONN] = GF_ERROR_CODE_ISCONN;
/*      ENOTCONN        134     / * Socket is not connected */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTCONN] = ENOTCONN;
        gf_errno_to_error_array[ENOTCONN] = GF_ERROR_CODE_NOTCONN;
/* XENIX has 135 - 142 */
/*      ESHUTDOWN       143     / * Can't send after socket shutdown */
        gf_error_to_errno_array[GF_ERROR_CODE_SHUTDOWN] = ESHUTDOWN;
        gf_errno_to_error_array[ESHUTDOWN] = GF_ERROR_CODE_SHUTDOWN;
/*      ETOOMANYREFS    144     / * Too many references: can't splice */
        gf_error_to_errno_array[GF_ERROR_CODE_TOOMANYREFS] = ETOOMANYREFS;
        gf_errno_to_error_array[ETOOMANYREFS] = GF_ERROR_CODE_TOOMANYREFS;
/*      ETIMEDOUT       145     / * Connection timed out */
        gf_error_to_errno_array[GF_ERROR_CODE_TIMEDOUT] = ETIMEDOUT;
        gf_errno_to_error_array[ETIMEDOUT] = GF_ERROR_CODE_TIMEDOUT;

/*      ECONNREFUSED    146     / * Connection refused */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNREFUSED] = ECONNREFUSED;
        gf_errno_to_error_array[ECONNREFUSED] = GF_ERROR_CODE_CONNREFUSED;
/*      EHOSTDOWN       147     / * Host is down */
        gf_error_to_errno_array[GF_ERROR_CODE_HOSTDOWN] = EHOSTDOWN;
        gf_errno_to_error_array[EHOSTDOWN] = GF_ERROR_CODE_HOSTDOWN;
/*      EHOSTUNREACH    148     / * No route to host */
        gf_error_to_errno_array[GF_ERROR_CODE_HOSTUNREACH] = EHOSTUNREACH;
        gf_errno_to_error_array[EHOSTUNREACH] = GF_ERROR_CODE_HOSTUNREACH;
/*      EALREADY        149     / * operation already in progress */
        gf_error_to_errno_array[GF_ERROR_CODE_ALREADY] = EALREADY;
        gf_errno_to_error_array[EALREADY] = GF_ERROR_CODE_ALREADY;
/*      EINPROGRESS     150     / * operation now in progress */
        gf_error_to_errno_array[GF_ERROR_CODE_INPROGRESS] = EINPROGRESS;
        gf_errno_to_error_array[EINPROGRESS] = GF_ERROR_CODE_INPROGRESS;

/* SUN Network File System */
/*      ESTALE          151     / * Stale NFS file handle */
        gf_error_to_errno_array[GF_ERROR_CODE_STALE] = ESTALE;
        gf_errno_to_error_array[ESTALE] = GF_ERROR_CODE_STALE;

        return ;
}
#endif /* GF_SOLARIS_HOST_OS */

#ifdef GF_DARWIN_HOST_OS
static void
init_compat_errno_arrays ()
{
        /*    EDEADLK         11              / * Resource deadlock would occur */
        gf_error_to_errno_array[GF_ERROR_CODE_DEADLK] = EDEADLK;
        gf_errno_to_error_array[EDEADLK] = GF_ERROR_CODE_DEADLK;

        /*    EAGAIN          35              / * Try Again */
        gf_error_to_errno_array[GF_ERROR_CODE_AGAIN] = EAGAIN;
        gf_errno_to_error_array[EAGAIN] = GF_ERROR_CODE_AGAIN;

        /*      EINPROGRESS     36              / * Operation now in progress */
        gf_error_to_errno_array[GF_ERROR_CODE_INPROGRESS] = EINPROGRESS;
        gf_errno_to_error_array[EINPROGRESS] = GF_ERROR_CODE_INPROGRESS;

        /*      EALREADY        37              / * Operation already in progress */
        gf_error_to_errno_array[GF_ERROR_CODE_ALREADY] = EALREADY;
        gf_errno_to_error_array[EALREADY] = GF_ERROR_CODE_ALREADY;

        /*      ENOTSOCK        38              / * Socket operation on non-socket */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTSOCK] = ENOTSOCK;
        gf_errno_to_error_array[ENOTSOCK] = GF_ERROR_CODE_NOTSOCK;

        /*      EDESTADDRREQ    39              / * Destination address required */
        gf_error_to_errno_array[GF_ERROR_CODE_DESTADDRREQ] = EDESTADDRREQ;
        gf_errno_to_error_array[EDESTADDRREQ] = GF_ERROR_CODE_DESTADDRREQ;

        /*      EMSGSIZE        40              / * Message too long */
        gf_error_to_errno_array[GF_ERROR_CODE_MSGSIZE] = EMSGSIZE;
        gf_errno_to_error_array[EMSGSIZE] = GF_ERROR_CODE_MSGSIZE;

        /*      EPROTOTYPE      41              / * Protocol wrong type for socket */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTOTYPE] = EPROTOTYPE;
        gf_errno_to_error_array[EPROTOTYPE] = GF_ERROR_CODE_PROTOTYPE;

        /*      ENOPROTOOPT     42              / * Protocol not available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOPROTOOPT] = ENOPROTOOPT;
        gf_errno_to_error_array[ENOPROTOOPT] = GF_ERROR_CODE_NOPROTOOPT;

        /*      EPROTONOSUPPORT 43              / * Protocol not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTONOSUPPORT] = EPROTONOSUPPORT;
        gf_errno_to_error_array[EPROTONOSUPPORT] = GF_ERROR_CODE_PROTONOSUPPORT;

        /*      ESOCKTNOSUPPORT 44              / * Socket type not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_SOCKTNOSUPPORT] = ESOCKTNOSUPPORT;
        gf_errno_to_error_array[ESOCKTNOSUPPORT] = GF_ERROR_CODE_SOCKTNOSUPPORT;

        /*      EOPNOTSUPP      45              / * Operation not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_OPNOTSUPP] = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;

        /*      EPFNOSUPPORT    46              / * Protocol family not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_PFNOSUPPORT] = EPFNOSUPPORT;
        gf_errno_to_error_array[EPFNOSUPPORT] = GF_ERROR_CODE_PFNOSUPPORT;

        /*      EAFNOSUPPORT    47              / * Address family not supported by protocol family */
        gf_error_to_errno_array[GF_ERROR_CODE_AFNOSUPPORT] = EAFNOSUPPORT;
        gf_errno_to_error_array[EAFNOSUPPORT] = GF_ERROR_CODE_AFNOSUPPORT;

        /*      EADDRINUSE      48              / * Address already in use */
        gf_error_to_errno_array[GF_ERROR_CODE_ADDRINUSE] = EADDRINUSE;
        gf_errno_to_error_array[EADDRINUSE] = GF_ERROR_CODE_ADDRINUSE;

        /*      EADDRNOTAVAIL   49              / * Can't assign requested address */
        gf_error_to_errno_array[GF_ERROR_CODE_ADDRNOTAVAIL] = EADDRNOTAVAIL;
        gf_errno_to_error_array[EADDRNOTAVAIL] = GF_ERROR_CODE_ADDRNOTAVAIL;

        /*      ENETDOWN        50              / * Network is down */
        gf_error_to_errno_array[GF_ERROR_CODE_NETDOWN] = ENETDOWN;
        gf_errno_to_error_array[ENETDOWN] = GF_ERROR_CODE_NETDOWN;

        /*      ENETUNREACH     51              / * Network is unreachable */
        gf_error_to_errno_array[GF_ERROR_CODE_NETUNREACH] = ENETUNREACH;
        gf_errno_to_error_array[ENETUNREACH] = GF_ERROR_CODE_NETUNREACH;

        /*      ENETRESET       52              / * Network dropped connection on reset */
        gf_error_to_errno_array[GF_ERROR_CODE_NETRESET] = ENETRESET;
        gf_errno_to_error_array[ENETRESET] = GF_ERROR_CODE_NETRESET;

        /*      ECONNABORTED    53              / * Software caused connection abort */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNABORTED] = ECONNABORTED;
        gf_errno_to_error_array[ECONNABORTED] = GF_ERROR_CODE_CONNABORTED;

        /*      ECONNRESET      54              / * Connection reset by peer */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNRESET] = ECONNRESET;
        gf_errno_to_error_array[ECONNRESET] = GF_ERROR_CODE_CONNRESET;

        /*      ENOBUFS         55              / * No buffer space available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOBUFS] = ENOBUFS;
        gf_errno_to_error_array[ENOBUFS] = GF_ERROR_CODE_NOBUFS;

        /*      EISCONN         56              / * Socket is already connected */
        gf_error_to_errno_array[GF_ERROR_CODE_ISCONN] = EISCONN;
        gf_errno_to_error_array[EISCONN] = GF_ERROR_CODE_ISCONN;

        /*      ENOTCONN        57              / * Socket is not connected */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTCONN] = ENOTCONN;
        gf_errno_to_error_array[ENOTCONN] = GF_ERROR_CODE_NOTCONN;

        /*      ESHUTDOWN       58              / * Can't send after socket shutdown */
        gf_error_to_errno_array[GF_ERROR_CODE_SHUTDOWN] = ESHUTDOWN;
        gf_errno_to_error_array[ESHUTDOWN] = GF_ERROR_CODE_SHUTDOWN;

        /*      ETOOMANYREFS    59              / * Too many references: can't splice */
        gf_error_to_errno_array[GF_ERROR_CODE_TOOMANYREFS] = ETOOMANYREFS;
        gf_errno_to_error_array[ETOOMANYREFS] = GF_ERROR_CODE_TOOMANYREFS;

        /*      ETIMEDOUT       60              / * Operation timed out */
        gf_error_to_errno_array[GF_ERROR_CODE_TIMEDOUT] = ETIMEDOUT;
        gf_errno_to_error_array[ETIMEDOUT] = GF_ERROR_CODE_TIMEDOUT;

        /*      ECONNREFUSED    61              / * Connection refused */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNREFUSED] = ECONNREFUSED;
        gf_errno_to_error_array[ECONNREFUSED] = GF_ERROR_CODE_CONNREFUSED;

        /*      ELOOP           62              / * Too many levels of symbolic links */
        gf_error_to_errno_array[GF_ERROR_CODE_LOOP] = ELOOP;
        gf_errno_to_error_array[ELOOP] = GF_ERROR_CODE_LOOP;

        /*      ENAMETOOLONG    63              / * File name too long */
        gf_error_to_errno_array[GF_ERROR_CODE_NAMETOOLONG] = ENAMETOOLONG;
        gf_errno_to_error_array[ENAMETOOLONG] = GF_ERROR_CODE_NAMETOOLONG;

        /*      EHOSTDOWN       64              / * Host is down */
        gf_error_to_errno_array[GF_ERROR_CODE_HOSTDOWN] = EHOSTDOWN;
        gf_errno_to_error_array[EHOSTDOWN] = GF_ERROR_CODE_HOSTDOWN;

        /*      EHOSTUNREACH    65              / * No route to host */
        gf_error_to_errno_array[GF_ERROR_CODE_HOSTUNREACH] = EHOSTUNREACH;
        gf_errno_to_error_array[EHOSTUNREACH] = GF_ERROR_CODE_HOSTUNREACH;

        /*      ENOTEMPTY       66              / * Directory not empty */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTEMPTY] = ENOTEMPTY;
        gf_errno_to_error_array[ENOTEMPTY] = GF_ERROR_CODE_NOTEMPTY;

        /*      EPROCLIM        67              / * Too many processes */
        gf_error_to_errno_array[GF_ERROR_CODE_PROCLIM] = EPROCLIM;
        gf_errno_to_error_array[EPROCLIM] = GF_ERROR_CODE_PROCLIM;

        /*      EUSERS          68              / * Too many users */
        gf_error_to_errno_array[GF_ERROR_CODE_USERS] = EUSERS;
        gf_errno_to_error_array[EUSERS] = GF_ERROR_CODE_USERS;

        /*      EDQUOT          69              / * Disc quota exceeded */
        gf_error_to_errno_array[GF_ERROR_CODE_DQUOT] = EDQUOT;
        gf_errno_to_error_array[EDQUOT] = GF_ERROR_CODE_DQUOT;

        /*      ESTALE          70              / * Stale NFS file handle */
        gf_error_to_errno_array[GF_ERROR_CODE_STALE] = ESTALE;
        gf_errno_to_error_array[ESTALE] = GF_ERROR_CODE_STALE;

        /*      EREMOTE         71              / * Too many levels of remote in path */
        gf_error_to_errno_array[GF_ERROR_CODE_REMOTE] = EREMOTE;
        gf_errno_to_error_array[EREMOTE] = GF_ERROR_CODE_REMOTE;

        /*      EBADRPC         72              / * RPC struct is bad */
        gf_error_to_errno_array[GF_ERROR_CODE_BADRPC] = EBADRPC;
        gf_errno_to_error_array[EBADRPC] = GF_ERROR_CODE_BADRPC;

        /*      ERPCMISMATCH    73              / * RPC version wrong */
        gf_error_to_errno_array[GF_ERROR_CODE_RPCMISMATCH] = ERPCMISMATCH;
        gf_errno_to_error_array[ERPCMISMATCH] = GF_ERROR_CODE_RPCMISMATCH;

        /*      EPROGUNAVAIL    74              / * RPC prog. not avail */
        gf_error_to_errno_array[GF_ERROR_CODE_PROGUNAVAIL] = EPROGUNAVAIL;
        gf_errno_to_error_array[EPROGUNAVAIL] = GF_ERROR_CODE_PROGUNAVAIL;

        /*      EPROGMISMATCH   75              / * Program version wrong */
        gf_error_to_errno_array[GF_ERROR_CODE_PROGMISMATCH] = EPROGMISMATCH;
        gf_errno_to_error_array[EPROGMISMATCH] = GF_ERROR_CODE_PROGMISMATCH;

        /*      EPROCUNAVAIL    76              / * Bad procedure for program */
        gf_error_to_errno_array[GF_ERROR_CODE_PROCUNAVAIL] = EPROCUNAVAIL;
        gf_errno_to_error_array[EPROCUNAVAIL] = GF_ERROR_CODE_PROCUNAVAIL;

        /*      ENOLCK          77              / * No locks available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOLCK] = ENOLCK;
        gf_errno_to_error_array[ENOLCK] = GF_ERROR_CODE_NOLCK;

        /*      ENOSYS          78              / * Function not implemented */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSYS] = ENOSYS;
        gf_errno_to_error_array[ENOSYS] = GF_ERROR_CODE_NOSYS;

        /*      EFTYPE          79              / * Inappropriate file type or format */
        gf_error_to_errno_array[GF_ERROR_CODE_FTYPE] = EFTYPE;
        gf_errno_to_error_array[EFTYPE] = GF_ERROR_CODE_FTYPE;

        /*      EAUTH           80              / * Authentication error */
        gf_error_to_errno_array[GF_ERROR_CODE_AUTH] = EAUTH;
        gf_errno_to_error_array[EAUTH] = GF_ERROR_CODE_AUTH;

        /*      ENEEDAUTH       81              / * Need authenticator */
        gf_error_to_errno_array[GF_ERROR_CODE_NEEDAUTH] = ENEEDAUTH;
        gf_errno_to_error_array[ENEEDAUTH] = GF_ERROR_CODE_NEEDAUTH;
/* Intelligent device errors */
/*      EPWROFF         82      / * Device power is off */
        gf_error_to_errno_array[GF_ERROR_CODE_PWROFF] = EPWROFF;
        gf_errno_to_error_array[EPWROFF] = GF_ERROR_CODE_PWROFF;
/*      EDEVERR         83      / * Device error, e.g. paper out */
        gf_error_to_errno_array[GF_ERROR_CODE_DEVERR] = EDEVERR;
        gf_errno_to_error_array[EDEVERR] = GF_ERROR_CODE_DEVERR;

        /*      EOVERFLOW       84              / * Value too large to be stored in data type */
        gf_error_to_errno_array[GF_ERROR_CODE_OVERFLOW] = EOVERFLOW;
        gf_errno_to_error_array[EOVERFLOW] = GF_ERROR_CODE_OVERFLOW;

/* Program loading errors */
/*   EBADEXEC   85      / * Bad executable */
        gf_error_to_errno_array[GF_ERROR_CODE_BADEXEC] = EBADEXEC;
        gf_errno_to_error_array[EBADEXEC] = GF_ERROR_CODE_BADEXEC;

/*   EBADARCH   86      / * Bad CPU type in executable */
        gf_error_to_errno_array[GF_ERROR_CODE_BADARCH] = EBADARCH;
        gf_errno_to_error_array[EBADARCH] = GF_ERROR_CODE_BADARCH;

/*   ESHLIBVERS 87      / * Shared library version mismatch */
        gf_error_to_errno_array[GF_ERROR_CODE_SHLIBVERS] = ESHLIBVERS;
        gf_errno_to_error_array[ESHLIBVERS] = GF_ERROR_CODE_SHLIBVERS;

/*   EBADMACHO  88      / * Malformed Macho file */
        gf_error_to_errno_array[GF_ERROR_CODE_BADMACHO] = EBADMACHO;
        gf_errno_to_error_array[EBADMACHO] = GF_ERROR_CODE_BADMACHO;

#ifdef EDOOFUS
        /*    EDOOFUS           88              / * Programming error */
        gf_error_to_errno_array[GF_ERROR_CODE_DOOFUS] = EDOOFUS;
        gf_errno_to_error_array[EDOOFUS] = GF_ERROR_CODE_DOOFUS;
#endif

        /*      ECANCELED       89              / * Operation canceled */
        gf_error_to_errno_array[GF_ERROR_CODE_CANCELED] = ECANCELED;
        gf_errno_to_error_array[ECANCELED] = GF_ERROR_CODE_CANCELED;

        /*   EIDRM              90              / * Identifier removed */
        gf_error_to_errno_array[GF_ERROR_CODE_IDRM] = EIDRM;
        gf_errno_to_error_array[EIDRM] = GF_ERROR_CODE_IDRM;
        /*   ENOMSG             91              / * No message of desired type */
        gf_error_to_errno_array[GF_ERROR_CODE_NOMSG] = ENOMSG;
        gf_errno_to_error_array[ENOMSG] = GF_ERROR_CODE_NOMSG;

        /*   EILSEQ             92              / * Illegal byte sequence */
        gf_error_to_errno_array[GF_ERROR_CODE_ILSEQ] = EILSEQ;
        gf_errno_to_error_array[EILSEQ] = GF_ERROR_CODE_ILSEQ;

        /*   ENOATTR            93              / * Attribute not found */
        gf_error_to_errno_array[GF_ERROR_CODE_NOATTR] = ENOATTR;
        gf_errno_to_error_array[ENOATTR] = GF_ERROR_CODE_NOATTR;

        /*   EBADMSG            94              / * Bad message */
        gf_error_to_errno_array[GF_ERROR_CODE_BADMSG] = EBADMSG;
        gf_errno_to_error_array[EBADMSG] = GF_ERROR_CODE_BADMSG;

        /*   EMULTIHOP  95              / * Reserved */
        gf_error_to_errno_array[GF_ERROR_CODE_MULTIHOP] = EMULTIHOP;
        gf_errno_to_error_array[EMULTIHOP] = GF_ERROR_CODE_MULTIHOP;

        /*      ENODATA         96              / * No message available on STREAM */
        gf_error_to_errno_array[GF_ERROR_CODE_NEEDAUTH] = ENEEDAUTH;
        gf_errno_to_error_array[ENEEDAUTH] = GF_ERROR_CODE_NEEDAUTH;

        /*   ENOLINK            97              / * Reserved */
        gf_error_to_errno_array[GF_ERROR_CODE_NOLINK] = ENOLINK;
        gf_errno_to_error_array[ENOLINK] = GF_ERROR_CODE_NOLINK;

        /*   ENOSR              98              / * No STREAM resources */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSR] = ENOSR;
        gf_errno_to_error_array[ENOSR] = GF_ERROR_CODE_NOSR;

        /*   ENOSTR             99              / * Not a STREAM */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSTR] = ENOSTR;
        gf_errno_to_error_array[ENOSTR] = GF_ERROR_CODE_NOSTR;

/*      EPROTO          100             / * Protocol error */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTO] = EPROTO;
        gf_errno_to_error_array[EPROTO] = GF_ERROR_CODE_PROTO;
/*   ETIME              101             / * STREAM ioctl timeout */
        gf_error_to_errno_array[GF_ERROR_CODE_TIME] = ETIME;
        gf_errno_to_error_array[ETIME] = GF_ERROR_CODE_TIME;

/* This value is only discrete when compiling __DARWIN_UNIX03, or KERNEL */
/*      EOPNOTSUPP      102             / * Operation not supported on socket */
        gf_error_to_errno_array[GF_ERROR_CODE_OPNOTSUPP] = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;

/*   ENOPOLICY  103             / * No such policy registered */
        gf_error_to_errno_array[GF_ERROR_CODE_NOPOLICY] = ENOPOLICY;
        gf_errno_to_error_array[ENOPOLICY] = GF_ERROR_CODE_NOPOLICY;

        return ;
}
#endif /* GF_DARWIN_HOST_OS */

#ifdef GF_BSD_HOST_OS
static void
init_compat_errno_arrays ()
{
        /* Quite a bit of things changed in FreeBSD - current */

        /*    EAGAIN          35              / * Try Again */
        gf_error_to_errno_array[GF_ERROR_CODE_AGAIN] = EAGAIN;
        gf_errno_to_error_array[EAGAIN] = GF_ERROR_CODE_AGAIN;

        /*    EDEADLK         11              / * Resource deadlock would occur */
        gf_error_to_errno_array[GF_ERROR_CODE_DEADLK] = EDEADLK;
        gf_errno_to_error_array[EDEADLK] = GF_ERROR_CODE_DEADLK;

        /*      EINPROGRESS     36              / * Operation now in progress */
        gf_error_to_errno_array[GF_ERROR_CODE_INPROGRESS] = EINPROGRESS;
        gf_errno_to_error_array[EINPROGRESS] = GF_ERROR_CODE_INPROGRESS;

        /*      EALREADY        37              / * Operation already in progress */
        gf_error_to_errno_array[GF_ERROR_CODE_ALREADY] = EALREADY;
        gf_errno_to_error_array[EALREADY] = GF_ERROR_CODE_ALREADY;

        /*      ENOTSOCK        38              / * Socket operation on non-socket */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTSOCK] = ENOTSOCK;
        gf_errno_to_error_array[ENOTSOCK] = GF_ERROR_CODE_NOTSOCK;

        /*      EDESTADDRREQ    39              / * Destination address required */
        gf_error_to_errno_array[GF_ERROR_CODE_DESTADDRREQ] = EDESTADDRREQ;
        gf_errno_to_error_array[EDESTADDRREQ] = GF_ERROR_CODE_DESTADDRREQ;

        /*      EMSGSIZE        40              / * Message too long */
        gf_error_to_errno_array[GF_ERROR_CODE_MSGSIZE] = EMSGSIZE;
        gf_errno_to_error_array[EMSGSIZE] = GF_ERROR_CODE_MSGSIZE;

        /*      EPROTOTYPE      41              / * Protocol wrong type for socket */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTOTYPE] = EPROTOTYPE;
        gf_errno_to_error_array[EPROTOTYPE] = GF_ERROR_CODE_PROTOTYPE;

        /*      ENOPROTOOPT     42              / * Protocol not available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOPROTOOPT] = ENOPROTOOPT;
        gf_errno_to_error_array[ENOPROTOOPT] = GF_ERROR_CODE_NOPROTOOPT;

        /*      EPROTONOSUPPORT 43              / * Protocol not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTONOSUPPORT] = EPROTONOSUPPORT;
        gf_errno_to_error_array[EPROTONOSUPPORT] = GF_ERROR_CODE_PROTONOSUPPORT;

        /*      ESOCKTNOSUPPORT 44              / * Socket type not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_SOCKTNOSUPPORT] = ESOCKTNOSUPPORT;
        gf_errno_to_error_array[ESOCKTNOSUPPORT] = GF_ERROR_CODE_SOCKTNOSUPPORT;

        /*      EOPNOTSUPP      45              / * Operation not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_OPNOTSUPP] = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;

        /*      EPFNOSUPPORT    46              / * Protocol family not supported */
        gf_error_to_errno_array[GF_ERROR_CODE_PFNOSUPPORT] = EPFNOSUPPORT;
        gf_errno_to_error_array[EPFNOSUPPORT] = GF_ERROR_CODE_PFNOSUPPORT;

        /*      EAFNOSUPPORT    47              / * Address family not supported by protocol family */
        gf_error_to_errno_array[GF_ERROR_CODE_AFNOSUPPORT] = EAFNOSUPPORT;
        gf_errno_to_error_array[EAFNOSUPPORT] = GF_ERROR_CODE_AFNOSUPPORT;

        /*      EADDRINUSE      48              / * Address already in use */
        gf_error_to_errno_array[GF_ERROR_CODE_ADDRINUSE] = EADDRINUSE;
        gf_errno_to_error_array[EADDRINUSE] = GF_ERROR_CODE_ADDRINUSE;

        /*      EADDRNOTAVAIL   49              / * Can't assign requested address */
        gf_error_to_errno_array[GF_ERROR_CODE_ADDRNOTAVAIL] = EADDRNOTAVAIL;
        gf_errno_to_error_array[EADDRNOTAVAIL] = GF_ERROR_CODE_ADDRNOTAVAIL;

        /*      ENETDOWN        50              / * Network is down */
        gf_error_to_errno_array[GF_ERROR_CODE_NETDOWN] = ENETDOWN;
        gf_errno_to_error_array[ENETDOWN] = GF_ERROR_CODE_NETDOWN;

        /*      ENETUNREACH     51              / * Network is unreachable */
        gf_error_to_errno_array[GF_ERROR_CODE_NETUNREACH] = ENETUNREACH;
        gf_errno_to_error_array[ENETUNREACH] = GF_ERROR_CODE_NETUNREACH;

        /*      ENETRESET       52              / * Network dropped connection on reset */
        gf_error_to_errno_array[GF_ERROR_CODE_NETRESET] = ENETRESET;
        gf_errno_to_error_array[ENETRESET] = GF_ERROR_CODE_NETRESET;

        /*      ECONNABORTED    53              / * Software caused connection abort */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNABORTED] = ECONNABORTED;
        gf_errno_to_error_array[ECONNABORTED] = GF_ERROR_CODE_CONNABORTED;

        /*      ECONNRESET      54              / * Connection reset by peer */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNRESET] = ECONNRESET;
        gf_errno_to_error_array[ECONNRESET] = GF_ERROR_CODE_CONNRESET;

        /*      ENOBUFS         55              / * No buffer space available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOBUFS] = ENOBUFS;
        gf_errno_to_error_array[ENOBUFS] = GF_ERROR_CODE_NOBUFS;

        /*      EISCONN         56              / * Socket is already connected */
        gf_error_to_errno_array[GF_ERROR_CODE_ISCONN] = EISCONN;
        gf_errno_to_error_array[EISCONN] = GF_ERROR_CODE_ISCONN;

        /*      ENOTCONN        57              / * Socket is not connected */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTCONN] = ENOTCONN;
        gf_errno_to_error_array[ENOTCONN] = GF_ERROR_CODE_NOTCONN;

        /*      ESHUTDOWN       58              / * Can't send after socket shutdown */
        gf_error_to_errno_array[GF_ERROR_CODE_SHUTDOWN] = ESHUTDOWN;
        gf_errno_to_error_array[ESHUTDOWN] = GF_ERROR_CODE_SHUTDOWN;

        /*      ETOOMANYREFS    59              / * Too many references: can't splice */
        gf_error_to_errno_array[GF_ERROR_CODE_TOOMANYREFS] = ETOOMANYREFS;
        gf_errno_to_error_array[ETOOMANYREFS] = GF_ERROR_CODE_TOOMANYREFS;

        /*      ETIMEDOUT       60              / * Operation timed out */
        gf_error_to_errno_array[GF_ERROR_CODE_TIMEDOUT] = ETIMEDOUT;
        gf_errno_to_error_array[ETIMEDOUT] = GF_ERROR_CODE_TIMEDOUT;

        /*      ECONNREFUSED    61              / * Connection refused */
        gf_error_to_errno_array[GF_ERROR_CODE_CONNREFUSED] = ECONNREFUSED;
        gf_errno_to_error_array[ECONNREFUSED] = GF_ERROR_CODE_CONNREFUSED;

        /*      ELOOP           62              / * Too many levels of symbolic links */
        gf_error_to_errno_array[GF_ERROR_CODE_LOOP] = ELOOP;
        gf_errno_to_error_array[ELOOP] = GF_ERROR_CODE_LOOP;

        /*      ENAMETOOLONG    63              / * File name too long */
        gf_error_to_errno_array[GF_ERROR_CODE_NAMETOOLONG] = ENAMETOOLONG;
        gf_errno_to_error_array[ENAMETOOLONG] = GF_ERROR_CODE_NAMETOOLONG;

        /*      EHOSTDOWN       64              / * Host is down */
        gf_error_to_errno_array[GF_ERROR_CODE_HOSTDOWN] = EHOSTDOWN;
        gf_errno_to_error_array[EHOSTDOWN] = GF_ERROR_CODE_HOSTDOWN;

        /*      EHOSTUNREACH    65              / * No route to host */
        gf_error_to_errno_array[GF_ERROR_CODE_HOSTUNREACH] = EHOSTUNREACH;
        gf_errno_to_error_array[EHOSTUNREACH] = GF_ERROR_CODE_HOSTUNREACH;

        /*      ENOTEMPTY       66              / * Directory not empty */
        gf_error_to_errno_array[GF_ERROR_CODE_NOTEMPTY] = ENOTEMPTY;
        gf_errno_to_error_array[ENOTEMPTY] = GF_ERROR_CODE_NOTEMPTY;

        /*      EPROCLIM        67              / * Too many processes */
        gf_error_to_errno_array[GF_ERROR_CODE_PROCLIM] = EPROCLIM;
        gf_errno_to_error_array[EPROCLIM] = GF_ERROR_CODE_PROCLIM;

        /*      EUSERS          68              / * Too many users */
        gf_error_to_errno_array[GF_ERROR_CODE_USERS] = EUSERS;
        gf_errno_to_error_array[EUSERS] = GF_ERROR_CODE_USERS;

        /*      EDQUOT          69              / * Disc quota exceeded */
        gf_error_to_errno_array[GF_ERROR_CODE_DQUOT] = EDQUOT;
        gf_errno_to_error_array[EDQUOT] = GF_ERROR_CODE_DQUOT;

        /*      ESTALE          70              / * Stale NFS file handle */
        gf_error_to_errno_array[GF_ERROR_CODE_STALE] = ESTALE;
        gf_errno_to_error_array[ESTALE] = GF_ERROR_CODE_STALE;

        /*      EREMOTE         71              / * Too many levels of remote in path */
        gf_error_to_errno_array[GF_ERROR_CODE_REMOTE] = EREMOTE;
        gf_errno_to_error_array[EREMOTE] = GF_ERROR_CODE_REMOTE;

        /*      EBADRPC         72              / * RPC struct is bad */
        gf_error_to_errno_array[GF_ERROR_CODE_BADRPC] = EBADRPC;
        gf_errno_to_error_array[EBADRPC] = GF_ERROR_CODE_BADRPC;

        /*      ERPCMISMATCH    73              / * RPC version wrong */
        gf_error_to_errno_array[GF_ERROR_CODE_RPCMISMATCH] = ERPCMISMATCH;
        gf_errno_to_error_array[ERPCMISMATCH] = GF_ERROR_CODE_RPCMISMATCH;

        /*      EPROGUNAVAIL    74              / * RPC prog. not avail */
        gf_error_to_errno_array[GF_ERROR_CODE_PROGUNAVAIL] = EPROGUNAVAIL;
        gf_errno_to_error_array[EPROGUNAVAIL] = GF_ERROR_CODE_PROGUNAVAIL;

        /*      EPROGMISMATCH   75              / * Program version wrong */
        gf_error_to_errno_array[GF_ERROR_CODE_PROGMISMATCH] = EPROGMISMATCH;
        gf_errno_to_error_array[EPROGMISMATCH] = GF_ERROR_CODE_PROGMISMATCH;

        /*      EPROCUNAVAIL    76              / * Bad procedure for program */
        gf_error_to_errno_array[GF_ERROR_CODE_PROCUNAVAIL] = EPROCUNAVAIL;
        gf_errno_to_error_array[EPROCUNAVAIL] = GF_ERROR_CODE_PROCUNAVAIL;

        /*      ENOLCK          77              / * No locks available */
        gf_error_to_errno_array[GF_ERROR_CODE_NOLCK] = ENOLCK;
        gf_errno_to_error_array[ENOLCK] = GF_ERROR_CODE_NOLCK;

        /*      ENOSYS          78              / * Function not implemented */
        gf_error_to_errno_array[GF_ERROR_CODE_NOSYS] = ENOSYS;
        gf_errno_to_error_array[ENOSYS] = GF_ERROR_CODE_NOSYS;

        /*      EFTYPE          79              / * Inappropriate file type or format */
        gf_error_to_errno_array[GF_ERROR_CODE_FTYPE] = EFTYPE;
        gf_errno_to_error_array[EFTYPE] = GF_ERROR_CODE_FTYPE;

        /*      EAUTH           80              / * Authentication error */
        gf_error_to_errno_array[GF_ERROR_CODE_AUTH] = EAUTH;
        gf_errno_to_error_array[EAUTH] = GF_ERROR_CODE_AUTH;

        /*      ENEEDAUTH       81              / * Need authenticator */
        gf_error_to_errno_array[GF_ERROR_CODE_NEEDAUTH] = ENEEDAUTH;
        gf_errno_to_error_array[ENEEDAUTH] = GF_ERROR_CODE_NEEDAUTH;

        /*      EIDRM           82              / * Identifier removed */
        gf_error_to_errno_array[GF_ERROR_CODE_IDRM] = EIDRM;
        gf_errno_to_error_array[EIDRM] = GF_ERROR_CODE_IDRM;

        /*      ENOMSG          83              / * No message of desired type */
        gf_error_to_errno_array[GF_ERROR_CODE_NOMSG] = ENOMSG;
        gf_errno_to_error_array[ENOMSG] = GF_ERROR_CODE_NOMSG;

        /*      EOVERFLOW       84              / * Value too large to be stored in data type */
        gf_error_to_errno_array[GF_ERROR_CODE_OVERFLOW] = EOVERFLOW;
        gf_errno_to_error_array[EOVERFLOW] = GF_ERROR_CODE_OVERFLOW;

        /*      ECANCELED       85              / * Operation canceled */
        gf_error_to_errno_array[GF_ERROR_CODE_CANCELED] = ECANCELED;
        gf_errno_to_error_array[ECANCELED] = GF_ERROR_CODE_CANCELED;

        /*      EILSEQ          86              / * Illegal byte sequence */
        gf_error_to_errno_array[GF_ERROR_CODE_ILSEQ] = EILSEQ;
        gf_errno_to_error_array[EILSEQ] = GF_ERROR_CODE_ILSEQ;

        /*      ENOATTR         87              / * Attribute not found */
        gf_error_to_errno_array[GF_ERROR_CODE_NOATTR] = ENOATTR;
        gf_errno_to_error_array[ENOATTR] = GF_ERROR_CODE_NOATTR;

#ifdef EDOOFUS
        /*    EDOOFUS           88              / * Programming error */
        gf_error_to_errno_array[GF_ERROR_CODE_DOOFUS] = EDOOFUS;
        gf_errno_to_error_array[EDOOFUS] = GF_ERROR_CODE_DOOFUS;
#endif

        /*      EBADMSG         89              / * Bad message */
        gf_error_to_errno_array[GF_ERROR_CODE_BADMSG] = EBADMSG;
        gf_errno_to_error_array[EBADMSG] = GF_ERROR_CODE_BADMSG;

#ifdef __NetBSD__
        /*      ENODATA         89              / * No message available */
        gf_error_to_errno_array[GF_ERROR_CODE_NODATA] = ENODATA;
        gf_errno_to_error_array[ENODATA] = GF_ERROR_CODE_NODATA;
#endif

        /*      EMULTIHOP       90              / * Multihop attempted */
        gf_error_to_errno_array[GF_ERROR_CODE_MULTIHOP] = EMULTIHOP;
        gf_errno_to_error_array[EMULTIHOP] = GF_ERROR_CODE_MULTIHOP;

        /*      ENOLINK         91              / * Link has been severed */
        gf_error_to_errno_array[GF_ERROR_CODE_NOLINK] = ENOLINK;
        gf_errno_to_error_array[ENOLINK] = GF_ERROR_CODE_NOLINK;

        /*      EPROTO          92              / * Protocol error */
        gf_error_to_errno_array[GF_ERROR_CODE_PROTO] = EPROTO;
        gf_errno_to_error_array[EPROTO] = GF_ERROR_CODE_PROTO;


        return ;
}
#endif /* GF_BSD_HOST_OS */

#ifdef GF_LINUX_HOST_OS
static void
init_compat_errno_arrays ()
{
        /* Things are fine. Everything should work seemlessly on GNU/Linux machines */
        return ;
}
#endif /* GF_LINUX_HOST_OS */


static void
init_errno_arrays ()
{
        int i;
        for (i=0; i < GF_ERROR_CODE_UNKNOWN; i++) {
                gf_errno_to_error_array[i] = i;
                gf_error_to_errno_array[i] = i;
        }
        /* Now change the order if it needs to be. */
        init_compat_errno_arrays();

        return;
}

int32_t
gf_errno_to_error (int32_t op_errno)
{
        if (!gf_compat_errno_init_done) {
                init_errno_arrays ();
                gf_compat_errno_init_done = 1;
        }

        if ((op_errno > GF_ERROR_CODE_SUCCESS) && (op_errno < GF_ERROR_CODE_UNKNOWN))
                return gf_errno_to_error_array[op_errno];

        return op_errno;
}


int32_t
gf_error_to_errno (int32_t error)
{
        if (!gf_compat_errno_init_done) {
                init_errno_arrays ();
                gf_compat_errno_init_done = 1;
        }

        if ((error > GF_ERROR_CODE_SUCCESS) && (error < GF_ERROR_CODE_UNKNOWN))
                return gf_error_to_errno_array[error];

        return error;
}
