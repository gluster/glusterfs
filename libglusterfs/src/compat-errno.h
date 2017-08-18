/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __COMPAT_ERRNO_H__
#define __COMPAT_ERRNO_H__

#include <errno.h>

#define GF_ERROR_CODE_SUCCESS         0
#define GF_ERROR_CODE_UNKNOWN         1024
#define GF_ERRNO_UNKNOWN              1024

typedef enum {

        GF_ERROR_CODE_PERM                      = 1,      /* Operation not permitted */
        GF_ERROR_CODE_NOENT                     = 2,      /* No such file or directory */
        GF_ERROR_CODE_SRCH                      = 3,      /* No such process */
        GF_ERROR_CODE_INTR                      = 4,      /* Interrupted system call */
        GF_ERROR_CODE_IO                        = 5,      /* I/O error */
        GF_ERROR_CODE_NXIO                      = 6,      /* No such device or address */
        GF_ERROR_CODE_2BIG                      = 7,      /* Argument list too long */
        GF_ERROR_CODE_NOEXEC                    = 8,      /* Exec format error */
        GF_ERROR_CODE_BADF                      = 9,      /* Bad file number */
        GF_ERROR_CODE_CHILD                     = 10,      /* No child processes */
        GF_ERROR_CODE_AGAIN                     = 11,      /* Try again */
        GF_ERROR_CODE_NOMEM                     = 12,      /* Out of memory */
        GF_ERROR_CODE_ACCES                     = 13,      /* Permission denied */
        GF_ERROR_CODE_FAULT                     = 14,      /* Bad address */
        GF_ERROR_CODE_NOTBLK                    = 15,      /* Block device required */
        GF_ERROR_CODE_BUSY                      = 16,      /* Device or resource busy */
        GF_ERROR_CODE_EXIST                     = 17,      /* File exists */
        GF_ERROR_CODE_XDEV                      = 18,      /* Cross-device link */
        GF_ERROR_CODE_NODEV                     = 19,      /* No such device */
        GF_ERROR_CODE_NOTDIR                    = 20,      /* Not a directory */
        GF_ERROR_CODE_ISDIR                     = 21,      /* Is a directory */
        GF_ERROR_CODE_INVAL                     = 22,      /* Invalid argument */
        GF_ERROR_CODE_NFILE                     = 23,      /* File table overflow */
        GF_ERROR_CODE_MFILE                     = 24,      /* Too many open files */
        GF_ERROR_CODE_NOTTY                     = 25,      /* Not a typewriter */
        GF_ERROR_CODE_TXTBSY                    = 26,      /* Text file busy */
        GF_ERROR_CODE_FBIG                      = 27,      /* File too large */
        GF_ERROR_CODE_NOSPC                     = 28,      /* No space left on device */
        GF_ERROR_CODE_SPIPE                     = 29,      /* Illegal seek */
        GF_ERROR_CODE_ROFS                      = 30,      /* Read-only file system */
        GF_ERROR_CODE_MLINK                     = 31,      /* Too many links */
        GF_ERROR_CODE_PIPE                      = 32,      /* Broken pipe */
        GF_ERROR_CODE_DOM                       = 33,      /* Math argument out of domain of func */
        GF_ERROR_CODE_RANGE                     = 34,      /* Math result not representable */
        GF_ERROR_CODE_DEADLK                    = 35,      /* Resource deadlock would occur */
        GF_ERROR_CODE_NAMETOOLONG               = 36,      /* File name too long */
        GF_ERROR_CODE_NOLCK                     = 37,      /* No record locks available */
        GF_ERROR_CODE_NOSYS                     = 38,      /* Function not implemented */
        GF_ERROR_CODE_NOTEMPTY                  = 39,      /* Directory not empty */
        GF_ERROR_CODE_LOOP                      = 40,      /* Too many symbolic links encountered */

        GF_ERROR_CODE_NOMSG                     = 42,      /* No message of desired type */
        GF_ERROR_CODE_IDRM                      = 43,      /* Identifier removed */
        GF_ERROR_CODE_CHRNG                     = 44,      /* Channel number out of range */
        GF_ERROR_CODE_L2NSYNC                   = 45,      /* Level 2 not synchronized */
        GF_ERROR_CODE_L3HLT                     = 46,      /* Level 3 halted */
        GF_ERROR_CODE_L3RST                     = 47,      /* Level 3 reset */
        GF_ERROR_CODE_LNRNG                     = 48,      /* Link number out of range */
        GF_ERROR_CODE_UNATCH                    = 49,      /* Protocol driver not attached */
        GF_ERROR_CODE_NOCSI                     = 50,      /* No CSI structure available */
        GF_ERROR_CODE_L2HLT                     = 51,      /* Level 2 halted */
        GF_ERROR_CODE_BADE                      = 52,      /* Invalid exchange */
        GF_ERROR_CODE_BADR                      = 53,      /* Invalid request descriptor */
        GF_ERROR_CODE_XFULL                     = 54,      /* Exchange full */
        GF_ERROR_CODE_NOANO                     = 55,      /* No anode */
        GF_ERROR_CODE_BADRQC                    = 56,      /* Invalid request code */
        GF_ERROR_CODE_BADSLT                    = 57,      /* Invalid slot */
        GF_ERROR_CODE_BFONT                     = 59,      /* Bad font file format */
        GF_ERROR_CODE_NOSTR                     = 60,      /* Device not a stream */
        GF_ERROR_CODE_NODATA                    = 61,      /* No data available */
        GF_ERROR_CODE_TIME                      = 62,      /* Timer expired */
        GF_ERROR_CODE_NOSR                      = 63,      /* Out of streams resources */
        GF_ERROR_CODE_NONET                     = 64,      /* Machine is not on the network */
        GF_ERROR_CODE_NOPKG                     = 65,      /* Package not installed */
        GF_ERROR_CODE_REMOTE                    = 66,      /* Object is remote */
        GF_ERROR_CODE_NOLINK                    = 67,      /* Link has been severed */
        GF_ERROR_CODE_ADV                       = 68,      /* Advertise error */
        GF_ERROR_CODE_SRMNT                     = 69,      /* Srmount error */
        GF_ERROR_CODE_COMM                      = 70,      /* Communication error on send */
        GF_ERROR_CODE_PROTO                     = 71,      /* Protocol error */
        GF_ERROR_CODE_MULTIHOP                  = 72,      /* Multihop attempted */
        GF_ERROR_CODE_DOTDOT                    = 73,      /* RFS specific error */
        GF_ERROR_CODE_BADMSG                    = 74,      /* Not a data message */
        GF_ERROR_CODE_OVERFLOW                  = 75,      /* Value too large for defined data type */
        GF_ERROR_CODE_NOTUNIQ                   = 76,      /* Name not unique on network */
        GF_ERROR_CODE_BADFD                     = 77,      /* File descriptor in bad state */
        GF_ERROR_CODE_REMCHG                    = 78,      /* Remote address changed */
        GF_ERROR_CODE_LIBACC                    = 79,      /* Can not access a needed shared library */
        GF_ERROR_CODE_LIBBAD                    = 80,      /* Accessing a corrupted shared library */
        GF_ERROR_CODE_LIBSCN                    = 81,      /* .lib section in a.out corrupted */
        GF_ERROR_CODE_LIBMAX                    = 82,      /* Attempting to link in too many shared libraries */
        GF_ERROR_CODE_LIBEXEC                   = 83,      /* Cannot exec a shared library directly */
        GF_ERROR_CODE_ILSEQ                     = 84,      /* Illegal byte sequence */
        GF_ERROR_CODE_RESTART                   = 85,      /* Interrupted system call should be restarted */
        GF_ERROR_CODE_STRPIPE                   = 86,      /* Streams pipe error */
        GF_ERROR_CODE_USERS                     = 87,      /* Too many users */
        GF_ERROR_CODE_NOTSOCK                   = 88,      /* Socket operation on non-socket */
        GF_ERROR_CODE_DESTADDRREQ               = 89,      /* Destination address required */
        GF_ERROR_CODE_MSGSIZE                   = 90,      /* Message too long */
        GF_ERROR_CODE_PROTOTYPE                 = 91,      /* Protocol wrong type for socket */
        GF_ERROR_CODE_NOPROTOOPT                = 92,      /* Protocol not available */
        GF_ERROR_CODE_PROTONOSUPPORT            = 93,      /* Protocol not supported */
        GF_ERROR_CODE_SOCKTNOSUPPORT            = 94,      /* Socket type not supported */
        GF_ERROR_CODE_OPNOTSUPP                 = 95,      /* Operation not supported on transport endpoint */
        GF_ERROR_CODE_PFNOSUPPORT               = 96,      /* Protocol family not supported */
        GF_ERROR_CODE_AFNOSUPPORT               = 97,      /* Address family not supported by protocol */
        GF_ERROR_CODE_ADDRINUSE                 = 98,      /* Address already in use */
        GF_ERROR_CODE_ADDRNOTAVAIL              = 99,      /* Cannot assign requested address */
        GF_ERROR_CODE_NETDOWN                   = 100,     /* Network is down */
        GF_ERROR_CODE_NETUNREACH                = 101,     /* Network is unreachable */
        GF_ERROR_CODE_NETRESET                  = 102,     /* Network dropped connection because of reset */
        GF_ERROR_CODE_CONNABORTED               = 103,     /* Software caused connection abort */
        GF_ERROR_CODE_CONNRESET                 = 104,     /* Connection reset by peer */
        GF_ERROR_CODE_NOBUFS                    = 105,     /* No buffer space available */
        GF_ERROR_CODE_ISCONN                    = 106,     /* Transport endpoint is already connected */
        GF_ERROR_CODE_NOTCONN                   = 107,     /* Transport endpoint is not connected */
        GF_ERROR_CODE_SHUTDOWN                  = 108,     /* Cannot send after transport endpoint shutdown */
        GF_ERROR_CODE_TOOMANYREFS               = 109,     /* Too many references: cannot splice */
        GF_ERROR_CODE_TIMEDOUT                  = 110,     /* Connection timed out */
        GF_ERROR_CODE_CONNREFUSED               = 111,     /* Connection refused */
        GF_ERROR_CODE_HOSTDOWN                  = 112,     /* Host is down */
        GF_ERROR_CODE_HOSTUNREACH               = 113,     /* No route to host */
        GF_ERROR_CODE_ALREADY                   = 114,     /* Operation already in progress */
        GF_ERROR_CODE_INPROGRESS                = 115,     /* Operation now in progress */
        GF_ERROR_CODE_STALE                     = 116,     /* Stale NFS file handle */
        GF_ERROR_CODE_UCLEAN                    = 117,     /* Structure needs cleaning */
        GF_ERROR_CODE_NOTNAM                    = 118,     /* Not a XENIX named type file */
        GF_ERROR_CODE_NAVAIL                    = 119,     /* No XENIX semaphores available */
        GF_ERROR_CODE_ISNAM                     = 120,     /* Is a named type file */
        GF_ERROR_CODE_REMOTEIO                  = 121,     /* Remote I/O error */
        GF_ERROR_CODE_DQUOT                     = 122,     /* Quota exceeded */
        GF_ERROR_CODE_NOMEDIUM                  = 123,     /* No medium found */
        GF_ERROR_CODE_MEDIUMTYPE                = 124,     /* Wrong medium type */
        GF_ERROR_CODE_CANCELED                  = 125,     /* Operation Canceled */
        GF_ERROR_CODE_NOKEY                     = 126,     /* Required key not available */
        GF_ERROR_CODE_KEYEXPIRED                = 127,     /* Key has expired */
        GF_ERROR_CODE_KEYREVOKED                = 128,     /* Key has been revoked */
        GF_ERROR_CODE_KEYREJECTED               = 129,     /* Key was rejected by service */

/* for robust mutexes */
        GF_ERROR_CODE_OWNERDEAD                 = 130,     /* Owner died */
        GF_ERROR_CODE_NOTRECOVERABLE            = 131,     /* State not recoverable */



/* Should never be seen by user programs */
        GF_ERROR_CODE_RESTARTSYS                = 512,
        GF_ERROR_CODE_RESTARTNOINTR             = 513,
        GF_ERROR_CODE_RESTARTNOHAND             = 514,     /* restart if no handler.. */
        GF_ERROR_CODE_NOIOCTLCMD                = 515,     /* No ioctl command */
        GF_ERROR_CODE_RESTART_RESTARTBLOCK      = 516,     /* restart by calling sys_restart_syscall */

/* Defined for the NFSv3 protocol */
        GF_ERROR_CODE_BADHANDLE                 = 521,     /* Illegal NFS file handle */
        GF_ERROR_CODE_NOTSYNC                   = 522,     /* Update synchronization mismatch */
        GF_ERROR_CODE_BADCOOKIE                 = 523,     /* Cookie is stale */
        GF_ERROR_CODE_NOTSUPP                   = 524,     /* Operation is not supported */
        GF_ERROR_CODE_TOOSMALL                  = 525,     /* Buffer or request is too small */
        GF_ERROR_CODE_SERVERFAULT               = 526,     /* An untranslatable error occurred */
        GF_ERROR_CODE_BADTYPE                   = 527,     /* Type not supported by server */
        GF_ERROR_CODE_JUKEBOX                   = 528,     /* Request initiated, but will not complete before timeout */
        GF_ERROR_CODE_IOCBQUEUED                = 529,     /* iocb queued, will get completion event */
        GF_ERROR_CODE_IOCBRETRY                 = 530,     /* iocb queued, will trigger a retry */

/* Darwin OS X */
        GF_ERROR_CODE_NOPOLICY                  = 701,
        GF_ERROR_CODE_BADMACHO                  = 702,
        GF_ERROR_CODE_PWROFF                    = 703,
        GF_ERROR_CODE_DEVERR                    = 704,
        GF_ERROR_CODE_BADARCH                   = 705,
        GF_ERROR_CODE_BADEXEC                   = 706,
        GF_ERROR_CODE_SHLIBVERS                 = 707,



/* Solaris */
/*      ENOTACTIVE 73   / * Facility is not active      */
        GF_ERROR_CODE_NOTACTIVE                 = 801,
/*      ELOCKUNMAPPED   72      / * locked lock was unmapped */
        GF_ERROR_CODE_LOCKUNMAPPED              = 802,

/* BSD system */
        GF_ERROR_CODE_PROCLIM                   = 901,          /* Too many processes */
        GF_ERROR_CODE_BADRPC                    = 902,          /* RPC struct is bad */
        GF_ERROR_CODE_RPCMISMATCH               = 903,          /* RPC version wrong */
        GF_ERROR_CODE_PROGUNAVAIL               = 904,          /* RPC prog. not avail */
        GF_ERROR_CODE_PROGMISMATCH              = 905,          /* Program version wrong */
        GF_ERROR_CODE_PROCUNAVAIL               = 905,          /* Bad procedure for program */
        GF_ERROR_CODE_FTYPE                     = 906,          /* Inappropriate file type or format */
        GF_ERROR_CODE_AUTH                      = 907,          /* Authentication error */
        GF_ERROR_CODE_NEEDAUTH                  = 908,          /* Need authenticator */
        GF_ERROR_CODE_DOOFUS                    = 909,          /* Programming error */

} gf_error_codes_t;

#define GF_ERROR_CODE_NOATTR GF_ERROR_CODE_NODATA  /* Attribute not found */

/* Either one of enodata or enoattr will be there in system */
#ifndef ENOATTR
#define ENOATTR ENODATA
#endif /* ENOATTR */

#ifndef ENODATA
#define ENODATA ENOATTR
#endif /* ENODATA */

#ifndef EBADFD
#define EBADFD EBADRPC
#endif /* EBADFD */

#if !defined(ENODATA)
/* This happens on FreeBSD.  Value borrowed from Linux. */
#define ENODATA 61
#endif

/* These functions are defined for all the OS flags, but content will
 * be different for each OS flag.
 */
int32_t gf_errno_to_error (int32_t op_errno);
int32_t gf_error_to_errno (int32_t error);
int gf_error_code_init(void);
char *gf_strerror(int error);

#endif /* __COMPAT_ERRNO_H__ */
