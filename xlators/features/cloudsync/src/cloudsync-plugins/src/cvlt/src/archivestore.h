/*
  Copyright (c) 2018 Commvault Systems, Inc. <http://www.commvault.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __ARCHIVESTORE_H__
#define __ARCHIVESTORE_H__

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <dlfcn.h>
#include <uuid/uuid.h>

#define CS_XATTR_ARCHIVE_UUID "trusted.cloudsync.uuid"
#define CS_XATTR_PRODUCT_ID "trusted.cloudsync.product-id"
#define CS_XATTR_STORE_ID "trusted.cloudsync.store-id"

struct _archstore_methods;
typedef struct _archstore_methods archstore_methods_t;

struct _archstore_desc {
    void *priv; /* Private field for store mgmt.   */
                /* To be used only by archive store*/
};
typedef struct _archstore_desc archstore_desc_t;

struct _archstore_info {
    char *id;         /* Identifier for the archivestore */
    uint32_t idlen;   /* Length of identifier string     */
    char *prod;       /* Name of the data mgmt. product  */
    uint32_t prodlen; /* Length of the product string    */
};
typedef struct _archstore_info archstore_info_t;

struct _archstore_fileinfo {
    uuid_t uuid;         /* uuid of the file                */
    char *path;          /* file path                       */
    uint32_t pathlength; /* length of file path             */
};
typedef struct _archstore_fileinfo archstore_fileinfo_t;

struct _app_callback_info {
    archstore_info_t *src_archstore;
    archstore_fileinfo_t *src_archfile;
    archstore_info_t *dest_archstore;
    archstore_fileinfo_t *dest_archfile;
};
typedef struct _app_callback_info app_callback_info_t;

typedef void (*app_callback_t)(archstore_desc_t *, app_callback_info_t *,
                               void *, int64_t, int32_t);

enum _archstore_scan_type { FULL = 1, INCREMENTAL = 2 };
typedef enum _archstore_scan_type archstore_scan_type_t;

typedef int32_t archstore_errno_t;

/*
 * Initialize archive store.
 * arg1  pointer to structure containing archive store information
 * arg2  error number if any generated during the initialization
 * arg3  name of the log file
 */
typedef int32_t (*init_archstore_t)(archstore_desc_t *, archstore_errno_t *,
                                    const char *);

/*
 * Clean up archive store.
 * arg1  pointer to structure containing archive store information
 * arg2  error number if any generated during the cleanup
 */
typedef int32_t (*term_archstore_t)(archstore_desc_t *, archstore_errno_t *);

/*
 * Read the contents of the file from archive store
 * arg1  pointer to structure containing archive store description
 * arg2  pointer to structure containing archive store information
 * arg3  pointer to structure containing information about file to be read
 * arg4  offset in the file from which data should be read
 * arg5  buffer where the data should be read
 * arg6  number of bytes of data to be read
 * arg7  error number if any generated during the read from file
 * arg8  callback handler to be invoked after the data is read
 * arg9  cookie to be passed when callback is invoked
 */
typedef int32_t (*read_archstore_t)(archstore_desc_t *, archstore_info_t *,
                                    archstore_fileinfo_t *, off_t, char *,
                                    size_t, archstore_errno_t *, app_callback_t,
                                    void *);

/*
 * Restore the contents of the file from archive store
 * This is basically in-place restore
 * arg1  pointer to structure containing archive store description
 * arg2  pointer to structure containing archive store information
 * arg3  pointer to structure containing information about file to be restored
 * arg4  error number if any generated during the file restore
 * arg5  callback to be invoked after the file is restored
 * arg6  cookie to be passed when callback is invoked
 */
typedef int32_t (*recall_archstore_t)(archstore_desc_t *, archstore_info_t *,
                                      archstore_fileinfo_t *,
                                      archstore_errno_t *, app_callback_t,
                                      void *);

/*
 * Restore the contents of the file from archive store to a different store
 * This is basically out-of-place restore
 * arg1  pointer to structure containing archive store description
 * arg2  pointer to structure containing source archive store information
 * arg3  pointer to structure containing information about file to be restored
 * arg4  pointer to structure containing destination archive store information
 * arg5  pointer to structure containing information about the location to
         which the file will be restored
 * arg6  error number if any generated during the file restore
 * arg7  callback to be invoked after the file is restored
 * arg8  cookie to be passed when callback is invoked
 */
typedef int32_t (*restore_archstore_t)(archstore_desc_t *, archstore_info_t *,
                                       archstore_fileinfo_t *,
                                       archstore_info_t *,
                                       archstore_fileinfo_t *,
                                       archstore_errno_t *, app_callback_t,
                                       void *);

/*
 * Archive the contents of the file to archive store
 * arg1  pointer to structure containing archive store description
 * arg2  pointer to structure containing source archive store information
 * arg3  pointer to structure containing information about files to be archived
 * arg4  pointer to structure containing destination archive store information
 * arg5  pointer to structure containing information about files that failed
 *       to be archived
 * arg6  error number if any generated during the file archival
 * arg7  callback to be invoked after the file is archived
 * arg8  cookie to be passed when callback is invoked
 */
typedef int32_t (*archive_archstore_t)(archstore_desc_t *, archstore_info_t *,
                                       archstore_fileinfo_t *,
                                       archstore_info_t *,
                                       archstore_fileinfo_t *,
                                       archstore_errno_t *, app_callback_t,
                                       void *);

/*
 * Backup list of files provided in the input file
 * arg1  pointer to structure containing archive store description
 * arg2  pointer to structure containing source archive store information
 * arg3  pointer to structure containing information about files to be backed up
 * arg4  pointer to structure containing destination archive store information
 * arg5  pointer to structure containing information about files that failed
 *       to be backed up
 * arg6  error number if any generated during the file archival
 * arg7  callback to be invoked after the file is archived
 * arg8  cookie to be passed when callback is invoked
 */
typedef int32_t (*backup_archstore_t)(archstore_desc_t *, archstore_info_t *,
                                      archstore_fileinfo_t *,
                                      archstore_info_t *,
                                      archstore_fileinfo_t *,
                                      archstore_errno_t *, app_callback_t,
                                      void *);

/*
 * Scan the contents of a store and determine the files which need to be
 * backed up.
 * arg1  pointer to structure containing archive store description
 * arg2  pointer to structure containing archive store information
 * arg3  type of scan whether full or incremental
 * arg4  path to file that contains list of files to be backed up
 * arg5  error number if any generated during scan operation
 */
typedef int32_t (*scan_archstore_t)(archstore_desc_t *, archstore_info_t *,
                                    archstore_scan_type_t, char *,
                                    archstore_errno_t *);

struct _archstore_methods {
    init_archstore_t init;
    term_archstore_t fini;
    backup_archstore_t backup;
    archive_archstore_t archive;
    scan_archstore_t scan;
    restore_archstore_t restore;
    recall_archstore_t recall;
    read_archstore_t read;
};

typedef int (*get_archstore_methods_t)(archstore_methods_t *);

/*
 * Single function that will be invoked by applications for extracting
 * the function pointers to all data management functions.
 */
int32_t
get_archstore_methods(archstore_methods_t *);

#endif /* End of __ARCHIVESTORE_H__ */
