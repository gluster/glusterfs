#ifndef _TIER_CTR_INTERFACE_H_
#define _TIER_CTR_INTERFACE_H_

#include "common-utils.h"
#include "gfdb_data_store_types.h"

#define GFDB_IPC_CTR_KEY "gfdb.ipc-ctr-op"

/*
 * CTR IPC OPERATIONS
 *
 *
 */
#define GFDB_IPC_CTR_QUERY_OPS "gfdb.ipc-ctr-query-op"
#define GFDB_IPC_CTR_CLEAR_OPS "gfdb.ipc-ctr-clear-op"
#define GFDB_IPC_CTR_GET_DB_PARAM_OPS "gfdb.ipc-ctr-get-db-parm"
#define GFDB_IPC_CTR_GET_DB_VERSION_OPS "gfdb.ipc-ctr-get-db-version"
#define GFDB_IPC_CTR_SET_COMPACT_PRAGMA "gfdb.ipc-ctr-set-compact-pragma"
/*
 * CTR IPC INPUT/OUTPUT
 *
 *
 */
#define GFDB_IPC_CTR_GET_QFILE_PATH "gfdb.ipc-ctr-get-qfile-path"
#define GFDB_IPC_CTR_GET_QUERY_PARAMS "gfdb.ipc-ctr-get-query-parms"
#define GFDB_IPC_CTR_RET_QUERY_COUNT "gfdb.ipc-ctr-ret-rec-count"
#define GFDB_IPC_CTR_GET_DB_KEY "gfdb.ipc-ctr-get-params-key"
#define GFDB_IPC_CTR_RET_DB_VERSION "gfdb.ipc-ctr-ret-db-version"

/*
 * gfdb ipc ctr params for query
 *
 *
 */
typedef struct gfdb_ipc_ctr_params {
        gf_boolean_t is_promote;
        int write_freq_threshold;
        int read_freq_threshold;
        gfdb_time_t time_stamp;
        int query_limit;
        gf_boolean_t emergency_demote;
} gfdb_ipc_ctr_params_t;

#endif
