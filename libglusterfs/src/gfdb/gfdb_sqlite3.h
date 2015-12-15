/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GFDB_SQLITE3_H
#define __GFDB_SQLITE3_H


/*Sqlite3 header file*/
#include <sqlite3.h>

#include "logging.h"
#include "gfdb_data_store_types.h"
#include "gfdb_mem-types.h"
#include "libglusterfs-messages.h"

#define GF_STMT_SIZE_MAX        2048

#define GF_DB_NAME              "gfdb.db"
#define GF_FILE_TABLE           "GF_FILE_TB"
#define GF_FILE_LINK_TABLE      "GF_FLINK_TB"
#define GF_MASTER_TABLE         "sqlite_master"

/*Since we have multiple tables to be created we put it in a transaction*/
#define GF_CREATE_STMT(out_str)\
do {\
        sprintf (out_str , "BEGIN; CREATE TABLE IF NOT EXISTS "\
                GF_FILE_TABLE\
                "(GF_ID TEXT PRIMARY KEY NOT NULL, "\
                "W_SEC INTEGER NOT NULL DEFAULT 0, "\
                "W_MSEC INTEGER NOT NULL DEFAULT 0, "\
                "UW_SEC INTEGER NOT NULL DEFAULT 0, "\
                "UW_MSEC INTEGER NOT NULL DEFAULT 0, "\
                "W_READ_SEC INTEGER NOT NULL DEFAULT 0, "\
                "W_READ_MSEC INTEGER NOT NULL DEFAULT 0, "\
                "UW_READ_SEC INTEGER NOT NULL DEFAULT 0, "\
                "UW_READ_MSEC INTEGER NOT NULL DEFAULT 0, "\
                "WRITE_FREQ_CNTR INTEGER NOT NULL DEFAULT 1, "\
                "READ_FREQ_CNTR INTEGER NOT NULL DEFAULT 1); "\
                "CREATE TABLE IF NOT EXISTS "\
                GF_FILE_LINK_TABLE\
                "(GF_ID TEXT NOT NULL, "\
                "GF_PID TEXT NOT NULL, "\
                "FNAME TEXT NOT NULL, "\
                "W_DEL_FLAG INTEGER NOT NULL DEFAULT 0, "\
                "LINK_UPDATE INTEGER NOT NULL DEFAULT 0, "\
                "PRIMARY KEY ( GF_ID, GF_PID, FNAME) "\
                ");"\
                "COMMIT;"\
                );;\
} while (0)

#define GF_COL_TB_WSEC          GF_FILE_TABLE "." GF_COL_WSEC
#define GF_COL_TB_WMSEC         GF_FILE_TABLE "." GF_COL_WMSEC
#define GF_COL_TB_UWSEC         GF_FILE_TABLE "." GF_COL_UWSEC
#define GF_COL_TB_UWMSEC        GF_FILE_TABLE "." GF_COL_UWMSEC
#define GF_COL_TB_RWSEC         GF_FILE_TABLE "." GF_COL_WSEC_READ
#define GF_COL_TB_RWMSEC        GF_FILE_TABLE "." GF_COL_WMSEC_READ
#define GF_COL_TB_RUWSEC        GF_FILE_TABLE "." GF_COL_UWSEC_READ
#define GF_COL_TB_RUWMSEC       GF_FILE_TABLE "." GF_COL_UWMSEC_READ
#define GF_COL_TB_WFC           GF_FILE_TABLE "." GF_COL_WRITE_FREQ_CNTR
#define GF_COL_TB_RFC           GF_FILE_TABLE "." GF_COL_READ_FREQ_CNTR


/*******************************************************************************
*                      SQLITE3 Connection details and PRAGMA
* ****************************************************************************/

#define GF_SQL_AV_NONE  "none"
#define GF_SQL_AV_FULL  "full"
#define GF_SQL_AV_INCR  "incr"


#define GF_SQL_SYNC_OFF         "off"
#define GF_SQL_SYNC_NORMAL      "normal"
#define GF_SQL_SYNC_FULL        "full"

#define GF_SQL_JM_DELETE        "delete"
#define GF_SQL_JM_TRUNCATE      "truncate"
#define GF_SQL_JM_PERSIST       "persist"
#define GF_SQL_JM_MEMORY        "memory"
#define GF_SQL_JM_WAL           "wal"
#define GF_SQL_JM_OFF           "off"


typedef enum gf_sql_auto_vacuum {
        gf_sql_av_none = 0,
        gf_sql_av_full,
        gf_sql_av_incr,
        gf_sql_av_invalid
} gf_sql_auto_vacuum_t;

typedef enum gf_sql_sync {
        gf_sql_sync_off = 0,
        gf_sql_sync_normal,
        gf_sql_sync_full,
        gf_sql_sync_invalid
} gf_sql_sync_t;


typedef enum gf_sql_journal_mode {
        gf_sql_jm_wal = 0,
        gf_sql_jm_delete,
        gf_sql_jm_truncate,
        gf_sql_jm_persist,
        gf_sql_jm_memory,
        gf_sql_jm_off,
        gf_sql_jm_invalid
} gf_sql_journal_mode_t;


typedef struct gf_sql_connection {
        char                    sqlite3_db_path[PATH_MAX];
        sqlite3                 *sqlite3_db_conn;
        ssize_t                 cache_size;
        ssize_t                 page_size;
        ssize_t                 wal_autocheckpoint;
        gf_sql_journal_mode_t   journal_mode;
        gf_sql_sync_t           synchronous;
        gf_sql_auto_vacuum_t    auto_vacuum;
} gf_sql_connection_t;



#define CHECK_SQL_CONN(sql_conn, out)\
do {\
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, sql_conn, out);\
        if (!sql_conn->sqlite3_db_conn) {\
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,\
                        LG_MSG_CONNECTION_INIT_FAILED,\
                        "sqlite3 connection not initialized");\
                goto out;\
        };\
} while (0)

#define GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, param_key, format, value,\
                        ret, error)\
do {\
        sprintf (sqlite3_config_str, "PRAGMA %s = " format ,  param_key,\
                value);\
        ret = sqlite3_exec (sql_conn->sqlite3_db_conn, sqlite3_config_str,\
                NULL, NULL, NULL);\
        if (ret != SQLITE_OK) {\
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_EXEC_FAILED,\
                        "Failed executing: %s : %s",\
                        sqlite3_config_str, sqlite3_errmsg\
                                               (sql_conn->sqlite3_db_conn));\
                        ret = -1;\
                        goto error;\
        };\
} while (0)

/************************SQLITE3 PARAMS KEYS***********************************/
#define GFDB_SQL_PARAM_DBPATH           "sql-db-path"
#define GFDB_SQL_PARAM_CACHE_SIZE       "sql-db-cachesize"
#define GFDB_SQL_PARAM_PAGE_SIZE        "sql-db-pagesize"
#define GFDB_SQL_PARAM_JOURNAL_MODE     "sql-db-journalmode"
#define GFDB_SQL_PARAM_WAL_AUTOCHECK    "sql-db-wal-autocheckpoint"
#define GFDB_SQL_PARAM_SYNC             "sql-db-sync"
#define GFDB_SQL_PARAM_AUTO_VACUUM      "sql-db-autovacuum"

#define GF_SQL_DEFAULT_DBPATH                   ""
#define GF_SQL_DEFAULT_PAGE_SIZE                "4096"
#define GF_SQL_DEFAULT_CACHE_SIZE               "1000"
#define GF_SQL_DEFAULT_WAL_AUTOCHECKPOINT       "1000"
#define GF_SQL_DEFAULT_JOURNAL_MODE             GF_SQL_JM_WAL
#define GF_SQL_DEFAULT_SYNC                     GF_SQL_SYNC_OFF
#define GF_SQL_DEFAULT_AUTO_VACUUM              GF_SQL_AV_NONE


/* Defines the indexs for sqlite params
 * The order should be maintained*/
typedef enum sqlite_param_index {
        sql_dbpath_ix = 0,
        sql_pagesize_ix,
        sql_cachesize_ix,
        sql_journalmode_ix,
        sql_walautocheck_ix,
        sql_dbsync_ix,
        sql_autovacuum_ix,
        /*This should be in the end*/
        sql_index_max
} sqlite_param_index_t;

/* Array to hold the sqlite param keys
 * The order should be maintained as sqlite_param_index_t*/
static char *sqlite_params_keys[] = {
        GFDB_SQL_PARAM_DBPATH,
        GFDB_SQL_PARAM_PAGE_SIZE,
        GFDB_SQL_PARAM_CACHE_SIZE,
        GFDB_SQL_PARAM_JOURNAL_MODE,
        GFDB_SQL_PARAM_WAL_AUTOCHECK,
        GFDB_SQL_PARAM_SYNC,
        GFDB_SQL_PARAM_AUTO_VACUUM
};


/* Array of default values for sqlite params
 * The order should be maintained as sqlite_param_index_t*/
static char *sqlite_params_default_value[] = {
        GF_SQL_DEFAULT_DBPATH,
        GF_SQL_DEFAULT_PAGE_SIZE,
        GF_SQL_DEFAULT_CACHE_SIZE,
        GF_SQL_DEFAULT_JOURNAL_MODE,
        GF_SQL_DEFAULT_WAL_AUTOCHECKPOINT,
        GF_SQL_DEFAULT_SYNC,
        GF_SQL_DEFAULT_AUTO_VACUUM
};

/*Extract sql params from page_size to auto_vacumm
 * The dbpath is extracted in a different way*/
static inline int
gfdb_set_sql_params(char *comp_name, dict_t *from_dict, dict_t *to_dict)
{
        sqlite_param_index_t sql_index  = sql_pagesize_ix;
        char *_val_str                  = NULL;
        int ret                      = -1;

        GF_ASSERT (comp_name);
        GF_ASSERT (from_dict);
        GF_ASSERT (to_dict);

        /*Extact and Set of the sql params from page_size*/
        for (sql_index = sql_pagesize_ix; sql_index < sql_index_max;
                sql_index++) {
                _val_str = NULL;
                GET_DB_PARAM_FROM_DICT_DEFAULT (comp_name, from_dict,
                        sqlite_params_keys[sql_index], _val_str,
                        sqlite_params_default_value[sql_index]);
                SET_DB_PARAM_TO_DICT (comp_name, to_dict,
                        sqlite_params_keys[sql_index], _val_str, ret, out);
        }
out:
        return ret;
}




/*************************SQLITE3 GFDB PLUGINS*********************************/

/*Db init and fini modules*/
int gf_sqlite3_fini (void **db_conn);
int gf_sqlite3_init (dict_t *args, void **db_conn);

/*insert/update/delete modules*/
int gf_sqlite3_insert (void *db_conn, gfdb_db_record_t *);
int gf_sqlite3_delete (void *db_conn, gfdb_db_record_t *);

/*querying modules*/
int gf_sqlite3_find_all (void *db_conn, gf_query_callback_t,
                        void *_query_cbk_args);
int gf_sqlite3_find_unchanged_for_time (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_query_cbk_args,
                                        gfdb_time_t *for_time);
int gf_sqlite3_find_recently_changed_files (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_query_cbk_args,
                                        gfdb_time_t *from_time);
int gf_sqlite3_find_unchanged_for_time_freq (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_query_cbk_args,
                                        gfdb_time_t *for_time,
                                        int write_freq_cnt,
                                        int read_freq_cnt,
                                        gf_boolean_t clear_counters);
int gf_sqlite3_find_recently_changed_files_freq (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_query_cbk_args,
                                        gfdb_time_t *from_time,
                                        int write_freq_cnt,
                                        int read_freq_cnt,
                                        gf_boolean_t clear_counters);

int gf_sqlite3_clear_files_heat (void *db_conn);

/* Function to extract version of sqlite db
 * Input:
 * void *db_conn        : Sqlite connection
 * char **version  : the version is extracted as a string and will be stored in
 *                   this variable. The freeing of the memory should be done by
 *                   the caller.
 * Return:
 *      On success return the lenght of the version string that is
 *      extracted.
 *      On failure return -1
 * */
int gf_sqlite3_version (void *db_conn, char **version);

/* Function to extract PRAGMA or setting from sqlite db
 * Input:
 * void *db_conn        : Sqlite connection
 * char *pragma_key     : PRAGMA or setting to be extracted
 * char **pragma_value  : the value of the PRAGMA or setting that is
 *                        extracted. This function will allocate memory
 *                        to pragma_value. The caller should free the memory
 * Return:
 *      On success return the lenght of the pragma/setting value that is
 *      extracted.
 *      On failure return -1
 * */
int gf_sqlite3_pragma (void *db_conn, char *pragma_key, char **pragma_value);

/* Function to set PRAGMA to sqlite db
 * Input:
 * void *db_conn        : Sqlite connection
 * char *pragma_key     : PRAGMA to be set
 * char *pragma_value   : the value of the PRAGMA
 * Return:
 *      On success return 0
 *      On failure return -1
 * */
int
gf_sqlite3_set_pragma (void *db_conn, char *pragma_key, char *pragma_value);



void gf_sqlite3_fill_db_operations (gfdb_db_operations_t  *gfdb_db_ops);


#endif
