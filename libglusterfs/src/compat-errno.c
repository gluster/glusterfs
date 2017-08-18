/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdint.h>
#include <string.h>

#include "compat-errno.h"

static int32_t gf_errno_to_error_array[GF_ERRNO_UNKNOWN];

struct gf_errno_struct {
        int32_t  error;
        char    *error_msg;
};

static struct gf_errno_struct gf_error_convert_array[GF_ERRNO_UNKNOWN] = {
        [GF_ERROR_CODE_PERM]             = { EPERM, NULL},
        [GF_ERROR_CODE_NOENT]            = { ENOENT, NULL},
        [GF_ERROR_CODE_SRCH]             = { ESRCH, NULL},
        [GF_ERROR_CODE_INTR]             = { EINTR, NULL},
        [GF_ERROR_CODE_IO]               = { EIO, NULL},
        [GF_ERROR_CODE_NXIO]             = { ENXIO, NULL},
        [GF_ERROR_CODE_2BIG]             = { E2BIG, NULL},
        [GF_ERROR_CODE_NOEXEC]           = { ENOEXEC, NULL},
        [GF_ERROR_CODE_BADF]             = { EBADF, NULL},
        [GF_ERROR_CODE_CHILD]            = { ECHILD, NULL},
        [GF_ERROR_CODE_AGAIN]            = { EAGAIN, NULL},
        [GF_ERROR_CODE_NOMEM]            = { ENOMEM, NULL},
        [GF_ERROR_CODE_ACCES]            = { EACCES, NULL},
        [GF_ERROR_CODE_FAULT]            = { EFAULT, NULL},
        [GF_ERROR_CODE_NOTBLK]           = { ENOTBLK, NULL},
        [GF_ERROR_CODE_BUSY]             = { EBUSY, NULL},
        [GF_ERROR_CODE_EXIST]            = { EEXIST, NULL},
        [GF_ERROR_CODE_XDEV]             = { EXDEV, NULL},
        [GF_ERROR_CODE_NODEV]            = { ENODEV, NULL},
        [GF_ERROR_CODE_NOTDIR]           = { ENOTDIR, NULL},
        [GF_ERROR_CODE_ISDIR]            = { EISDIR, NULL},
        [GF_ERROR_CODE_INVAL]            = { EINVAL, NULL},
        [GF_ERROR_CODE_NFILE]            = { ENFILE, NULL},
        [GF_ERROR_CODE_MFILE]            = { EMFILE, NULL},
        [GF_ERROR_CODE_NOTTY]            = { ENOTTY, NULL},
        [GF_ERROR_CODE_TXTBSY]           = { ETXTBSY, NULL},
        [GF_ERROR_CODE_FBIG]             = { EFBIG, NULL},
        [GF_ERROR_CODE_NOSPC]            = { ENOSPC, NULL},
        [GF_ERROR_CODE_SPIPE]            = { ESPIPE, NULL},
        [GF_ERROR_CODE_ROFS]             = { EROFS, NULL},
        [GF_ERROR_CODE_MLINK]            = { EMLINK, NULL},
        [GF_ERROR_CODE_PIPE]             = { EPIPE, NULL},
        [GF_ERROR_CODE_DOM]              = { EDOM, NULL},
        [GF_ERROR_CODE_RANGE]            = { ERANGE, NULL},
        [GF_ERROR_CODE_DEADLK]           = { EDEADLK, NULL},
        [GF_ERROR_CODE_NAMETOOLONG]      = { ENAMETOOLONG, NULL},
        [GF_ERROR_CODE_NOLCK]            = { ENOLCK, NULL},
        [GF_ERROR_CODE_NOSYS]            = { ENOSYS, NULL},
        [GF_ERROR_CODE_NOTEMPTY]         = { ENOTEMPTY, NULL},
        [GF_ERROR_CODE_LOOP]             = { ELOOP, NULL},
        [GF_ERROR_CODE_NOMSG]            = { ENOMSG, NULL},
        [GF_ERROR_CODE_IDRM]             = { EIDRM, NULL},

        [GF_ERROR_CODE_NODATA]           = { ENODATA, NULL},
        [GF_ERROR_CODE_REMOTE]           = { EREMOTE, NULL},
        [GF_ERROR_CODE_NOLINK]           = { ENOLINK, NULL},
        [GF_ERROR_CODE_PROTO]            = { EPROTO, NULL},
        [GF_ERROR_CODE_MULTIHOP]         = { EMULTIHOP, NULL},
        [GF_ERROR_CODE_BADMSG]           = { EBADMSG, NULL},
        [GF_ERROR_CODE_OVERFLOW]         = { EOVERFLOW, NULL},
        [GF_ERROR_CODE_ILSEQ]            = { EILSEQ, NULL},
        [GF_ERROR_CODE_USERS]            = { EUSERS, NULL},
        [GF_ERROR_CODE_NOTSOCK]          = { ENOTSOCK, NULL},
        [GF_ERROR_CODE_DESTADDRREQ]      = { EDESTADDRREQ, NULL},
        [GF_ERROR_CODE_MSGSIZE]          = { EMSGSIZE, NULL},
        [GF_ERROR_CODE_PROTOTYPE]        = { EPROTOTYPE, NULL},
        [GF_ERROR_CODE_NOPROTOOPT]       = { ENOPROTOOPT, NULL},
        [GF_ERROR_CODE_PROTONOSUPPORT]   = { EPROTONOSUPPORT, NULL},
        [GF_ERROR_CODE_SOCKTNOSUPPORT]   = { ESOCKTNOSUPPORT, NULL},
        [GF_ERROR_CODE_OPNOTSUPP]        = { EOPNOTSUPP, NULL},
        [GF_ERROR_CODE_PFNOSUPPORT]      = { EPFNOSUPPORT, NULL},
        [GF_ERROR_CODE_AFNOSUPPORT]      = { EAFNOSUPPORT, NULL},
        [GF_ERROR_CODE_ADDRINUSE]        = { EADDRINUSE, NULL},
        [GF_ERROR_CODE_ADDRNOTAVAIL]     = { EADDRNOTAVAIL, NULL},
        [GF_ERROR_CODE_NETDOWN]          = { ENETDOWN, NULL},
        [GF_ERROR_CODE_NETUNREACH]       = { ENETUNREACH, NULL},
        [GF_ERROR_CODE_NETRESET]         = { ENETRESET, NULL},
        [GF_ERROR_CODE_CONNABORTED]      = { ECONNABORTED, NULL},
        [GF_ERROR_CODE_CONNRESET]        = { ECONNRESET, NULL},
        [GF_ERROR_CODE_NOBUFS]           = { ENOBUFS, NULL},
        [GF_ERROR_CODE_ISCONN]           = { EISCONN, NULL},
        [GF_ERROR_CODE_NOTCONN]          = { ENOTCONN, NULL},
        [GF_ERROR_CODE_SHUTDOWN]         = { ESHUTDOWN, NULL},
        [GF_ERROR_CODE_TOOMANYREFS]      = { ETOOMANYREFS, NULL},
        [GF_ERROR_CODE_TIMEDOUT]         = { ETIMEDOUT, NULL},
        [GF_ERROR_CODE_CONNREFUSED]      = { ECONNREFUSED, NULL},
        [GF_ERROR_CODE_HOSTDOWN]         = { EHOSTDOWN, NULL},
        [GF_ERROR_CODE_HOSTUNREACH]      = { EHOSTUNREACH, NULL},
        [GF_ERROR_CODE_ALREADY]          = { EALREADY, NULL},
        [GF_ERROR_CODE_INPROGRESS]       = { EINPROGRESS, NULL},
        [GF_ERROR_CODE_STALE]            = { ESTALE, NULL},
        [GF_ERROR_CODE_DQUOT]            = { EDQUOT, NULL},
        [GF_ERROR_CODE_CANCELED]         = { ECANCELED, NULL},

#ifndef GF_BSD_HOST_OS

        [GF_ERROR_CODE_NOSTR]            = { ENOSTR, NULL},
        [GF_ERROR_CODE_TIME]             = { ETIME, NULL},
        [GF_ERROR_CODE_NOSR]             = { ENOSR, NULL},
        [GF_ERROR_CODE_CHRNG]            = { ECHRNG, NULL},
        [GF_ERROR_CODE_L2NSYNC]          = { EL2NSYNC, NULL},
        [GF_ERROR_CODE_L3HLT]            = { EL3HLT, NULL},
        [GF_ERROR_CODE_L3RST]            = { EL3RST, NULL},
        [GF_ERROR_CODE_LNRNG]            = { ELNRNG, NULL},
        [GF_ERROR_CODE_UNATCH]           = { EUNATCH, NULL},
        [GF_ERROR_CODE_NOCSI]            = { ENOCSI, NULL},
        [GF_ERROR_CODE_L2HLT]            = { EL2HLT, NULL},
        [GF_ERROR_CODE_BADE]             = { EBADE, NULL},
        [GF_ERROR_CODE_BADR]             = { EBADR, NULL},
        [GF_ERROR_CODE_XFULL]            = { EXFULL, NULL},
        [GF_ERROR_CODE_NOANO]            = { ENOANO, NULL},
        [GF_ERROR_CODE_BADRQC]           = { EBADRQC, NULL},
        [GF_ERROR_CODE_BADSLT]           = { EBADSLT, NULL},
        [GF_ERROR_CODE_BFONT]            = { EBFONT, NULL},
        [GF_ERROR_CODE_NONET]            = { ENONET, NULL},
        [GF_ERROR_CODE_NOPKG]            = { ENOPKG, NULL},
        [GF_ERROR_CODE_ADV]              = { EADV, NULL},
        [GF_ERROR_CODE_SRMNT]            = { ESRMNT, NULL},
        [GF_ERROR_CODE_COMM]             = { ECOMM, NULL},
        [GF_ERROR_CODE_DOTDOT]           = { EDOTDOT, NULL},
        [GF_ERROR_CODE_NOTUNIQ]          = { ENOTUNIQ, NULL},
        [GF_ERROR_CODE_BADFD]            = { EBADFD, NULL},
        [GF_ERROR_CODE_REMCHG]           = { EREMCHG, NULL},
        [GF_ERROR_CODE_LIBACC]           = { ELIBACC, NULL},
        [GF_ERROR_CODE_LIBBAD]           = { ELIBBAD, NULL},
        [GF_ERROR_CODE_LIBSCN]           = { ELIBSCN, NULL},
        [GF_ERROR_CODE_LIBMAX]           = { ELIBMAX, NULL},
        [GF_ERROR_CODE_LIBEXEC]          = { ELIBEXEC, NULL},
        [GF_ERROR_CODE_RESTART]          = { ERESTART, NULL},
        [GF_ERROR_CODE_STRPIPE]          = { ESTRPIPE, NULL},
        [GF_ERROR_CODE_UCLEAN]           = { EUCLEAN, NULL},
        [GF_ERROR_CODE_NOTNAM]           = { ENOTNAM, NULL},
        [GF_ERROR_CODE_NAVAIL]           = { ENAVAIL, NULL},
        [GF_ERROR_CODE_ISNAM]            = { EISNAM, NULL},
        [GF_ERROR_CODE_REMOTEIO]         = { EREMOTEIO, NULL},
        [GF_ERROR_CODE_NOMEDIUM]         = { ENOMEDIUM, NULL},
        [GF_ERROR_CODE_MEDIUMTYPE]       = { EMEDIUMTYPE, NULL},
        [GF_ERROR_CODE_NOKEY]            = { ENOKEY, NULL},
        [GF_ERROR_CODE_KEYEXPIRED]       = { EKEYEXPIRED, NULL},
        [GF_ERROR_CODE_KEYREVOKED]       = { EKEYREVOKED, NULL},
        [GF_ERROR_CODE_KEYREJECTED]      = { EKEYREJECTED, NULL},
        [GF_ERROR_CODE_OWNERDEAD]        = { EOWNERDEAD, NULL},
        [GF_ERROR_CODE_NOTRECOVERABLE]   = { ENOTRECOVERABLE, NULL},
#endif
};

#ifdef GF_SOLARIS_HOST_OS
static void
init_compat_errno_arrays ()
{
/*      ENOMSG  35      / * No message of desired type          */
        gf_error_convert_array[GF_ERROR_CODE_NOMSG].error = ENOMSG;
        gf_errno_to_error_array[ENOMSG] = GF_ERROR_CODE_NOMSG;

/*      EIDRM   36      / * Identifier removed                  */
        gf_error_convert_array[GF_ERROR_CODE_IDRM].error = EIDRM;
        gf_errno_to_error_array[EIDRM] = GF_ERROR_CODE_IDRM;

/*      ECHRNG  37      / * Channel number out of range         */
        gf_error_convert_array[GF_ERROR_CODE_CHRNG].error = ECHRNG;
        gf_errno_to_error_array[ECHRNG] = GF_ERROR_CODE_CHRNG;

/*      EL2NSYNC 38     / * Level 2 not synchronized            */
        gf_error_convert_array[GF_ERROR_CODE_L2NSYNC].error = EL2NSYNC;
        gf_errno_to_error_array[EL2NSYNC] = GF_ERROR_CODE_L2NSYNC;

/*      EL3HLT  39      / * Level 3 halted                      */
        gf_error_convert_array[GF_ERROR_CODE_L3HLT].error = EL3HLT;
        gf_errno_to_error_array[EL3HLT] = GF_ERROR_CODE_L3HLT;

/*      EL3RST  40      / * Level 3 reset                       */
        gf_error_convert_array[GF_ERROR_CODE_L3RST].error = EL3RST;
        gf_errno_to_error_array[EL3RST] = GF_ERROR_CODE_L3RST;

/*      ELNRNG  41      / * Link number out of range            */
        gf_error_convert_array[GF_ERROR_CODE_LNRNG].error = ELNRNG;
        gf_errno_to_error_array[ELNRNG] = GF_ERROR_CODE_LNRNG;

/*      EUNATCH 42      / * Protocol driver not attached                */
        gf_error_convert_array[GF_ERROR_CODE_UNATCH].error = EUNATCH;
        gf_errno_to_error_array[EUNATCH] = GF_ERROR_CODE_UNATCH;

/*      ENOCSI  43      / * No CSI structure available          */
        gf_error_convert_array[GF_ERROR_CODE_NOCSI].error = ENOCSI;
        gf_errno_to_error_array[ENOCSI] = GF_ERROR_CODE_NOCSI;

/*      EL2HLT  44      / * Level 2 halted                      */
        gf_error_convert_array[GF_ERROR_CODE_L2HLT].error = EL2HLT;
        gf_errno_to_error_array[EL2HLT] = GF_ERROR_CODE_L2HLT;

/*      EDEADLK 45      / * Deadlock condition.                 */
        gf_error_convert_array[GF_ERROR_CODE_DEADLK].error = EDEADLK;
        gf_errno_to_error_array[EDEADLK] = GF_ERROR_CODE_DEADLK;

/*      ENOLCK  46      / * No record locks available.          */
        gf_error_convert_array[GF_ERROR_CODE_NOLCK].error = ENOLCK;
        gf_errno_to_error_array[ENOLCK] = GF_ERROR_CODE_NOLCK;

/*      ECANCELED 47    / * Operation canceled                  */
        gf_error_convert_array[GF_ERROR_CODE_CANCELED].error = ECANCELED;
        gf_errno_to_error_array[ECANCELED] = GF_ERROR_CODE_CANCELED;

/*      ENOTSUP 48      / * Operation not supported             */
        gf_error_convert_array[GF_ERROR_CODE_NOTSUPP].error = ENOTSUP;
        gf_errno_to_error_array[ENOTSUP] = GF_ERROR_CODE_NOTSUPP;

/* Filesystem Quotas */
/*      EDQUOT  49      / * Disc quota exceeded                 */
        gf_error_convert_array[GF_ERROR_CODE_DQUOT].error = EDQUOT;
        gf_errno_to_error_array[EDQUOT] = GF_ERROR_CODE_DQUOT;

/* Convergent Error Returns */
/*      EBADE   50      / * invalid exchange                    */
        gf_error_convert_array[GF_ERROR_CODE_BADE].error = EBADE;
        gf_errno_to_error_array[EBADE] = GF_ERROR_CODE_BADE;
/*      EBADR   51      / * invalid request descriptor          */
        gf_error_convert_array[GF_ERROR_CODE_BADR].error = EBADR;
        gf_errno_to_error_array[EBADR] = GF_ERROR_CODE_BADR;
/*      EXFULL  52      / * exchange full                       */
        gf_error_convert_array[GF_ERROR_CODE_XFULL].error = EXFULL;
        gf_errno_to_error_array[EXFULL] = GF_ERROR_CODE_XFULL;
/*      ENOANO  53      / * no anode                            */
        gf_error_convert_array[GF_ERROR_CODE_NOANO].error = ENOANO;
        gf_errno_to_error_array[ENOANO] = GF_ERROR_CODE_NOANO;
/*      EBADRQC 54      / * invalid request code                        */
        gf_error_convert_array[GF_ERROR_CODE_BADRQC].error = EBADRQC;
        gf_errno_to_error_array[EBADRQC] = GF_ERROR_CODE_BADRQC;
/*      EBADSLT 55      / * invalid slot                                */
        gf_error_convert_array[GF_ERROR_CODE_BADSLT].error = EBADSLT;
        gf_errno_to_error_array[EBADSLT] = GF_ERROR_CODE_BADSLT;
/*      EDEADLOCK 56    / * file locking deadlock error         */
/* This is same as EDEADLK on linux */
        gf_error_convert_array[GF_ERROR_CODE_DEADLK].error = EDEADLOCK;
        gf_errno_to_error_array[EDEADLOCK] = GF_ERROR_CODE_DEADLK;

/*      EBFONT  57      / * bad font file fmt                   */
        gf_error_convert_array[GF_ERROR_CODE_BFONT].error = EBFONT;
        gf_errno_to_error_array[EBFONT] = GF_ERROR_CODE_BFONT;

/* Interprocess Robust Locks */
/*      EOWNERDEAD      58      / * process died with the lock */
        gf_error_convert_array[GF_ERROR_CODE_OWNERDEAD].error = EOWNERDEAD;
        gf_errno_to_error_array[EOWNERDEAD] = GF_ERROR_CODE_OWNERDEAD;
/*      ENOTRECOVERABLE 59      / * lock is not recoverable */
        gf_error_convert_array[GF_ERROR_CODE_NOTRECOVERABLE].error = ENOTRECOVERABLE;
        gf_errno_to_error_array[ENOTRECOVERABLE] = GF_ERROR_CODE_NOTRECOVERABLE;

/* stream problems */
/*      ENOSTR  60      / * Device not a stream                 */
        gf_error_convert_array[GF_ERROR_CODE_NOSTR].error = ENOSTR;
        gf_errno_to_error_array[ENOSTR] = GF_ERROR_CODE_NOSTR;
/*      ENODATA 61      / * no data (for no delay io)           */
        gf_error_convert_array[GF_ERROR_CODE_NODATA].error = ENODATA;
        gf_errno_to_error_array[ENODATA] = GF_ERROR_CODE_NODATA;
/*      ETIME   62      / * timer expired                       */
        gf_error_convert_array[GF_ERROR_CODE_TIME].error = ETIME;
        gf_errno_to_error_array[ETIME] = GF_ERROR_CODE_TIME;
/*      ENOSR   63      / * out of streams resources            */
        gf_error_convert_array[GF_ERROR_CODE_NOSR].error = ENOSR;
        gf_errno_to_error_array[ENOSR] = GF_ERROR_CODE_NOSR;

/*      ENONET  64      / * Machine is not on the network       */
        gf_error_convert_array[GF_ERROR_CODE_NONET].error = ENONET;
        gf_errno_to_error_array[ENONET] = GF_ERROR_CODE_NONET;
/*      ENOPKG  65      / * Package not installed               */
        gf_error_convert_array[GF_ERROR_CODE_NOPKG].error = ENOPKG;
        gf_errno_to_error_array[ENOPKG] = GF_ERROR_CODE_NOPKG;
/*      EREMOTE 66      / * The object is remote                        */
        gf_error_convert_array[GF_ERROR_CODE_REMOTE].error = EREMOTE;
        gf_errno_to_error_array[EREMOTE] = GF_ERROR_CODE_REMOTE;
/*      ENOLINK 67      / * the link has been severed           */
        gf_error_convert_array[GF_ERROR_CODE_NOLINK].error = ENOLINK;
        gf_errno_to_error_array[ENOLINK] = GF_ERROR_CODE_NOLINK;
/*      EADV    68      / * advertise error                     */
        gf_error_convert_array[GF_ERROR_CODE_ADV].error = EADV;
        gf_errno_to_error_array[EADV] = GF_ERROR_CODE_ADV;
/*      ESRMNT  69      / * srmount error                       */
        gf_error_convert_array[GF_ERROR_CODE_SRMNT].error = ESRMNT;
        gf_errno_to_error_array[ESRMNT] = GF_ERROR_CODE_SRMNT;

/*      ECOMM   70      / * Communication error on send         */
        gf_error_convert_array[GF_ERROR_CODE_COMM].error = ECOMM;
        gf_errno_to_error_array[ECOMM] = GF_ERROR_CODE_COMM;
/*      EPROTO  71      / * Protocol error                      */
        gf_error_convert_array[GF_ERROR_CODE_PROTO].error = EPROTO;
        gf_errno_to_error_array[EPROTO] = GF_ERROR_CODE_PROTO;

/* Interprocess Robust Locks */
/*      ELOCKUNMAPPED   72      / * locked lock was unmapped */
        gf_error_convert_array[GF_ERROR_CODE_LOCKUNMAPPED].error = ELOCKUNMAPPED;
        gf_errno_to_error_array[ELOCKUNMAPPED] = GF_ERROR_CODE_LOCKUNMAPPED;

/*      ENOTACTIVE 73   / * Facility is not active              */
        gf_error_convert_array[GF_ERROR_CODE_NOTACTIVE].error = ENOTACTIVE;
        gf_errno_to_error_array[ENOTACTIVE] = GF_ERROR_CODE_NOTACTIVE;
/*      EMULTIHOP 74    / * multihop attempted                  */
        gf_error_convert_array[GF_ERROR_CODE_MULTIHOP].error = EMULTIHOP;
        gf_errno_to_error_array[EMULTIHOP] = GF_ERROR_CODE_MULTIHOP;
/*      EBADMSG 77      / * trying to read unreadable message   */
        gf_error_convert_array[GF_ERROR_CODE_BADMSG].error = EBADMSG;
        gf_errno_to_error_array[EBADMSG] = GF_ERROR_CODE_BADMSG;
/*      ENAMETOOLONG 78 / * path name is too long               */
        gf_error_convert_array[GF_ERROR_CODE_NAMETOOLONG].error = ENAMETOOLONG;
        gf_errno_to_error_array[ENAMETOOLONG] = GF_ERROR_CODE_NAMETOOLONG;
/*      EOVERFLOW 79    / * value too large to be stored in data type */
        gf_error_convert_array[GF_ERROR_CODE_OVERFLOW].error = EOVERFLOW;
        gf_errno_to_error_array[EOVERFLOW] = GF_ERROR_CODE_OVERFLOW;
/*      ENOTUNIQ 80     / * given log. name not unique          */
        gf_error_convert_array[GF_ERROR_CODE_NOTUNIQ].error = ENOTUNIQ;
        gf_errno_to_error_array[ENOTUNIQ] = GF_ERROR_CODE_NOTUNIQ;
/*      EBADFD  81      / * f.d. invalid for this operation     */
        gf_error_convert_array[GF_ERROR_CODE_BADFD].error = EBADFD;
        gf_errno_to_error_array[EBADFD] = GF_ERROR_CODE_BADFD;
/*      EREMCHG 82      / * Remote address changed              */
        gf_error_convert_array[GF_ERROR_CODE_REMCHG].error = EREMCHG;
        gf_errno_to_error_array[EREMCHG] = GF_ERROR_CODE_REMCHG;

/* shared library problems */
/*      ELIBACC 83      / * Can't access a needed shared lib.   */
        gf_error_convert_array[GF_ERROR_CODE_LIBACC].error = ELIBACC;
        gf_errno_to_error_array[ELIBACC] = GF_ERROR_CODE_LIBACC;
/*      ELIBBAD 84      / * Accessing a corrupted shared lib.   */
        gf_error_convert_array[GF_ERROR_CODE_LIBBAD].error = ELIBBAD;
        gf_errno_to_error_array[ELIBBAD] = GF_ERROR_CODE_LIBBAD;
/*      ELIBSCN 85      / * .lib section in a.out corrupted.    */
        gf_error_convert_array[GF_ERROR_CODE_LIBSCN].error = ELIBSCN;
        gf_errno_to_error_array[ELIBSCN] = GF_ERROR_CODE_LIBSCN;
/*      ELIBMAX 86      / * Attempting to link in too many libs.        */
        gf_error_convert_array[GF_ERROR_CODE_LIBMAX].error = ELIBMAX;
        gf_errno_to_error_array[ELIBMAX] = GF_ERROR_CODE_LIBMAX;
/*      ELIBEXEC 87     / * Attempting to exec a shared library.        */
        gf_error_convert_array[GF_ERROR_CODE_LIBEXEC].error = ELIBEXEC;
        gf_errno_to_error_array[ELIBEXEC] = GF_ERROR_CODE_LIBEXEC;
/*      EILSEQ  88      / * Illegal byte sequence.              */
        gf_error_convert_array[GF_ERROR_CODE_ILSEQ].error = EILSEQ;
        gf_errno_to_error_array[EILSEQ] = GF_ERROR_CODE_ILSEQ;
/*      ENOSYS  89      / * Unsupported file system operation   */
        gf_error_convert_array[GF_ERROR_CODE_NOSYS].error = ENOSYS;
        gf_errno_to_error_array[ENOSYS] = GF_ERROR_CODE_NOSYS;
/*      ELOOP   90      / * Symbolic link loop                  */
        gf_error_convert_array[GF_ERROR_CODE_LOOP].error = ELOOP;
        gf_errno_to_error_array[ELOOP] = GF_ERROR_CODE_LOOP;
/*      ERESTART 91     / * Restartable system call             */
        gf_error_convert_array[GF_ERROR_CODE_RESTART].error = ERESTART;
        gf_errno_to_error_array[ERESTART] = GF_ERROR_CODE_RESTART;
/*      ESTRPIPE 92     / * if pipe/FIFO, don't sleep in stream head */
        gf_error_convert_array[GF_ERROR_CODE_STRPIPE].error = ESTRPIPE;
        gf_errno_to_error_array[ESTRPIPE] = GF_ERROR_CODE_STRPIPE;
/*      ENOTEMPTY 93    / * directory not empty                 */
        gf_error_convert_array[GF_ERROR_CODE_NOTEMPTY].error = ENOTEMPTY;
        gf_errno_to_error_array[ENOTEMPTY] = GF_ERROR_CODE_NOTEMPTY;
/*      EUSERS  94      / * Too many users (for UFS)            */
        gf_error_convert_array[GF_ERROR_CODE_USERS].error = EUSERS;
        gf_errno_to_error_array[EUSERS] = GF_ERROR_CODE_USERS;

/* BSD Networking Software */
        /* argument errors */
/*      ENOTSOCK        95      / * Socket operation on non-socket */
        gf_error_convert_array[GF_ERROR_CODE_NOTSOCK].error = ENOTSOCK;
        gf_errno_to_error_array[ENOTSOCK] = GF_ERROR_CODE_NOTSOCK;
/*      EDESTADDRREQ    96      / * Destination address required */
        gf_error_convert_array[GF_ERROR_CODE_DESTADDRREQ].error = EDESTADDRREQ;
        gf_errno_to_error_array[EDESTADDRREQ] = GF_ERROR_CODE_DESTADDRREQ;
/*      EMSGSIZE        97      / * Message too long */
        gf_error_convert_array[GF_ERROR_CODE_MSGSIZE].error = EMSGSIZE;
        gf_errno_to_error_array[EMSGSIZE] = GF_ERROR_CODE_MSGSIZE;
/*      EPROTOTYPE      98      / * Protocol wrong type for socket */
        gf_error_convert_array[GF_ERROR_CODE_PROTOTYPE].error = EPROTOTYPE;
        gf_errno_to_error_array[EPROTOTYPE] = GF_ERROR_CODE_PROTOTYPE;
/*      ENOPROTOOPT     99      / * Protocol not available */
        gf_error_convert_array[GF_ERROR_CODE_NOPROTOOPT].error = ENOPROTOOPT;
        gf_errno_to_error_array[ENOPROTOOPT] = GF_ERROR_CODE_NOPROTOOPT;
/*      EPROTONOSUPPORT 120     / * Protocol not supported */
        gf_error_convert_array[GF_ERROR_CODE_PROTONOSUPPORT].error = EPROTONOSUPPORT;
        gf_errno_to_error_array[EPROTONOSUPPORT] = GF_ERROR_CODE_PROTONOSUPPORT;
/*      ESOCKTNOSUPPORT 121     / * Socket type not supported */
        gf_error_convert_array[GF_ERROR_CODE_SOCKTNOSUPPORT].error = ESOCKTNOSUPPORT;
        gf_errno_to_error_array[ESOCKTNOSUPPORT] = GF_ERROR_CODE_SOCKTNOSUPPORT;

/*      EOPNOTSUPP      122     / * Operation not supported on socket */
        gf_error_convert_array[GF_ERROR_CODE_OPNOTSUPP].error = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;
/*      EPFNOSUPPORT    123     / * Protocol family not supported */
        gf_error_convert_array[GF_ERROR_CODE_PFNOSUPPORT].error = EPFNOSUPPORT;
        gf_errno_to_error_array[EPFNOSUPPORT] = GF_ERROR_CODE_PFNOSUPPORT;
/*      EAFNOSUPPORT    124     / * Address family not supported by */
        /* protocol family */
        gf_error_convert_array[GF_ERROR_CODE_AFNOSUPPORT].error = EAFNOSUPPORT;
        gf_errno_to_error_array[EAFNOSUPPORT] = GF_ERROR_CODE_AFNOSUPPORT;
/*      EADDRINUSE      125     / * Address already in use */
        gf_error_convert_array[GF_ERROR_CODE_ADDRINUSE].error = EADDRINUSE;
        gf_errno_to_error_array[EADDRINUSE] = GF_ERROR_CODE_ADDRINUSE;
/*      EADDRNOTAVAIL   126     / * Can't assign requested address */
        /* operational errors */
        gf_error_convert_array[GF_ERROR_CODE_ADDRNOTAVAIL].error = EADDRNOTAVAIL;
        gf_errno_to_error_array[EADDRNOTAVAIL] = GF_ERROR_CODE_ADDRNOTAVAIL;
/*      ENETDOWN        127     / * Network is down */
        gf_error_convert_array[GF_ERROR_CODE_NETDOWN].error = ENETDOWN;
        gf_errno_to_error_array[ENETDOWN] = GF_ERROR_CODE_NETDOWN;
/*      ENETUNREACH     128     / * Network is unreachable */
        gf_error_convert_array[GF_ERROR_CODE_NETUNREACH].error = ENETUNREACH;
        gf_errno_to_error_array[ENETUNREACH] = GF_ERROR_CODE_NETUNREACH;
/*      ENETRESET       129     / * Network dropped connection because */
        /* of reset */
        gf_error_convert_array[GF_ERROR_CODE_NETRESET].error = ENETRESET;
        gf_errno_to_error_array[ENETRESET] = GF_ERROR_CODE_NETRESET;
/*      ECONNABORTED    130     / * Software caused connection abort */
        gf_error_convert_array[GF_ERROR_CODE_CONNABORTED].error = ECONNABORTED;
        gf_errno_to_error_array[ECONNABORTED] = GF_ERROR_CODE_CONNABORTED;
/*      ECONNRESET      131     / * Connection reset by peer */
        gf_error_convert_array[GF_ERROR_CODE_CONNRESET].error = ECONNRESET;
        gf_errno_to_error_array[ECONNRESET] = GF_ERROR_CODE_CONNRESET;
/*      ENOBUFS         132     / * No buffer space available */
        gf_error_convert_array[GF_ERROR_CODE_NOBUFS].error = ENOBUFS;
        gf_errno_to_error_array[ENOBUFS] = GF_ERROR_CODE_NOBUFS;
/*      EISCONN         133     / * Socket is already connected */
        gf_error_convert_array[GF_ERROR_CODE_ISCONN].error = EISCONN;
        gf_errno_to_error_array[EISCONN] = GF_ERROR_CODE_ISCONN;
/*      ENOTCONN        134     / * Socket is not connected */
        gf_error_convert_array[GF_ERROR_CODE_NOTCONN].error = ENOTCONN;
        gf_errno_to_error_array[ENOTCONN] = GF_ERROR_CODE_NOTCONN;
/* XENIX has 135 - 142 */
/*      ESHUTDOWN       143     / * Can't send after socket shutdown */
        gf_error_convert_array[GF_ERROR_CODE_SHUTDOWN].error = ESHUTDOWN;
        gf_errno_to_error_array[ESHUTDOWN] = GF_ERROR_CODE_SHUTDOWN;
/*      ETOOMANYREFS    144     / * Too many references: can't splice */
        gf_error_convert_array[GF_ERROR_CODE_TOOMANYREFS].error = ETOOMANYREFS;
        gf_errno_to_error_array[ETOOMANYREFS] = GF_ERROR_CODE_TOOMANYREFS;
/*      ETIMEDOUT       145     / * Connection timed out */
        gf_error_convert_array[GF_ERROR_CODE_TIMEDOUT].error = ETIMEDOUT;
        gf_errno_to_error_array[ETIMEDOUT] = GF_ERROR_CODE_TIMEDOUT;

/*      ECONNREFUSED    146     / * Connection refused */
        gf_error_convert_array[GF_ERROR_CODE_CONNREFUSED].error = ECONNREFUSED;
        gf_errno_to_error_array[ECONNREFUSED] = GF_ERROR_CODE_CONNREFUSED;
/*      EHOSTDOWN       147     / * Host is down */
        gf_error_convert_array[GF_ERROR_CODE_HOSTDOWN].error = EHOSTDOWN;
        gf_errno_to_error_array[EHOSTDOWN] = GF_ERROR_CODE_HOSTDOWN;
/*      EHOSTUNREACH    148     / * No route to host */
        gf_error_convert_array[GF_ERROR_CODE_HOSTUNREACH].error = EHOSTUNREACH;
        gf_errno_to_error_array[EHOSTUNREACH] = GF_ERROR_CODE_HOSTUNREACH;
/*      EALREADY        149     / * operation already in progress */
        gf_error_convert_array[GF_ERROR_CODE_ALREADY].error = EALREADY;
        gf_errno_to_error_array[EALREADY] = GF_ERROR_CODE_ALREADY;
/*      EINPROGRESS     150     / * operation now in progress */
        gf_error_convert_array[GF_ERROR_CODE_INPROGRESS].error = EINPROGRESS;
        gf_errno_to_error_array[EINPROGRESS] = GF_ERROR_CODE_INPROGRESS;

/* SUN Network File System */
/*      ESTALE          151     / * Stale NFS file handle */
        gf_error_convert_array[GF_ERROR_CODE_STALE].error = ESTALE;
        gf_errno_to_error_array[ESTALE] = GF_ERROR_CODE_STALE;

        return ;
}
#endif /* GF_SOLARIS_HOST_OS */

#ifdef GF_DARWIN_HOST_OS
static void
init_compat_errno_arrays ()
{
        /*    EDEADLK         11              / * Resource deadlock would occur */
        gf_error_convert_array[GF_ERROR_CODE_DEADLK].error = EDEADLK;
        gf_errno_to_error_array[EDEADLK] = GF_ERROR_CODE_DEADLK;

        /*    EAGAIN          35              / * Try Again */
        gf_error_convert_array[GF_ERROR_CODE_AGAIN].error = EAGAIN;
        gf_errno_to_error_array[EAGAIN] = GF_ERROR_CODE_AGAIN;

        /*      EINPROGRESS     36              / * Operation now in progress */
        gf_error_convert_array[GF_ERROR_CODE_INPROGRESS].error = EINPROGRESS;
        gf_errno_to_error_array[EINPROGRESS] = GF_ERROR_CODE_INPROGRESS;

        /*      EALREADY        37              / * Operation already in progress */
        gf_error_convert_array[GF_ERROR_CODE_ALREADY].error = EALREADY;
        gf_errno_to_error_array[EALREADY] = GF_ERROR_CODE_ALREADY;

        /*      ENOTSOCK        38              / * Socket operation on non-socket */
        gf_error_convert_array[GF_ERROR_CODE_NOTSOCK].error = ENOTSOCK;
        gf_errno_to_error_array[ENOTSOCK] = GF_ERROR_CODE_NOTSOCK;

        /*      EDESTADDRREQ    39              / * Destination address required */
        gf_error_convert_array[GF_ERROR_CODE_DESTADDRREQ].error = EDESTADDRREQ;
        gf_errno_to_error_array[EDESTADDRREQ] = GF_ERROR_CODE_DESTADDRREQ;

        /*      EMSGSIZE        40              / * Message too long */
        gf_error_convert_array[GF_ERROR_CODE_MSGSIZE].error = EMSGSIZE;
        gf_errno_to_error_array[EMSGSIZE] = GF_ERROR_CODE_MSGSIZE;

        /*      EPROTOTYPE      41              / * Protocol wrong type for socket */
        gf_error_convert_array[GF_ERROR_CODE_PROTOTYPE].error = EPROTOTYPE;
        gf_errno_to_error_array[EPROTOTYPE] = GF_ERROR_CODE_PROTOTYPE;

        /*      ENOPROTOOPT     42              / * Protocol not available */
        gf_error_convert_array[GF_ERROR_CODE_NOPROTOOPT].error = ENOPROTOOPT;
        gf_errno_to_error_array[ENOPROTOOPT] = GF_ERROR_CODE_NOPROTOOPT;

        /*      EPROTONOSUPPORT 43              / * Protocol not supported */
        gf_error_convert_array[GF_ERROR_CODE_PROTONOSUPPORT].error = EPROTONOSUPPORT;
        gf_errno_to_error_array[EPROTONOSUPPORT] = GF_ERROR_CODE_PROTONOSUPPORT;

        /*      ESOCKTNOSUPPORT 44              / * Socket type not supported */
        gf_error_convert_array[GF_ERROR_CODE_SOCKTNOSUPPORT].error = ESOCKTNOSUPPORT;
        gf_errno_to_error_array[ESOCKTNOSUPPORT] = GF_ERROR_CODE_SOCKTNOSUPPORT;

        /*      EOPNOTSUPP      45              / * Operation not supported */
        gf_error_convert_array[GF_ERROR_CODE_OPNOTSUPP].error = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;

        /*      EPFNOSUPPORT    46              / * Protocol family not supported */
        gf_error_convert_array[GF_ERROR_CODE_PFNOSUPPORT].error = EPFNOSUPPORT;
        gf_errno_to_error_array[EPFNOSUPPORT] = GF_ERROR_CODE_PFNOSUPPORT;

        /*      EAFNOSUPPORT    47              / * Address family not supported by protocol family */
        gf_error_convert_array[GF_ERROR_CODE_AFNOSUPPORT].error = EAFNOSUPPORT;
        gf_errno_to_error_array[EAFNOSUPPORT] = GF_ERROR_CODE_AFNOSUPPORT;

        /*      EADDRINUSE      48              / * Address already in use */
        gf_error_convert_array[GF_ERROR_CODE_ADDRINUSE].error = EADDRINUSE;
        gf_errno_to_error_array[EADDRINUSE] = GF_ERROR_CODE_ADDRINUSE;

        /*      EADDRNOTAVAIL   49              / * Can't assign requested address */
        gf_error_convert_array[GF_ERROR_CODE_ADDRNOTAVAIL].error = EADDRNOTAVAIL;
        gf_errno_to_error_array[EADDRNOTAVAIL] = GF_ERROR_CODE_ADDRNOTAVAIL;

        /*      ENETDOWN        50              / * Network is down */
        gf_error_convert_array[GF_ERROR_CODE_NETDOWN].error = ENETDOWN;
        gf_errno_to_error_array[ENETDOWN] = GF_ERROR_CODE_NETDOWN;

        /*      ENETUNREACH     51              / * Network is unreachable */
        gf_error_convert_array[GF_ERROR_CODE_NETUNREACH].error = ENETUNREACH;
        gf_errno_to_error_array[ENETUNREACH] = GF_ERROR_CODE_NETUNREACH;

        /*      ENETRESET       52              / * Network dropped connection on reset */
        gf_error_convert_array[GF_ERROR_CODE_NETRESET].error = ENETRESET;
        gf_errno_to_error_array[ENETRESET] = GF_ERROR_CODE_NETRESET;

        /*      ECONNABORTED    53              / * Software caused connection abort */
        gf_error_convert_array[GF_ERROR_CODE_CONNABORTED].error = ECONNABORTED;
        gf_errno_to_error_array[ECONNABORTED] = GF_ERROR_CODE_CONNABORTED;

        /*      ECONNRESET      54              / * Connection reset by peer */
        gf_error_convert_array[GF_ERROR_CODE_CONNRESET].error = ECONNRESET;
        gf_errno_to_error_array[ECONNRESET] = GF_ERROR_CODE_CONNRESET;

        /*      ENOBUFS         55              / * No buffer space available */
        gf_error_convert_array[GF_ERROR_CODE_NOBUFS].error = ENOBUFS;
        gf_errno_to_error_array[ENOBUFS] = GF_ERROR_CODE_NOBUFS;

        /*      EISCONN         56              / * Socket is already connected */
        gf_error_convert_array[GF_ERROR_CODE_ISCONN].error = EISCONN;
        gf_errno_to_error_array[EISCONN] = GF_ERROR_CODE_ISCONN;

        /*      ENOTCONN        57              / * Socket is not connected */
        gf_error_convert_array[GF_ERROR_CODE_NOTCONN].error = ENOTCONN;
        gf_errno_to_error_array[ENOTCONN] = GF_ERROR_CODE_NOTCONN;

        /*      ESHUTDOWN       58              / * Can't send after socket shutdown */
        gf_error_convert_array[GF_ERROR_CODE_SHUTDOWN].error = ESHUTDOWN;
        gf_errno_to_error_array[ESHUTDOWN] = GF_ERROR_CODE_SHUTDOWN;

        /*      ETOOMANYREFS    59              / * Too many references: can't splice */
        gf_error_convert_array[GF_ERROR_CODE_TOOMANYREFS].error = ETOOMANYREFS;
        gf_errno_to_error_array[ETOOMANYREFS] = GF_ERROR_CODE_TOOMANYREFS;

        /*      ETIMEDOUT       60              / * Operation timed out */
        gf_error_convert_array[GF_ERROR_CODE_TIMEDOUT].error = ETIMEDOUT;
        gf_errno_to_error_array[ETIMEDOUT] = GF_ERROR_CODE_TIMEDOUT;

        /*      ECONNREFUSED    61              / * Connection refused */
        gf_error_convert_array[GF_ERROR_CODE_CONNREFUSED].error = ECONNREFUSED;
        gf_errno_to_error_array[ECONNREFUSED] = GF_ERROR_CODE_CONNREFUSED;

        /*      ELOOP           62              / * Too many levels of symbolic links */
        gf_error_convert_array[GF_ERROR_CODE_LOOP].error = ELOOP;
        gf_errno_to_error_array[ELOOP] = GF_ERROR_CODE_LOOP;

        /*      ENAMETOOLONG    63              / * File name too long */
        gf_error_convert_array[GF_ERROR_CODE_NAMETOOLONG].error = ENAMETOOLONG;
        gf_errno_to_error_array[ENAMETOOLONG] = GF_ERROR_CODE_NAMETOOLONG;

        /*      EHOSTDOWN       64              / * Host is down */
        gf_error_convert_array[GF_ERROR_CODE_HOSTDOWN].error = EHOSTDOWN;
        gf_errno_to_error_array[EHOSTDOWN] = GF_ERROR_CODE_HOSTDOWN;

        /*      EHOSTUNREACH    65              / * No route to host */
        gf_error_convert_array[GF_ERROR_CODE_HOSTUNREACH].error = EHOSTUNREACH;
        gf_errno_to_error_array[EHOSTUNREACH] = GF_ERROR_CODE_HOSTUNREACH;

        /*      ENOTEMPTY       66              / * Directory not empty */
        gf_error_convert_array[GF_ERROR_CODE_NOTEMPTY].error = ENOTEMPTY;
        gf_errno_to_error_array[ENOTEMPTY] = GF_ERROR_CODE_NOTEMPTY;

        /*      EPROCLIM        67              / * Too many processes */
        gf_error_convert_array[GF_ERROR_CODE_PROCLIM].error = EPROCLIM;
        gf_errno_to_error_array[EPROCLIM] = GF_ERROR_CODE_PROCLIM;

        /*      EUSERS          68              / * Too many users */
        gf_error_convert_array[GF_ERROR_CODE_USERS].error = EUSERS;
        gf_errno_to_error_array[EUSERS] = GF_ERROR_CODE_USERS;

        /*      EDQUOT          69              / * Disc quota exceeded */
        gf_error_convert_array[GF_ERROR_CODE_DQUOT].error = EDQUOT;
        gf_errno_to_error_array[EDQUOT] = GF_ERROR_CODE_DQUOT;

        /*      ESTALE          70              / * Stale NFS file handle */
        gf_error_convert_array[GF_ERROR_CODE_STALE].error = ESTALE;
        gf_errno_to_error_array[ESTALE] = GF_ERROR_CODE_STALE;

        /*      EREMOTE         71              / * Too many levels of remote in path */
        gf_error_convert_array[GF_ERROR_CODE_REMOTE].error = EREMOTE;
        gf_errno_to_error_array[EREMOTE] = GF_ERROR_CODE_REMOTE;

        /*      EBADRPC         72              / * RPC struct is bad */
        gf_error_convert_array[GF_ERROR_CODE_BADRPC].error = EBADRPC;
        gf_errno_to_error_array[EBADRPC] = GF_ERROR_CODE_BADRPC;

        /*      ERPCMISMATCH    73              / * RPC version wrong */
        gf_error_convert_array[GF_ERROR_CODE_RPCMISMATCH].error = ERPCMISMATCH;
        gf_errno_to_error_array[ERPCMISMATCH] = GF_ERROR_CODE_RPCMISMATCH;

        /*      EPROGUNAVAIL    74              / * RPC prog. not avail */
        gf_error_convert_array[GF_ERROR_CODE_PROGUNAVAIL].error = EPROGUNAVAIL;
        gf_errno_to_error_array[EPROGUNAVAIL] = GF_ERROR_CODE_PROGUNAVAIL;

        /*      EPROGMISMATCH   75              / * Program version wrong */
        gf_error_convert_array[GF_ERROR_CODE_PROGMISMATCH].error = EPROGMISMATCH;
        gf_errno_to_error_array[EPROGMISMATCH] = GF_ERROR_CODE_PROGMISMATCH;

        /*      EPROCUNAVAIL    76              / * Bad procedure for program */
        gf_error_convert_array[GF_ERROR_CODE_PROCUNAVAIL].error = EPROCUNAVAIL;
        gf_errno_to_error_array[EPROCUNAVAIL] = GF_ERROR_CODE_PROCUNAVAIL;

        /*      ENOLCK          77              / * No locks available */
        gf_error_convert_array[GF_ERROR_CODE_NOLCK].error = ENOLCK;
        gf_errno_to_error_array[ENOLCK] = GF_ERROR_CODE_NOLCK;

        /*      ENOSYS          78              / * Function not implemented */
        gf_error_convert_array[GF_ERROR_CODE_NOSYS].error = ENOSYS;
        gf_errno_to_error_array[ENOSYS] = GF_ERROR_CODE_NOSYS;

        /*      EFTYPE          79              / * Inappropriate file type or format */
        gf_error_convert_array[GF_ERROR_CODE_FTYPE].error = EFTYPE;
        gf_errno_to_error_array[EFTYPE] = GF_ERROR_CODE_FTYPE;

        /*      EAUTH           80              / * Authentication error */
        gf_error_convert_array[GF_ERROR_CODE_AUTH].error = EAUTH;
        gf_errno_to_error_array[EAUTH] = GF_ERROR_CODE_AUTH;

        /*      ENEEDAUTH       81              / * Need authenticator */
        gf_error_convert_array[GF_ERROR_CODE_NEEDAUTH].error = ENEEDAUTH;
        gf_errno_to_error_array[ENEEDAUTH] = GF_ERROR_CODE_NEEDAUTH;
/* Intelligent device errors */
/*      EPWROFF         82      / * Device power is off */
        gf_error_convert_array[GF_ERROR_CODE_PWROFF].error = EPWROFF;
        gf_errno_to_error_array[EPWROFF] = GF_ERROR_CODE_PWROFF;
/*      EDEVERR         83      / * Device error, e.g. paper out */
        gf_error_convert_array[GF_ERROR_CODE_DEVERR].error = EDEVERR;
        gf_errno_to_error_array[EDEVERR] = GF_ERROR_CODE_DEVERR;

        /*      EOVERFLOW       84              / * Value too large to be stored in data type */
        gf_error_convert_array[GF_ERROR_CODE_OVERFLOW].error = EOVERFLOW;
        gf_errno_to_error_array[EOVERFLOW] = GF_ERROR_CODE_OVERFLOW;

/* Program loading errors */
/*   EBADEXEC   85      / * Bad executable */
        gf_error_convert_array[GF_ERROR_CODE_BADEXEC].error = EBADEXEC;
        gf_errno_to_error_array[EBADEXEC] = GF_ERROR_CODE_BADEXEC;

/*   EBADARCH   86      / * Bad CPU type in executable */
        gf_error_convert_array[GF_ERROR_CODE_BADARCH].error = EBADARCH;
        gf_errno_to_error_array[EBADARCH] = GF_ERROR_CODE_BADARCH;

/*   ESHLIBVERS 87      / * Shared library version mismatch */
        gf_error_convert_array[GF_ERROR_CODE_SHLIBVERS].error = ESHLIBVERS;
        gf_errno_to_error_array[ESHLIBVERS] = GF_ERROR_CODE_SHLIBVERS;

/*   EBADMACHO  88      / * Malformed Macho file */
        gf_error_convert_array[GF_ERROR_CODE_BADMACHO].error = EBADMACHO;
        gf_errno_to_error_array[EBADMACHO] = GF_ERROR_CODE_BADMACHO;

#ifdef EDOOFUS
        /*    EDOOFUS           88              / * Programming error */
        gf_error_convert_array[GF_ERROR_CODE_DOOFUS].error = EDOOFUS;
        gf_errno_to_error_array[EDOOFUS] = GF_ERROR_CODE_DOOFUS;
#endif

        /*      ECANCELED       89              / * Operation canceled */
        gf_error_convert_array[GF_ERROR_CODE_CANCELED].error = ECANCELED;
        gf_errno_to_error_array[ECANCELED] = GF_ERROR_CODE_CANCELED;

        /*   EIDRM              90              / * Identifier removed */
        gf_error_convert_array[GF_ERROR_CODE_IDRM].error = EIDRM;
        gf_errno_to_error_array[EIDRM] = GF_ERROR_CODE_IDRM;
        /*   ENOMSG             91              / * No message of desired type */
        gf_error_convert_array[GF_ERROR_CODE_NOMSG].error = ENOMSG;
        gf_errno_to_error_array[ENOMSG] = GF_ERROR_CODE_NOMSG;

        /*   EILSEQ             92              / * Illegal byte sequence */
        gf_error_convert_array[GF_ERROR_CODE_ILSEQ].error = EILSEQ;
        gf_errno_to_error_array[EILSEQ] = GF_ERROR_CODE_ILSEQ;

        /*   ENOATTR            93              / * Attribute not found */
        gf_error_convert_array[GF_ERROR_CODE_NOATTR].error = ENOATTR;
        gf_errno_to_error_array[ENOATTR] = GF_ERROR_CODE_NOATTR;

        /*   EBADMSG            94              / * Bad message */
        gf_error_convert_array[GF_ERROR_CODE_BADMSG].error = EBADMSG;
        gf_errno_to_error_array[EBADMSG] = GF_ERROR_CODE_BADMSG;

        /*   EMULTIHOP  95              / * Reserved */
        gf_error_convert_array[GF_ERROR_CODE_MULTIHOP].error = EMULTIHOP;
        gf_errno_to_error_array[EMULTIHOP] = GF_ERROR_CODE_MULTIHOP;

        /*      ENODATA         96              / * No message available on STREAM */
        gf_error_convert_array[GF_ERROR_CODE_NEEDAUTH].error = ENEEDAUTH;
        gf_errno_to_error_array[ENEEDAUTH] = GF_ERROR_CODE_NEEDAUTH;

        /*   ENOLINK            97              / * Reserved */
        gf_error_convert_array[GF_ERROR_CODE_NOLINK].error = ENOLINK;
        gf_errno_to_error_array[ENOLINK] = GF_ERROR_CODE_NOLINK;

        /*   ENOSR              98              / * No STREAM resources */
        gf_error_convert_array[GF_ERROR_CODE_NOSR].error = ENOSR;
        gf_errno_to_error_array[ENOSR] = GF_ERROR_CODE_NOSR;

        /*   ENOSTR             99              / * Not a STREAM */
        gf_error_convert_array[GF_ERROR_CODE_NOSTR].error = ENOSTR;
        gf_errno_to_error_array[ENOSTR] = GF_ERROR_CODE_NOSTR;

/*      EPROTO          100             / * Protocol error */
        gf_error_convert_array[GF_ERROR_CODE_PROTO].error = EPROTO;
        gf_errno_to_error_array[EPROTO] = GF_ERROR_CODE_PROTO;
/*   ETIME              101             / * STREAM ioctl timeout */
        gf_error_convert_array[GF_ERROR_CODE_TIME].error = ETIME;
        gf_errno_to_error_array[ETIME] = GF_ERROR_CODE_TIME;

/* This value is only discrete when compiling __DARWIN_UNIX03, or KERNEL */
/*      EOPNOTSUPP      102             / * Operation not supported on socket */
        gf_error_convert_array[GF_ERROR_CODE_OPNOTSUPP].error = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;

/*   ENOPOLICY  103             / * No such policy registered */
        gf_error_convert_array[GF_ERROR_CODE_NOPOLICY].error = ENOPOLICY;
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
        gf_error_convert_array[GF_ERROR_CODE_AGAIN].error = EAGAIN;
        gf_errno_to_error_array[EAGAIN] = GF_ERROR_CODE_AGAIN;

        /*    EDEADLK         11              / * Resource deadlock would occur */
        gf_error_convert_array[GF_ERROR_CODE_DEADLK].error = EDEADLK;
        gf_errno_to_error_array[EDEADLK] = GF_ERROR_CODE_DEADLK;

        /*      EINPROGRESS     36              / * Operation now in progress */
        gf_error_convert_array[GF_ERROR_CODE_INPROGRESS].error = EINPROGRESS;
        gf_errno_to_error_array[EINPROGRESS] = GF_ERROR_CODE_INPROGRESS;

        /*      EALREADY        37              / * Operation already in progress */
        gf_error_convert_array[GF_ERROR_CODE_ALREADY].error = EALREADY;
        gf_errno_to_error_array[EALREADY] = GF_ERROR_CODE_ALREADY;

        /*      ENOTSOCK        38              / * Socket operation on non-socket */
        gf_error_convert_array[GF_ERROR_CODE_NOTSOCK].error = ENOTSOCK;
        gf_errno_to_error_array[ENOTSOCK] = GF_ERROR_CODE_NOTSOCK;

        /*      EDESTADDRREQ    39              / * Destination address required */
        gf_error_convert_array[GF_ERROR_CODE_DESTADDRREQ].error = EDESTADDRREQ;
        gf_errno_to_error_array[EDESTADDRREQ] = GF_ERROR_CODE_DESTADDRREQ;

        /*      EMSGSIZE        40              / * Message too long */
        gf_error_convert_array[GF_ERROR_CODE_MSGSIZE].error = EMSGSIZE;
        gf_errno_to_error_array[EMSGSIZE] = GF_ERROR_CODE_MSGSIZE;

        /*      EPROTOTYPE      41              / * Protocol wrong type for socket */
        gf_error_convert_array[GF_ERROR_CODE_PROTOTYPE].error = EPROTOTYPE;
        gf_errno_to_error_array[EPROTOTYPE] = GF_ERROR_CODE_PROTOTYPE;

        /*      ENOPROTOOPT     42              / * Protocol not available */
        gf_error_convert_array[GF_ERROR_CODE_NOPROTOOPT].error = ENOPROTOOPT;
        gf_errno_to_error_array[ENOPROTOOPT] = GF_ERROR_CODE_NOPROTOOPT;

        /*      EPROTONOSUPPORT 43              / * Protocol not supported */
        gf_error_convert_array[GF_ERROR_CODE_PROTONOSUPPORT].error = EPROTONOSUPPORT;
        gf_errno_to_error_array[EPROTONOSUPPORT] = GF_ERROR_CODE_PROTONOSUPPORT;

        /*      ESOCKTNOSUPPORT 44              / * Socket type not supported */
        gf_error_convert_array[GF_ERROR_CODE_SOCKTNOSUPPORT].error = ESOCKTNOSUPPORT;
        gf_errno_to_error_array[ESOCKTNOSUPPORT] = GF_ERROR_CODE_SOCKTNOSUPPORT;

        /*      EOPNOTSUPP      45              / * Operation not supported */
        gf_error_convert_array[GF_ERROR_CODE_OPNOTSUPP].error = EOPNOTSUPP;
        gf_errno_to_error_array[EOPNOTSUPP] = GF_ERROR_CODE_OPNOTSUPP;

        /*      EPFNOSUPPORT    46              / * Protocol family not supported */
        gf_error_convert_array[GF_ERROR_CODE_PFNOSUPPORT].error = EPFNOSUPPORT;
        gf_errno_to_error_array[EPFNOSUPPORT] = GF_ERROR_CODE_PFNOSUPPORT;

        /*      EAFNOSUPPORT    47              / * Address family not supported by protocol family */
        gf_error_convert_array[GF_ERROR_CODE_AFNOSUPPORT].error = EAFNOSUPPORT;
        gf_errno_to_error_array[EAFNOSUPPORT] = GF_ERROR_CODE_AFNOSUPPORT;

        /*      EADDRINUSE      48              / * Address already in use */
        gf_error_convert_array[GF_ERROR_CODE_ADDRINUSE].error = EADDRINUSE;
        gf_errno_to_error_array[EADDRINUSE] = GF_ERROR_CODE_ADDRINUSE;

        /*      EADDRNOTAVAIL   49              / * Can't assign requested address */
        gf_error_convert_array[GF_ERROR_CODE_ADDRNOTAVAIL].error = EADDRNOTAVAIL;
        gf_errno_to_error_array[EADDRNOTAVAIL] = GF_ERROR_CODE_ADDRNOTAVAIL;

        /*      ENETDOWN        50              / * Network is down */
        gf_error_convert_array[GF_ERROR_CODE_NETDOWN].error = ENETDOWN;
        gf_errno_to_error_array[ENETDOWN] = GF_ERROR_CODE_NETDOWN;

        /*      ENETUNREACH     51              / * Network is unreachable */
        gf_error_convert_array[GF_ERROR_CODE_NETUNREACH].error = ENETUNREACH;
        gf_errno_to_error_array[ENETUNREACH] = GF_ERROR_CODE_NETUNREACH;

        /*      ENETRESET       52              / * Network dropped connection on reset */
        gf_error_convert_array[GF_ERROR_CODE_NETRESET].error = ENETRESET;
        gf_errno_to_error_array[ENETRESET] = GF_ERROR_CODE_NETRESET;

        /*      ECONNABORTED    53              / * Software caused connection abort */
        gf_error_convert_array[GF_ERROR_CODE_CONNABORTED].error = ECONNABORTED;
        gf_errno_to_error_array[ECONNABORTED] = GF_ERROR_CODE_CONNABORTED;

        /*      ECONNRESET      54              / * Connection reset by peer */
        gf_error_convert_array[GF_ERROR_CODE_CONNRESET].error = ECONNRESET;
        gf_errno_to_error_array[ECONNRESET] = GF_ERROR_CODE_CONNRESET;

        /*      ENOBUFS         55              / * No buffer space available */
        gf_error_convert_array[GF_ERROR_CODE_NOBUFS].error = ENOBUFS;
        gf_errno_to_error_array[ENOBUFS] = GF_ERROR_CODE_NOBUFS;

        /*      EISCONN         56              / * Socket is already connected */
        gf_error_convert_array[GF_ERROR_CODE_ISCONN].error = EISCONN;
        gf_errno_to_error_array[EISCONN] = GF_ERROR_CODE_ISCONN;

        /*      ENOTCONN        57              / * Socket is not connected */
        gf_error_convert_array[GF_ERROR_CODE_NOTCONN].error = ENOTCONN;
        gf_errno_to_error_array[ENOTCONN] = GF_ERROR_CODE_NOTCONN;

        /*      ESHUTDOWN       58              / * Can't send after socket shutdown */
        gf_error_convert_array[GF_ERROR_CODE_SHUTDOWN].error = ESHUTDOWN;
        gf_errno_to_error_array[ESHUTDOWN] = GF_ERROR_CODE_SHUTDOWN;

        /*      ETOOMANYREFS    59              / * Too many references: can't splice */
        gf_error_convert_array[GF_ERROR_CODE_TOOMANYREFS].error = ETOOMANYREFS;
        gf_errno_to_error_array[ETOOMANYREFS] = GF_ERROR_CODE_TOOMANYREFS;

        /*      ETIMEDOUT       60              / * Operation timed out */
        gf_error_convert_array[GF_ERROR_CODE_TIMEDOUT].error = ETIMEDOUT;
        gf_errno_to_error_array[ETIMEDOUT] = GF_ERROR_CODE_TIMEDOUT;

        /*      ECONNREFUSED    61              / * Connection refused */
        gf_error_convert_array[GF_ERROR_CODE_CONNREFUSED].error = ECONNREFUSED;
        gf_errno_to_error_array[ECONNREFUSED] = GF_ERROR_CODE_CONNREFUSED;

        /*      ELOOP           62              / * Too many levels of symbolic links */
        gf_error_convert_array[GF_ERROR_CODE_LOOP].error = ELOOP;
        gf_errno_to_error_array[ELOOP] = GF_ERROR_CODE_LOOP;

        /*      ENAMETOOLONG    63              / * File name too long */
        gf_error_convert_array[GF_ERROR_CODE_NAMETOOLONG].error = ENAMETOOLONG;
        gf_errno_to_error_array[ENAMETOOLONG] = GF_ERROR_CODE_NAMETOOLONG;

        /*      EHOSTDOWN       64              / * Host is down */
        gf_error_convert_array[GF_ERROR_CODE_HOSTDOWN].error = EHOSTDOWN;
        gf_errno_to_error_array[EHOSTDOWN] = GF_ERROR_CODE_HOSTDOWN;

        /*      EHOSTUNREACH    65              / * No route to host */
        gf_error_convert_array[GF_ERROR_CODE_HOSTUNREACH].error = EHOSTUNREACH;
        gf_errno_to_error_array[EHOSTUNREACH] = GF_ERROR_CODE_HOSTUNREACH;

        /*      ENOTEMPTY       66              / * Directory not empty */
        gf_error_convert_array[GF_ERROR_CODE_NOTEMPTY].error = ENOTEMPTY;
        gf_errno_to_error_array[ENOTEMPTY] = GF_ERROR_CODE_NOTEMPTY;

        /*      EPROCLIM        67              / * Too many processes */
        gf_error_convert_array[GF_ERROR_CODE_PROCLIM].error = EPROCLIM;
        gf_errno_to_error_array[EPROCLIM] = GF_ERROR_CODE_PROCLIM;

        /*      EUSERS          68              / * Too many users */
        gf_error_convert_array[GF_ERROR_CODE_USERS].error = EUSERS;
        gf_errno_to_error_array[EUSERS] = GF_ERROR_CODE_USERS;

        /*      EDQUOT          69              / * Disc quota exceeded */
        gf_error_convert_array[GF_ERROR_CODE_DQUOT].error = EDQUOT;
        gf_errno_to_error_array[EDQUOT] = GF_ERROR_CODE_DQUOT;

        /*      ESTALE          70              / * Stale NFS file handle */
        gf_error_convert_array[GF_ERROR_CODE_STALE].error = ESTALE;
        gf_errno_to_error_array[ESTALE] = GF_ERROR_CODE_STALE;

        /*      EREMOTE         71              / * Too many levels of remote in path */
        gf_error_convert_array[GF_ERROR_CODE_REMOTE].error = EREMOTE;
        gf_errno_to_error_array[EREMOTE] = GF_ERROR_CODE_REMOTE;

        /*      EBADRPC         72              / * RPC struct is bad */
        gf_error_convert_array[GF_ERROR_CODE_BADRPC].error = EBADRPC;
        gf_errno_to_error_array[EBADRPC] = GF_ERROR_CODE_BADRPC;

        /*      ERPCMISMATCH    73              / * RPC version wrong */
        gf_error_convert_array[GF_ERROR_CODE_RPCMISMATCH].error = ERPCMISMATCH;
        gf_errno_to_error_array[ERPCMISMATCH] = GF_ERROR_CODE_RPCMISMATCH;

        /*      EPROGUNAVAIL    74              / * RPC prog. not avail */
        gf_error_convert_array[GF_ERROR_CODE_PROGUNAVAIL].error = EPROGUNAVAIL;
        gf_errno_to_error_array[EPROGUNAVAIL] = GF_ERROR_CODE_PROGUNAVAIL;

        /*      EPROGMISMATCH   75              / * Program version wrong */
        gf_error_convert_array[GF_ERROR_CODE_PROGMISMATCH].error = EPROGMISMATCH;
        gf_errno_to_error_array[EPROGMISMATCH] = GF_ERROR_CODE_PROGMISMATCH;

        /*      EPROCUNAVAIL    76              / * Bad procedure for program */
        gf_error_convert_array[GF_ERROR_CODE_PROCUNAVAIL].error = EPROCUNAVAIL;
        gf_errno_to_error_array[EPROCUNAVAIL] = GF_ERROR_CODE_PROCUNAVAIL;

        /*      ENOLCK          77              / * No locks available */
        gf_error_convert_array[GF_ERROR_CODE_NOLCK].error = ENOLCK;
        gf_errno_to_error_array[ENOLCK] = GF_ERROR_CODE_NOLCK;

        /*      ENOSYS          78              / * Function not implemented */
        gf_error_convert_array[GF_ERROR_CODE_NOSYS].error = ENOSYS;
        gf_errno_to_error_array[ENOSYS] = GF_ERROR_CODE_NOSYS;

        /*      EFTYPE          79              / * Inappropriate file type or format */
        gf_error_convert_array[GF_ERROR_CODE_FTYPE].error = EFTYPE;
        gf_errno_to_error_array[EFTYPE] = GF_ERROR_CODE_FTYPE;

        /*      EAUTH           80              / * Authentication error */
        gf_error_convert_array[GF_ERROR_CODE_AUTH].error = EAUTH;
        gf_errno_to_error_array[EAUTH] = GF_ERROR_CODE_AUTH;

        /*      ENEEDAUTH       81              / * Need authenticator */
        gf_error_convert_array[GF_ERROR_CODE_NEEDAUTH].error = ENEEDAUTH;
        gf_errno_to_error_array[ENEEDAUTH] = GF_ERROR_CODE_NEEDAUTH;

        /*      EIDRM           82              / * Identifier removed */
        gf_error_convert_array[GF_ERROR_CODE_IDRM].error = EIDRM;
        gf_errno_to_error_array[EIDRM] = GF_ERROR_CODE_IDRM;

        /*      ENOMSG          83              / * No message of desired type */
        gf_error_convert_array[GF_ERROR_CODE_NOMSG].error = ENOMSG;
        gf_errno_to_error_array[ENOMSG] = GF_ERROR_CODE_NOMSG;

        /*      EOVERFLOW       84              / * Value too large to be stored in data type */
        gf_error_convert_array[GF_ERROR_CODE_OVERFLOW].error = EOVERFLOW;
        gf_errno_to_error_array[EOVERFLOW] = GF_ERROR_CODE_OVERFLOW;

        /*      ECANCELED       85              / * Operation canceled */
        gf_error_convert_array[GF_ERROR_CODE_CANCELED].error = ECANCELED;
        gf_errno_to_error_array[ECANCELED] = GF_ERROR_CODE_CANCELED;

        /*      EILSEQ          86              / * Illegal byte sequence */
        gf_error_convert_array[GF_ERROR_CODE_ILSEQ].error = EILSEQ;
        gf_errno_to_error_array[EILSEQ] = GF_ERROR_CODE_ILSEQ;

        /*      ENOATTR         87              / * Attribute not found */
        gf_error_convert_array[GF_ERROR_CODE_NOATTR].error = ENOATTR;
        gf_errno_to_error_array[ENOATTR] = GF_ERROR_CODE_NOATTR;

#ifdef EDOOFUS
        /*    EDOOFUS           88              / * Programming error */
        gf_error_convert_array[GF_ERROR_CODE_DOOFUS].error = EDOOFUS;
        gf_errno_to_error_array[EDOOFUS] = GF_ERROR_CODE_DOOFUS;
#endif

        /*      EBADMSG         89              / * Bad message */
        gf_error_convert_array[GF_ERROR_CODE_BADMSG].error = EBADMSG;
        gf_errno_to_error_array[EBADMSG] = GF_ERROR_CODE_BADMSG;

#ifdef __NetBSD__
        /*      ENODATA         89              / * No message available */
        gf_error_convert_array[GF_ERROR_CODE_NODATA].error = ENODATA;
        gf_errno_to_error_array[ENODATA] = GF_ERROR_CODE_NODATA;
#endif

        /*      EMULTIHOP       90              / * Multihop attempted */
        gf_error_convert_array[GF_ERROR_CODE_MULTIHOP].error = EMULTIHOP;
        gf_errno_to_error_array[EMULTIHOP] = GF_ERROR_CODE_MULTIHOP;

        /*      ENOLINK         91              / * Link has been severed */
        gf_error_convert_array[GF_ERROR_CODE_NOLINK].error = ENOLINK;
        gf_errno_to_error_array[ENOLINK] = GF_ERROR_CODE_NOLINK;

        /*      EPROTO          92              / * Protocol error */
        gf_error_convert_array[GF_ERROR_CODE_PROTO].error = EPROTO;
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
        }
        /* Now change the order if it needs to be. */
        init_compat_errno_arrays();

        return;
}

int32_t
gf_errno_to_error (int32_t op_errno)
{
        if ((op_errno > GF_ERROR_CODE_SUCCESS) && (op_errno < GF_ERROR_CODE_UNKNOWN))
                return gf_errno_to_error_array[op_errno];

        return op_errno;
}


int32_t
gf_error_to_errno (int32_t error)
{
        if ((error > GF_ERROR_CODE_SUCCESS) && (error < GF_ERROR_CODE_UNKNOWN))
                return gf_error_convert_array[error].error;

        return error;
}


int
gf_error_code_init(void)
{
        init_errno_arrays ();

        return 0;
}

char *
gf_strerror(int error)
{
        if ((error > GF_ERROR_CODE_SUCCESS) && (error < GF_ERROR_CODE_UNKNOWN)) {
                if (gf_error_convert_array[error].error_msg)
                        return gf_error_convert_array[error].error_msg;
                else
                        return strerror(gf_error_convert_array[error].error);
        }

        return strerror (error);
}
