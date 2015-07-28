/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "gfdb_sqlite3.h"
#include "gfdb_sqlite3_helper.h"
#include "libglusterfs-messages.h"

/******************************************************************************
 *
 *                      Util functions
 *
 * ***************************************************************************/
gf_sql_connection_t *
gf_sql_connection_init ()
{
        gf_sql_connection_t *gf_sql_conn = NULL;

        gf_sql_conn = GF_CALLOC (1, sizeof(gf_sql_connection_t),
                        gf_mt_sql_connection_t);
        if (gf_sql_conn == NULL) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, ENOMEM,
                        LG_MSG_NO_MEMORY, "Error allocating memory to "
                        "gf_sql_connection_t ");
        }

        return gf_sql_conn;
}

void
gf_sql_connection_fini (gf_sql_connection_t **sql_connection)
{
        if (!sql_connection)
                return;
        GF_FREE (*sql_connection);
        *sql_connection = NULL;
}

const char *
gf_sql_jm2str (gf_sql_journal_mode_t jm)
{
        switch (jm) {
        case gf_sql_jm_delete:
                return GF_SQL_JM_DELETE;
        case gf_sql_jm_truncate:
                return GF_SQL_JM_TRUNCATE;
        case gf_sql_jm_persist:
                return GF_SQL_JM_PERSIST;
        case gf_sql_jm_memory:
                return GF_SQL_JM_MEMORY;
        case gf_sql_jm_wal:
                return GF_SQL_JM_WAL;
        case gf_sql_jm_off:
                return GF_SQL_JM_OFF;
        case gf_sql_jm_invalid:
                break;
        }
        return NULL;
}

gf_sql_journal_mode_t
gf_sql_str2jm (const char *jm_str)
{
        if (!jm_str) {
                return gf_sql_jm_invalid;
        } else if (strcmp(jm_str, GF_SQL_JM_DELETE) == 0) {
                return gf_sql_jm_delete;
        } else if (strcmp(jm_str, GF_SQL_JM_TRUNCATE) == 0) {
                return gf_sql_jm_truncate;
        } else if (strcmp(jm_str, GF_SQL_JM_PERSIST) == 0) {
                return gf_sql_jm_persist;
        } else if (strcmp(jm_str, GF_SQL_JM_MEMORY) == 0) {
                return gf_sql_jm_memory;
        } else if (strcmp(jm_str, GF_SQL_JM_WAL) == 0) {
                return gf_sql_jm_wal;
        } else if (strcmp(jm_str, GF_SQL_JM_OFF) == 0) {
                return gf_sql_jm_off;
        }
        return gf_sql_jm_invalid;
}

const char *
gf_sql_av_t2str (gf_sql_auto_vacuum_t sql_av)
{
        switch (sql_av) {
        case gf_sql_av_none:
                return GF_SQL_AV_NONE;
        case gf_sql_av_full:
                return GF_SQL_AV_FULL;
        case gf_sql_av_incr:
                return GF_SQL_AV_INCR;
        case gf_sql_av_invalid:
                break;
        }
        return NULL;
}

gf_sql_auto_vacuum_t
gf_sql_str2av_t (const char *av_str)
{
        if (!av_str) {
                return gf_sql_sync_invalid;
        } else if (strcmp(av_str, GF_SQL_AV_NONE) == 0) {
                return gf_sql_av_none;
        } else if (strcmp(av_str, GF_SQL_AV_FULL) == 0) {
                return gf_sql_av_full;
        } else if (strcmp(av_str, GF_SQL_AV_INCR) == 0) {
                return gf_sql_av_incr;
        }
        return gf_sql_sync_invalid;
}

const char *
gf_sync_t2str (gf_sql_sync_t sql_sync)
{
        switch (sql_sync) {
        case gf_sql_sync_off:
                return GF_SQL_SYNC_OFF;
        case gf_sql_sync_normal:
                return GF_SQL_SYNC_NORMAL;
        case gf_sql_sync_full:
                return GF_SQL_SYNC_FULL;
        case gf_sql_sync_invalid:
                break;
        }
        return NULL;
}

gf_sql_sync_t
gf_sql_str2sync_t (const char *sync_str)
{
        if (!sync_str) {
                return gf_sql_sync_invalid;
        } else if (strcmp(sync_str, GF_SQL_SYNC_OFF) == 0) {
                return gf_sql_sync_off;
        } else if (strcmp(sync_str, GF_SQL_SYNC_NORMAL) == 0) {
                return gf_sql_sync_normal;
        } else if (strcmp(sync_str, GF_SQL_SYNC_FULL) == 0) {
                return gf_sql_sync_full;
        }
        return gf_sql_sync_invalid;
}


/*TODO replace GF_CALLOC by mem_pool or iobuff if required for performace */
static char *
sql_stmt_init ()
{
        char *sql_stmt = NULL;

        sql_stmt = GF_CALLOC (GF_STMT_SIZE_MAX, sizeof(char),
                        gf_common_mt_char);

        if (!sql_stmt) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, ENOMEM,
                        LG_MSG_NO_MEMORY, "Error allocating memory to SQL "
                        "Statement ");
                goto out;
        }
out:
        return sql_stmt;
}

/*TODO replace GF_FREE by mem_pool or iobuff if required for performace */
static void
sql_stmt_fini (char **sql_stmt)
{
        GF_FREE (*sql_stmt);
}

/******************************************************************************
 *                      DB Essential functions used by
 *                      > gf_open_sqlite3_conn ()
 *                      > gf_close_sqlite3_conn ()
 * ***************************************************************************/
static sqlite3 *
gf_open_sqlite3_conn(char *sqlite3_db_path, int flags)
{
        sqlite3 *sqlite3_db_conn = NULL;
        int ret = -1;

        GF_ASSERT (sqlite3_db_path);

        /*Creates DB if not created*/
        ret = sqlite3_open_v2(sqlite3_db_path, &sqlite3_db_conn, flags, NULL);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_DB_ERROR,
                        "FATAL: Could open %s : %s",
                        sqlite3_db_path, sqlite3_errmsg(sqlite3_db_conn));
        }
        return sqlite3_db_conn;
}

static int
gf_close_sqlite3_conn(sqlite3 *sqlite3_db_conn)
{
        int ret = 0;

        GF_ASSERT (sqlite3_db_conn);

        if (sqlite3_db_conn) {
                ret = sqlite3_close (sqlite3_db_conn);
                if (ret != SQLITE_OK) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_CONNECTION_ERROR, "FATAL: sqlite3 close"
                                " connection failed %s",
                                sqlite3_errmsg(sqlite3_db_conn));
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

/******************************************************************************
*
*                      Database init / fini / create table
*
* ***************************************************************************/


/*Function to fill db operations*/
void
gf_sqlite3_fill_db_operations(gfdb_db_operations_t  *gfdb_db_ops)
{
        GF_ASSERT (gfdb_db_ops);

        gfdb_db_ops->init_db_op = gf_sqlite3_init;
        gfdb_db_ops->fini_db_op = gf_sqlite3_fini;

        gfdb_db_ops->insert_record_op = gf_sqlite3_insert;
        gfdb_db_ops->delete_record_op = gf_sqlite3_delete;

        gfdb_db_ops->find_all_op = gf_sqlite3_find_all;
        gfdb_db_ops->find_unchanged_for_time_op =
                        gf_sqlite3_find_unchanged_for_time;
        gfdb_db_ops->find_recently_changed_files_op =
                        gf_sqlite3_find_recently_changed_files;
        gfdb_db_ops->find_unchanged_for_time_freq_op =
                        gf_sqlite3_find_unchanged_for_time_freq;
        gfdb_db_ops->find_recently_changed_files_freq_op =
                        gf_sqlite3_find_recently_changed_files_freq;
}


static int
create_filetable (sqlite3 *sqlite3_db_conn)
{
        int ret                         =       -1;
        char *sql_stmt                  =       NULL;
        char *sql_strerror              =       NULL;

        GF_ASSERT(sqlite3_db_conn);

        sql_stmt = sql_stmt_init();
        if (!sql_stmt) {
                ret = ENOMEM;
                goto out;
        }

        GF_CREATE_STMT(sql_stmt);

        ret = sqlite3_exec (sqlite3_db_conn, sql_stmt, NULL, NULL,
                                &sql_strerror);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_EXEC_FAILED,
                        "Failed executing: %s : %s", sql_stmt, sql_strerror);
                sqlite3_free (sql_strerror);
                        ret = -1;
                        goto out;
        }


        ret = 0;
out:
        sql_stmt_fini (&sql_stmt);
        return ret;
}




static int
apply_sql_params_db(gf_sql_connection_t *sql_conn, dict_t *param_dict)
{
        int ret = -1;
        char *temp_str = NULL;
        char sqlite3_config_str[PATH_MAX] = "";

        GF_ASSERT(sql_conn);
        GF_ASSERT(param_dict);

        /*Extract sql page_size from param_dict,
         * if not specified default value will be GF_SQL_DEFAULT_PAGE_SIZE*/
        temp_str = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(GFDB_STR_SQLITE3, param_dict,
                        GFDB_SQL_PARAM_PAGE_SIZE, temp_str,
                        GF_SQL_DEFAULT_PAGE_SIZE);
        sql_conn->page_size = atoi(temp_str);
        /*Apply page_size on the sqlite db*/
        GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, "page_size", "%ld",
                        sql_conn->page_size, ret, out);



        /*Extract sql cache size from param_dict,
         * if not specified default value will be
         * GF_SQL_DEFAULT_CACHE_SIZE pages*/
        temp_str = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(GFDB_STR_SQLITE3, param_dict,
                        GFDB_SQL_PARAM_CACHE_SIZE, temp_str,
                        GF_SQL_DEFAULT_CACHE_SIZE);
        sql_conn->cache_size = atoi(temp_str);
        /*Apply cache size on the sqlite db*/
        GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, "cache_size", "%ld",
                        sql_conn->cache_size, ret, out);




        /*Extract sql journal mode from param_dict,
         * if not specified default value will be
         * GF_SQL_DEFAULT_JOURNAL_MODE i.e "wal"*/
        temp_str = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(GFDB_STR_SQLITE3, param_dict,
                        GFDB_SQL_PARAM_JOURNAL_MODE, temp_str,
                        GF_SQL_DEFAULT_JOURNAL_MODE);
        sql_conn->journal_mode = gf_sql_str2jm (temp_str);
        /*Apply journal mode to the sqlite db*/
        GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, "journal_mode", "%s",
                        temp_str, ret, out);



        /*Only when the journal mode is WAL, wal_autocheckpoint makes sense*/
        if (sql_conn->journal_mode == gf_sql_jm_wal) {
                /*Extract sql wal auto check point from param_dict
                * if not specified default value will be
                * GF_SQL_DEFAULT_WAL_AUTOCHECKPOINT pages*/
                temp_str = NULL;
                GET_DB_PARAM_FROM_DICT_DEFAULT(GFDB_STR_SQLITE3, param_dict,
                        GFDB_SQL_PARAM_WAL_AUTOCHECK, temp_str,
                        GF_SQL_DEFAULT_WAL_AUTOCHECKPOINT);
                sql_conn->wal_autocheckpoint = atoi(temp_str);
                /*Apply wal auto check point to the sqlite db*/
                GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, "wal_autocheckpoint",
                        "%ld", sql_conn->wal_autocheckpoint, ret, out);
        }



        /*Extract sql synchronous from param_dict
         * if not specified default value will be GF_SQL_DEFAULT_SYNC*/
         temp_str = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(GFDB_STR_SQLITE3, param_dict,
                        GFDB_SQL_PARAM_SYNC, temp_str, GF_SQL_DEFAULT_SYNC);
        sql_conn->synchronous = gf_sql_str2sync_t (temp_str);
        /*Apply synchronous to the sqlite db*/
        GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, "synchronous", "%d",
                        sql_conn->synchronous, ret, out);



        /*Extract sql auto_vacuum from param_dict
         * if not specified default value will be GF_SQL_DEFAULT_AUTO_VACUUM*/
         temp_str = NULL;
        GET_DB_PARAM_FROM_DICT_DEFAULT(GFDB_STR_SQLITE3, param_dict,
                        GFDB_SQL_PARAM_AUTO_VACUUM, temp_str,
                        GF_SQL_DEFAULT_AUTO_VACUUM);
        sql_conn->auto_vacuum = gf_sql_str2av_t (temp_str);
        /*Apply auto_vacuum to the sqlite db*/
        GF_SQLITE3_SET_PRAGMA(sqlite3_config_str, "auto_vacuum", "%d",
                        sql_conn->auto_vacuum, ret, out);

        ret = 0;
out:
        return ret;
}



int
gf_sqlite3_init (dict_t *args, void **db_conn) {
        int ret                         = -1;
        gf_sql_connection_t *sql_conn   = NULL;
        struct stat stbuf               = {0,};
        gf_boolean_t    is_dbfile_exist = _gf_false;
        char *temp_str                  = NULL;

        GF_ASSERT (args);
        GF_ASSERT (db_conn);

        if (*db_conn != NULL) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_CONNECTION_ERROR, "DB Connection is not "
                        "empty!");
                return 0;
        }

        if (!sqlite3_threadsafe()) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_NOT_MULTITHREAD_MODE,
                        "sqlite3 is not in multithreaded mode");
                goto out;
        }

        sql_conn = gf_sql_connection_init ();
        if (!sql_conn) {
                goto out;
        }

        /*Extract sql db path from args*/
        temp_str = NULL;
        GET_DB_PARAM_FROM_DICT(GFDB_STR_SQLITE3, args,
                        GFDB_SQL_PARAM_DBPATH, temp_str, out);
        strncpy(sql_conn->sqlite3_db_path, temp_str, PATH_MAX-1);
        sql_conn->sqlite3_db_path[PATH_MAX-1] = 0;

        is_dbfile_exist = (stat (sql_conn->sqlite3_db_path, &stbuf) == 0) ?
                                                _gf_true : _gf_false;

        /*Creates DB if not created*/
        sql_conn->sqlite3_db_conn = gf_open_sqlite3_conn (
                                        sql_conn->sqlite3_db_path,
                                        SQLITE_OPEN_READWRITE |
                                        SQLITE_OPEN_CREATE);
        if (!sql_conn->sqlite3_db_conn) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_CONNECTION_ERROR,
                        "Failed creating db connection");
                goto out;
        }

        /* If the file exist we skip the config part
         * and creation of the schema */
        if (is_dbfile_exist)
                goto db_exists;


        /*Apply sqlite3 params to database*/
        ret = apply_sql_params_db(sql_conn, args);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_SET_PARAM_FAILED, "Failed applying sql params"
                        " to %s", sql_conn->sqlite3_db_path);
                goto out;
        }

        /*Create the schema if NOT present*/
        ret = create_filetable (sql_conn->sqlite3_db_conn);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_CREATE_FAILED, "Failed Creating %s Table",
                        GF_FILE_TABLE);
               goto out;
        }

db_exists:
        ret = 0;
out:
        if (ret) {
                gf_sqlite3_fini ((void **)&sql_conn);
        }

        *db_conn = sql_conn;

        return ret;
}


int
gf_sqlite3_fini (void **db_conn)
{
        int ret = -1;
        gf_sql_connection_t *sql_conn = NULL;

        GF_ASSERT (db_conn);
        sql_conn = *db_conn;

        if (sql_conn) {
                if (sql_conn->sqlite3_db_conn) {
                        ret = gf_close_sqlite3_conn(sql_conn->sqlite3_db_conn);
                        if (ret) {
                                /*Logging of error done in
                                 * gf_close_sqlite3_conn()*/
                                goto out;
                        }
                        sql_conn->sqlite3_db_conn = NULL;
                }
                gf_sql_connection_fini (&sql_conn);
        }
        *db_conn = sql_conn;
        ret = 0;
out:
        return ret;
}

/******************************************************************************
 *
 *                      INSERT/UPDATE/DELETE Operations
 *
 *
 * ***************************************************************************/

int gf_sqlite3_insert(void *db_conn, gfdb_db_record_t *gfdb_db_record)
{
        int ret                         =       -1;
        gf_sql_connection_t *sql_conn   =       db_conn;

        CHECK_SQL_CONN(sql_conn, out);
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, gfdb_db_record, out);


        /*This is for debugging bug. Will be removed with a bug fix*/
        if ((GFDB_FOP_WIND == gfdb_db_record->gfdb_fop_path) &&
            (strncmp(gfdb_db_record->file_path, "<gfid", 5) == 0)) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_SKIP_PATH,
                        "Skip path <gfid fop=%d",
                        gfdb_db_record->gfdb_fop_type);
                goto out;
        }

        switch (gfdb_db_record->gfdb_fop_path) {
        case GFDB_FOP_WIND:
                ret = gf_sql_insert_wind(sql_conn, gfdb_db_record);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_INSERT_FAILED, "Failed wind insert");
                        goto out;
                }
                break;
        case GFDB_FOP_UNWIND:
                ret = gf_sql_insert_unwind (sql_conn, gfdb_db_record);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_INSERT_FAILED, "Failed unwind insert");
                        goto out;
                }
                break;

        case GFDB_FOP_WDEL:
                ret = gf_sql_update_delete_wind(sql_conn, gfdb_db_record);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_UPDATE_FAILED, "Failed updating delete "
                                "during wind");
                        goto out;
                }
                break;
        case GFDB_FOP_UNDEL:
        case GFDB_FOP_UNDEL_ALL:
                ret = gf_sql_delete_unwind(sql_conn, gfdb_db_record);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_DELETE_FAILED, "Failed deleting");
                        goto out;
                }
                break;
        case GFDB_FOP_INVALID:
        default:
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_INVALID_FOP,
                        "Cannot record to DB: Invalid FOP");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
gf_sqlite3_delete(void *db_conn, gfdb_db_record_t *gfdb_db_record)
{
        int ret = -1;
        gf_sql_connection_t *sql_conn = db_conn;

        CHECK_SQL_CONN(sql_conn, out);
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, gfdb_db_record, out);

        ret = 0;
out:
        return ret;
}

/******************************************************************************
 *
 *                      SELECT QUERY FUNCTIONS
 *
 *
 * ***************************************************************************/

/*
 * Find All files recorded in the DB
 * Input:
 *      query_callback  :       query callback fuction to handle
 *                              result records from the query
 * */
int
gf_sqlite3_find_all (void *db_conn, gf_query_callback_t query_callback,
                        void *query_cbk_args)
{
        int ret                                 =       -1;
        char *query_str                         =       NULL;
        gf_sql_connection_t *sql_conn           =       db_conn;
        sqlite3_stmt *prep_stmt                 =       NULL;


        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, query_callback, out);

        query_str = "select GF_FILE_TB.GF_ID,"
                   " (select group_concat( GF_PID || ',' || FNAME || ','"
                   " || FPATH || ',' || W_DEL_FLAG ||',' || LINK_UPDATE , '::')"
                   " from GF_FLINK_TB where "
                   "GF_FILE_TB.GF_ID = GF_FLINK_TB.GF_ID) from GF_FILE_TB ;";


        ret = sqlite3_prepare(sql_conn->sqlite3_db_conn, query_str, -1,
                                &prep_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing statment %s :"
                        "%s", query_str,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        ret = gf_sql_query_function(prep_stmt, query_callback, query_cbk_args);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_QUERY_FAILED,
                        "Failed Query %s", query_str);
                goto out;
        }

        ret = 0;
out:
        sqlite3_finalize(prep_stmt);
        return ret;
}


/*
 * Find recently changed files from the DB
 * Input:
 *      query_callback  :       query callback fuction to handle
 *                              result records from the query
 *      from_time       :       Time to define what is recent
 * */
int
gf_sqlite3_find_recently_changed_files(void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *query_cbk_args,
                                        gfdb_time_t *from_time)
{
        int ret                                 =       -1;
        char *query_str                         =       NULL;
        gf_sql_connection_t *sql_conn           =       db_conn;
        sqlite3_stmt *prep_stmt                 =       NULL;
        uint64_t  from_time_usec                =       0;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, query_callback, out);

        query_str = "select GF_FILE_TB.GF_ID,"
                " (select group_concat( GF_PID || ',' || FNAME || ','"
                " || FPATH || ',' || W_DEL_FLAG ||',' || LINK_UPDATE , '::')"
                " from GF_FLINK_TB where GF_FILE_TB.GF_ID = GF_FLINK_TB.GF_ID)"
                "  from GF_FILE_TB where "
                /*First condition: For writes*/
                "((" GF_COL_TB_WSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_WMSEC ") >= ? )"
                " OR "
                /*Second condition: For reads*/
                "((" GF_COL_TB_RWSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_RWMSEC ") >= ?)";

        from_time_usec = gfdb_time_2_usec(from_time);

        ret = sqlite3_prepare(sql_conn->sqlite3_db_conn, query_str, -1,
                                &prep_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing statment %s :"
                        " %s", query_str,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write wind time*/
        ret = sqlite3_bind_int64(prep_stmt, 1, from_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding from_time_usec "
                        "%ld : %s", from_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind read wind time*/
        ret = sqlite3_bind_int64(prep_stmt, 2, from_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding from_time_usec "
                        "%ld : %s ", from_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the query*/
        ret = gf_sql_query_function(prep_stmt, query_callback, query_cbk_args);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_QUERY_FAILED,
                        "Failed Query %s", query_str);
                goto out;
        }

        /*Clear freq counters of un-selected data*/
        ret = gf_sql_clear_counters(sql_conn);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_CLEAR_COUNTER_FAILED, "Failed clearing"
                        " counters!");
                goto out;
        }

        ret = 0;
out:
        sqlite3_finalize(prep_stmt);
        return ret;
}


/*
 * Find unchanged files from a specified time from the DB
 * Input:
 *      query_callback  :       query callback fuction to handle
 *                              result records from the query
 *      for_time        :        Time from where the file/s are not changed
 * */
int
gf_sqlite3_find_unchanged_for_time (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *query_cbk_args,
                                        gfdb_time_t *for_time)
{
        int ret                                 =       -1;
        char *query_str                         =       NULL;
        gf_sql_connection_t *sql_conn           =       db_conn;
        sqlite3_stmt *prep_stmt                 =       NULL;
        uint64_t  for_time_usec                 =       0;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, query_callback, out);

        query_str = "select GF_FILE_TB.GF_ID,"
                " (select group_concat( GF_PID || ',' || FNAME || ','"
                " || FPATH || ',' || W_DEL_FLAG ||',' || LINK_UPDATE , '::')"
                " from GF_FLINK_TB where GF_FILE_TB.GF_ID = GF_FLINK_TB.GF_ID)"
                "  from GF_FILE_TB where "
                /*First condition: For writes*/
                "((" GF_COL_TB_WSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_WMSEC ") <= ? )"
                " AND "
                /*Second condition: For reads*/
                "((" GF_COL_TB_RWSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_RWMSEC ") <= ?)";

        for_time_usec = gfdb_time_2_usec(for_time);

        ret = sqlite3_prepare(sql_conn->sqlite3_db_conn, query_str, -1,
                                &prep_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing statment %s :"
                        " %s", query_str,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write wind time*/
        ret = sqlite3_bind_int64(prep_stmt, 1, for_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding for_time_usec "
                        "%ld : %s", for_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind read wind time*/
        ret = sqlite3_bind_int64(prep_stmt, 2, for_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding for_time_usec "
                        "%ld : %s", for_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the query*/
        ret = gf_sql_query_function(prep_stmt, query_callback, query_cbk_args);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_QUERY_FAILED,
                        "Failed Query %s", query_str);
                goto out;
        }

        /*Clear freq counters of un-selected data*/
        ret = gf_sql_clear_counters(sql_conn);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_CLEAR_COUNTER_FAILED, "Failed clearing"
                        " counters!");
                goto out;
        }

        ret = 0;
out:
        sqlite3_finalize(prep_stmt);
        return ret;
}





/*
 * Find recently changed files with a specific frequency from the DB
 * Input:
 *      db_conn         :       db connection object
 *      query_callback  :       query callback fuction to handle
 *                              result records from the query
 *      from_time       :       Time to define what is recent
 *      freq_write_cnt  :       Frequency thresold for write
 *      freq_read_cnt   :       Frequency thresold for read
 *      clear_counters  :       Clear counters (r/w) for all inodes in DB
 * */
int
gf_sqlite3_find_recently_changed_files_freq (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *query_cbk_args,
                                        gfdb_time_t *from_time,
                                        int freq_write_cnt,
                                        int freq_read_cnt,
                                        gf_boolean_t clear_counters)
{
        int ret                                 =       -1;
        char *query_str                         =       NULL;
        gf_sql_connection_t *sql_conn           =       db_conn;
        sqlite3_stmt *prep_stmt                 =       NULL;
        uint64_t  from_time_usec                =       0;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO(GFDB_STR_SQLITE3, query_callback, out);

        query_str = "select GF_FILE_TB.GF_ID,"
                " (select group_concat( GF_PID || ',' || FNAME || ','"
                " || FPATH || ',' || W_DEL_FLAG ||',' || LINK_UPDATE , '::')"
                " from GF_FLINK_TB where GF_FILE_TB.GF_ID = GF_FLINK_TB.GF_ID)"
                "  from GF_FILE_TB where "
                /*First condition: For Writes*/
                "( ((" GF_COL_TB_WSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_WMSEC ") >= ? )"
                " AND "" (" GF_COL_TB_WFC " >= ? ) )"
                " OR "
                /*Second condition: For Reads */
                "( ((" GF_COL_TB_RWSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_RWMSEC ") >= ?)"
                " AND "" (" GF_COL_TB_RFC " >= ? ) )";

        from_time_usec = gfdb_time_2_usec(from_time);

        ret = sqlite3_prepare(sql_conn->sqlite3_db_conn, query_str, -1,
                                &prep_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing statment %s :"
                        " %s", query_str,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write wind time*/
        ret = sqlite3_bind_int64(prep_stmt, 1, from_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding from_time_usec "
                        "%ld : %s", from_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write frequency thresold*/
        ret = sqlite3_bind_int(prep_stmt, 2, freq_write_cnt);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding freq_write_cnt "
                        "%d : %s", freq_write_cnt,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Bind read wind time*/
        ret = sqlite3_bind_int64(prep_stmt, 3, from_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding from_time_usec "
                        "%ld : %s", from_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind read frequency thresold*/
        ret = sqlite3_bind_int(prep_stmt, 4, freq_read_cnt);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding freq_read_cnt "
                        "%d : %s", freq_read_cnt,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the query*/
        ret = gf_sql_query_function(prep_stmt, query_callback, query_cbk_args);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_QUERY_FAILED,
                        "Failed Query %s", query_str);
                goto out;
        }



        /*Clear counters*/
        if (clear_counters) {
                ret = gf_sql_clear_counters(sql_conn);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_CLEAR_COUNTER_FAILED, "Failed clearing"
                                " counters!");
                        goto out;
                }
        }
        ret = 0;
out:
        sqlite3_finalize (prep_stmt);
        return ret;
}




/*
 * Find unchanged files from a specified time, w.r.t to frequency, from the DB
 * Input:
 *      query_callback  :       query callback fuction to handle
 *                              result records from the query
 *      for_time       :        Time from where the file/s are not changed
 *      freq_write_cnt  :       Frequency thresold for write
 *      freq_read_cnt   :       Frequency thresold for read
 *      clear_counters  :       Clear counters (r/w) for all inodes in DB
 * */
int
gf_sqlite3_find_unchanged_for_time_freq (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *query_cbk_args,
                                        gfdb_time_t *for_time,
                                        int freq_write_cnt,
                                        int freq_read_cnt,
                                        gf_boolean_t clear_counters)
{
        int ret                                 =       -1;
        char *query_str                         =       NULL;
        gf_sql_connection_t *sql_conn           =       db_conn;
        sqlite3_stmt *prep_stmt                 =       NULL;
        uint64_t  for_time_usec                 =       0;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, query_callback, out);

        query_str = "select GF_FILE_TB.GF_ID,"
                " (select group_concat( GF_PID || ',' || FNAME || ','"
                " || FPATH || ',' || W_DEL_FLAG ||',' || LINK_UPDATE , '::')"
                " from GF_FLINK_TB where GF_FILE_TB.GF_ID = GF_FLINK_TB.GF_ID)"
                "  from GF_FILE_TB where "
                /*First condition: For Writes
                 * Files that have write wind time smaller than for_time
                 * OR
                 * File that have write wind time greater than for_time,
                 * but write_frequency less than freq_write_cnt*/
                "( ((" GF_COL_TB_WSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_WMSEC ") < ? )"
                " OR "
                "( (" GF_COL_TB_WFC " < ? ) AND"
                "((" GF_COL_TB_WSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_WMSEC ") >= ? ) ) )"
                " AND "
                /*Second condition: For Reads
                 * Files that have read wind time smaller than for_time
                 * OR
                 * File that have read wind time greater than for_time,
                 * but write_frequency less than freq_write_cnt*/
                "( ((" GF_COL_TB_RWSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_RWMSEC ") < ? )"
                " OR "
                "( (" GF_COL_TB_RFC " < ? ) AND"
                "((" GF_COL_TB_RWSEC " * " TOSTRING(GFDB_MICROSEC) " + "
                GF_COL_TB_RWMSEC ") >= ? ) ) )";


        for_time_usec = gfdb_time_2_usec(for_time);

        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, query_str, -1,
                                &prep_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing delete "
                        "statment %s : %s", query_str,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write wind time*/
        ret = sqlite3_bind_int64 (prep_stmt, 1, for_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding for_time_usec "
                        "%ld : %s", for_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write frequency thresold*/
        ret = sqlite3_bind_int (prep_stmt, 2, freq_write_cnt);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding freq_write_cnt"
                        " %d : %s", freq_write_cnt,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind write wind time*/
        ret = sqlite3_bind_int64 (prep_stmt, 3, for_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding for_time_usec "
                        "%ld : %s", for_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }



        /*Bind read wind time*/
        ret = sqlite3_bind_int64 (prep_stmt, 4, for_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding for_time_usec "
                        "%ld : %s", for_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind read frequency thresold*/
        ret = sqlite3_bind_int (prep_stmt, 5, freq_read_cnt);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding freq_read_cnt "
                        "%d : %s", freq_read_cnt,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind read wind time*/
        ret = sqlite3_bind_int64 (prep_stmt, 6, for_time_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding for_time_usec "
                        "%ld : %s", for_time_usec,
                        sqlite3_errmsg(sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the query*/
        ret = gf_sql_query_function (prep_stmt, query_callback, query_cbk_args);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_QUERY_FAILED,
                        "Failed Query %s", query_str);
                goto out;
        }


        /*Clear counters*/
        if (clear_counters) {
                ret = gf_sql_clear_counters (sql_conn);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_CLEAR_COUNTER_FAILED, "Failed clearing "
                                "counters!");
                        goto out;
                }
        }

        ret = 0;
out:
        sqlite3_finalize(prep_stmt);
        return ret;
}
