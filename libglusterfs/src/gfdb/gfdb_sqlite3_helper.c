/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "gfdb_sqlite3_helper.h"

/*****************************************************************************
 *
 *                 Helper function to execute actual sql queries
 *
 *
 * ****************************************************************************/

static inline int
gf_sql_delete_all (gf_sql_connection_t *sql_conn,
                  char              *gfid)
{
        int ret = -1;
        sqlite3_stmt *delete_file_stmt = NULL;
        sqlite3_stmt *delete_link_stmt = NULL;
        char *delete_link_str = "DELETE FROM "
                           GF_FILE_LINK_TABLE
                           " WHERE GF_ID = ? ;";
        char *delete_file_str = "DELETE FROM "
                           GF_FILE_TABLE
                           " WHERE GF_ID = ? ;";

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);

        /*
         * Delete all links associated with this GFID
         *
         * */
        /*Prepare statement for delete all links*/
        ret = sqlite3_prepare(sql_conn->sqlite3_db_conn, delete_link_str, -1,
                                &delete_link_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing delete statment %s : %s",
                        delete_link_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (delete_link_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Execute the prepare statement*/
        if (sqlite3_step (delete_link_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        delete_link_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*
         * Delete entry from file table associated with this GFID
         *
         * */
        /*Prepare statement for delete all links*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, delete_file_str, -1,
                                &delete_file_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing delete statment %s : %s",
                        delete_file_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (delete_file_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (delete_file_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        delete_file_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

out:
        /*Free prepared statement*/
        sqlite3_finalize (delete_file_stmt);
        sqlite3_finalize (delete_link_stmt);
        return ret;
}

static inline int
gf_sql_delete_link (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename)
{
        int ret = -1;
        sqlite3_stmt *delete_stmt = NULL;
        char *delete_str = "DELETE FROM "
                           GF_FILE_LINK_TABLE
                           " WHERE GF_ID = ? AND GF_PID = ?"
                           " AND FNAME = ?;";

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basename, out);

        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, delete_str, -1,
                                &delete_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing delete statment %s : %s", delete_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (delete_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind pargfid*/
        ret = sqlite3_bind_text (delete_stmt, 2, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent gfid %s : %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basename*/
        ret = sqlite3_bind_text (delete_stmt, 3, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding basename %s : %s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step(delete_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        delete_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        ret = 0;
out:
        /*Free prepared statement*/
        sqlite3_finalize (delete_stmt);
        return ret;
}



static inline int
gf_sql_update_link_flags (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   int                  update_flag,
                   gf_boolean_t         is_update_or_delete)
{
        int ret = -1;
        sqlite3_stmt *update_stmt = NULL;
        char *update_column = NULL;
        char update_str[1024] = "";


        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basename, out);

        update_column = (is_update_or_delete) ? "LINK_UPDATE" : "W_DEL_FLAG";

        sprintf (update_str, "UPDATE "
                            GF_FILE_LINK_TABLE
                            " SET %s = ?"
                            " WHERE GF_ID = ? AND GF_PID = ? AND FNAME = ?;",
                            update_column);

        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, update_str, -1,
                                &update_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing update statment %s : %s", update_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Bind link_update*/
        ret = sqlite3_bind_int (update_stmt, 1, update_flag);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding update_flag %d : %s", update_flag,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (update_stmt, 2, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind pargfid*/
        ret = sqlite3_bind_text (update_stmt, 3, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent gfid %s : %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basename*/
        ret = sqlite3_bind_text (update_stmt, 4, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding basename %s : %s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Execute the prepare statement*/
        if (sqlite3_step(update_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        update_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        /*Free prepared statement*/
        sqlite3_finalize (update_stmt);
        return ret;
}


static inline int
gf_sql_insert_link (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   char                 *basepath)
{
        int ret = -1;
        sqlite3_stmt *insert_stmt = NULL;
        char *insert_str = "INSERT INTO "
                           GF_FILE_LINK_TABLE
                           " (GF_ID, GF_PID, FNAME, FPATH,"
                           " W_DEL_FLAG, LINK_UPDATE) "
                           " VALUES (?, ?, ?, ?, 0, 1);";

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basename, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basepath, out);

        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, insert_str, -1,
                                &insert_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing insert statment %s : %s", insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (insert_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind pargfid*/
        ret = sqlite3_bind_text (insert_stmt, 2, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent gfid %s : %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basename*/
        ret = sqlite3_bind_text (insert_stmt, 3, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding basename %s : %s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basepath*/
        ret = sqlite3_bind_text (insert_stmt, 4, basepath, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding basepath %s : %s", basepath,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (insert_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        /*Free prepared statement*/
        sqlite3_finalize (insert_stmt);
        return ret;
}


static inline int
gf_sql_update_link (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   char                 *basepath,
                   char                 *old_pargfid,
                   char                 *old_basename)
{
        int ret = -1;
        sqlite3_stmt *insert_stmt = NULL;
        char *insert_str =  "INSERT INTO "
                            GF_FILE_LINK_TABLE
                            " (GF_ID, GF_PID, FNAME, FPATH,"
                            " W_DEL_FLAG, LINK_UPDATE) "
                            " VALUES (? , ?, ?, ?, 0, 1);";

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basename, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basepath, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, old_pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, old_basename, out);

        /*
         *
         * Delete the old link
         *
         * */
         ret = gf_sql_delete_link (sql_conn, gfid, old_pargfid,
                                old_basename);
        if (ret) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed deleting old link");
                goto out;
        }

        /*
         *
         * insert new link
         *
         * */
        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, insert_str, -1,
                                &insert_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing insert statment %s : %s", insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (insert_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind new pargfid*/
        ret = sqlite3_bind_text (insert_stmt, 2, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent gfid %s : %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind new basename*/
        ret = sqlite3_bind_text (insert_stmt, 3, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding basename %s : %s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind new basepath*/
        ret = sqlite3_bind_text (insert_stmt, 4, basepath, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding basename %s : %s", basepath,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (insert_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }



        ret = 0;
out:
        /*Free prepared statement*/
        sqlite3_finalize (insert_stmt);
        return ret;
}

static inline int
gf_sql_insert_write_wind_time (gf_sql_connection_t  *sql_conn,
                                char                 *gfid,
                                gfdb_time_t          *wind_time)
{
        int ret = -1;
        sqlite3_stmt *insert_stmt = NULL;
        char *insert_str = "INSERT INTO "
                           GF_FILE_TABLE
                           "(GF_ID, W_SEC, W_MSEC, UW_SEC, UW_MSEC)"
                           " VALUES (?, ?, ?, 0, 0);";

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, wind_time, out);


        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, insert_str, -1,
                        &insert_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing insert statment %s : %s", insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (insert_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind wind secs*/
        ret = sqlite3_bind_int (insert_stmt, 2, wind_time->tv_sec);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent wind secs %ld : %s",
                        wind_time->tv_sec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind wind msecs*/
        ret = sqlite3_bind_int (insert_stmt, 3, wind_time->tv_usec);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent wind msecs %ld : %s",
                        wind_time->tv_usec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (insert_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        /*Free prepared statement*/
        sqlite3_finalize (insert_stmt);
        return ret;
}



/*Update write/read times for both wind and unwind*/
static inline int
gf_update_time (gf_sql_connection_t    *sql_conn,
                char                    *gfid,
                gfdb_time_t             *update_time,
                gf_boolean_t            record_counter,
                gf_boolean_t            is_wind,
                gf_boolean_t            is_read)
{
        int ret = -1;
        sqlite3_stmt *update_stmt = NULL;
        char update_str[1024] = "";
        char *freq_cntr_str = NULL;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, update_time, out);

        /*
         * Constructing the prepare statment string.
         *
         * */
        /*For write time*/
        if (!is_read) {
                if (is_wind) {
                        /*if record counter is on*/
                        freq_cntr_str = (record_counter) ?
                        ", WRITE_FREQ_CNTR = WRITE_FREQ_CNTR + 1" : "";

                        /*Prefectly safe as we will not go array of bound*/
                        sprintf (update_str, "UPDATE "
                                GF_FILE_TABLE
                                " SET W_SEC = ?, W_MSEC = ? "
                                " %s"/*place for read freq counters*/
                                " WHERE GF_ID = ? ;", freq_cntr_str);
                } else {
                        /*Prefectly safe as we will not go array of bound*/
                        sprintf (update_str, "UPDATE "
                                GF_FILE_TABLE
                                " SET UW_SEC = ?, UW_MSEC = ? ;");
                }
        }
        /*For Read Time update*/
        else {
                if (is_wind) {
                        /*if record counter is on*/
                        freq_cntr_str = (record_counter) ?
                        ", READ_FREQ_CNTR = READ_FREQ_CNTR + 1" : "";

                        /*Prefectly safe as we will not go array of bound*/
                        sprintf (update_str, "UPDATE "
                                GF_FILE_TABLE
                                " SET W_READ_SEC = ?, W_READ_MSEC = ? "
                                " %s"/*place for read freq counters*/
                                " WHERE GF_ID = ? ;", freq_cntr_str);
                } else {
                        /*Prefectly safe as we will not go array of bound*/
                        sprintf (update_str, "UPDATE "
                                GF_FILE_TABLE
                                " SET UW_READ_SEC = ?, UW_READ_MSEC = ? ;");
                }
        }

        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, update_str, -1,
                                &update_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed preparing insert statment %s : %s", update_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind time secs*/
        ret = sqlite3_bind_int (update_stmt, 1, update_time->tv_sec);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent wind secs %ld : %s",
                        update_time->tv_sec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind time msecs*/
        ret = sqlite3_bind_int (update_stmt, 2, update_time->tv_usec);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding parent wind msecs %ld : %s",
                        update_time->tv_usec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (update_stmt, 3, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (update_stmt) != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing the prepared stmt %s : %s",
                        update_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        /*Free prepared statement*/
        sqlite3_finalize (update_stmt);
        return ret;
}

/******************************************************************************
 *
 *                      Helper functions for gf_sqlite3_insert()
 *
 *
 * ****************************************************************************/

int
gf_sql_insert_wind (gf_sql_connection_t  *sql_conn,
                   gfdb_db_record_t     *gfdb_db_record)
{
        int ret                 = -1;
        gfdb_time_t *modtime    = NULL;
        char *pargfid_str       = NULL;
        char *gfid_str          = NULL;
        char *old_pargfid_str   = NULL;
        gf_boolean_t its_wind   = _gf_true;/*remains true for this function*/



        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfdb_db_record, out);


        gfid_str = gf_strdup (uuid_utoa (gfdb_db_record->gfid));
        if (!gfid_str) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Creating gfid string failed.");
                goto out;
        }

        modtime = &gfdb_db_record->gfdb_wind_change_time;

        /* handle all dentry based operations */
        if (isdentryfop (gfdb_db_record->gfdb_fop_type)) {
                /*Parent GFID is always set*/
                pargfid_str = gf_strdup (uuid_utoa (gfdb_db_record->pargfid));
                if (!pargfid_str) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Creating gfid string failed.");
                        goto out;
                }

                /* handle create, mknod */
                if (isdentrycreatefop (gfdb_db_record->gfdb_fop_type)) {
                        /*insert link*/
                        ret = gf_sql_insert_link(sql_conn,
                                        gfid_str, pargfid_str,
                                        gfdb_db_record->file_name,
                                        gfdb_db_record->file_path);
                        if (ret) {
                                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed inserting link in DB");
                                goto out;
                        }
                        gfdb_db_record->islinkupdate = _gf_true;

                        /*
                         * Only for create/mknod insert wind time
                         * for the first time
                         * */
                        ret = gf_sql_insert_write_wind_time (sql_conn, gfid_str,
                                                                modtime);
                        if (ret) {
                                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed inserting wind time in DB");
                                goto out;
                        }
                        goto out;
                }
                /*handle rename, link */
                else {
                        /*rename*/
                        if (strlen (gfdb_db_record->old_file_name) != 0) {
                                old_pargfid_str = gf_strdup (uuid_utoa (
                                                gfdb_db_record->old_pargfid));
                                if (!old_pargfid_str) {
                                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                                "Creating gfid string failed.");
                                        goto out;
                                }
                                gf_sql_update_link (sql_conn, gfid_str,
                                                pargfid_str,
                                                gfdb_db_record->file_name,
                                                gfdb_db_record->file_path,
                                                old_pargfid_str,
                                                gfdb_db_record->old_file_name);
                                if (ret) {
                                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                                "Failed updating link");
                                        goto out;
                                }
                                gfdb_db_record->islinkupdate = _gf_true;
                        }
                        /*link*/
                        else {
                                ret = gf_sql_insert_link (sql_conn,
                                        gfid_str, pargfid_str,
                                        gfdb_db_record->file_name,
                                        gfdb_db_record->file_path);
                                if (ret) {
                                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                        "Failed inserting link in DB");
                                        goto out;
                                }
                                gfdb_db_record->islinkupdate = _gf_true;
                        }
                }
        }

        /* update times only when said!*/
        if (gfdb_db_record->do_record_times) {
                /*All fops update times read or write*/
                ret = gf_update_time (sql_conn, gfid_str, modtime,
                        gfdb_db_record->do_record_counters,
                        its_wind,
                        isreadfop (gfdb_db_record->gfdb_fop_type));
                if (ret) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed update wind time in DB");
                        goto out;
                }
        }

        ret = 0;
out:
        GF_FREE (gfid_str);
        GF_FREE (pargfid_str);
        GF_FREE (old_pargfid_str);
        return ret;
}




int
gf_sql_insert_unwind (gf_sql_connection_t  *sql_conn,
                     gfdb_db_record_t     *gfdb_db_record)
{

        int ret = -1;
        gfdb_time_t *modtime    = NULL;
        gf_boolean_t its_wind   = _gf_true;/*remains true for this function*/
        char *gfid_str = NULL;
        char *pargfid_str = NULL;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfdb_db_record, out);

        gfid_str = gf_strdup (uuid_utoa(gfdb_db_record->gfid));
        if (!gfid_str) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Creating gfid string failed.");
                goto out;
        }

        /*Only update if recording unwind is set*/
        if (gfdb_db_record->do_record_times &&
                gfdb_db_record->do_record_uwind_time) {
                modtime = &gfdb_db_record->gfdb_unwind_change_time;
                ret = gf_update_time (sql_conn, gfid_str, modtime,
                        gfdb_db_record->do_record_counters,
                        (!its_wind),
                        isreadfop (gfdb_db_record->gfdb_fop_type));
                if (ret) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed update unwind time in DB");
                        goto out;
                }
        }

        /*For link creation and changes we use link updated*/
        if (gfdb_db_record->islinkupdate &&
                isdentryfop(gfdb_db_record->gfdb_fop_type)) {

                pargfid_str = gf_strdup(uuid_utoa(gfdb_db_record->pargfid));
                if (!pargfid_str) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Creating pargfid_str string failed.");
                        goto out;
                }

                ret = gf_sql_update_link_flags (sql_conn, gfid_str, pargfid_str,
                                        gfdb_db_record->file_name, 0, _gf_true);
                if (ret) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed updating link flags in unwind");
                        goto out;
                }
        }

        ret = 0;
out:
        GF_FREE (gfid_str);
        GF_FREE (pargfid_str);
        return ret;
}


int
gf_sql_update_delete_wind (gf_sql_connection_t  *sql_conn,
                          gfdb_db_record_t     *gfdb_db_record)
{
        int ret = -1;
        gfdb_time_t *modtime    = NULL;
        char *gfid_str          = NULL;
        char *pargfid_str       = NULL;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfdb_db_record, out);

        gfid_str = gf_strdup (uuid_utoa(gfdb_db_record->gfid));
        if (!gfid_str) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Creating gfid string failed.");
                goto out;
        }

        pargfid_str = gf_strdup (uuid_utoa(gfdb_db_record->pargfid));
        if (!pargfid_str) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Creating pargfid_str string failed.");
                        goto out;
        }

        if (gfdb_db_record->do_record_times) {
                /*Update the wind write times*/
                modtime = &gfdb_db_record->gfdb_wind_change_time;
                ret = gf_update_time (sql_conn, gfid_str, modtime,
                        gfdb_db_record->do_record_counters,
                        _gf_true,
                        isreadfop (gfdb_db_record->gfdb_fop_type));
                if (ret) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed update wind time in DB");
                        goto out;
                }
        }

        ret = gf_sql_update_link_flags (sql_conn, gfid_str, pargfid_str,
                                        gfdb_db_record->file_name, 1,
                                        _gf_false);
        if (ret) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed updating link flags in wind");
                goto out;
        }

        ret = 0;
out:
        GF_FREE (gfid_str);
        GF_FREE (pargfid_str);
        return ret;
}

int
gf_sql_delete_unwind (gf_sql_connection_t  *sql_conn,
                          gfdb_db_record_t     *gfdb_db_record)
{
        int ret = -1;
        char *gfid_str = NULL;
        char *pargfid_str = NULL;
        gfdb_time_t *modtime    = NULL;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfdb_db_record, out);

        gfid_str = gf_strdup (uuid_utoa(gfdb_db_record->gfid));
        if (!gfid_str) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Creating gfid string failed.");
                goto out;
        }

        /*Nuke all the entries for this GFID from DB*/
        if (gfdb_db_record->gfdb_fop_path == GFDB_FOP_UNDEL_ALL) {
                gf_sql_delete_all(sql_conn, gfid_str);
        }
        /*Remove link entries only*/
        else if (gfdb_db_record->gfdb_fop_path == GFDB_FOP_UNDEL) {

                pargfid_str = gf_strdup(uuid_utoa(gfdb_db_record->pargfid));
                if (!pargfid_str) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Creating pargfid_str string failed.");
                        goto out;
                }

                modtime = &gfdb_db_record->gfdb_unwind_change_time;

                ret = gf_sql_delete_link(sql_conn, gfid_str, pargfid_str,
                                        gfdb_db_record->file_name);
                if (ret) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed deleting link");
                        goto out;
                }

                if (gfdb_db_record->do_record_times &&
                        gfdb_db_record->do_record_uwind_time) {
                        ret = gf_update_time (sql_conn, gfid_str, modtime,
                                gfdb_db_record->do_record_counters,
                                _gf_false,
                                isreadfop(gfdb_db_record->gfdb_fop_type));
                        if (ret) {
                                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                "Failed update unwind time in DB");
                                goto out;
                        }
                }
        }
        ret = 0;
out:
        GF_FREE (gfid_str);
        GF_FREE (pargfid_str);
        return ret;
}

/******************************************************************************
 *
 *                      Find/Query helper functions
 *
 * ****************************************************************************/
int
gf_sql_query_function (sqlite3_stmt              *prep_stmt,
                      gf_query_callback_t       query_callback,
                      void                      *_query_cbk_args)
{
        int ret = -1;
        gfdb_query_record_t *gfdb_query_record = NULL;
        char *text_column = NULL;
        sqlite3 *db_conn = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, prep_stmt, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, query_callback, out);

        db_conn = sqlite3_db_handle(prep_stmt);

        gfdb_query_record = gfdb_query_record_init ();
        if (!gfdb_query_record) {
                        gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                        "Failed to create gfdb_query_record");
                                goto out;
        }

        /*Loop to access queried rows*/
        while ((ret = sqlite3_step (prep_stmt)) == SQLITE_ROW) {

                /*Clear the query record*/
                memset (gfdb_query_record, 0, sizeof(*gfdb_query_record));

                if (sqlite3_column_count(prep_stmt) > 0) {

                        /*Retriving GFID - column index is 0*/
                        text_column = (char *)sqlite3_column_text
                                                        (prep_stmt, 0);
                        if (!text_column) {
                                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                        "Failed retriving GF_ID");
                                goto out;
                        }
                        ret = gf_uuid_parse (text_column, gfdb_query_record->gfid);
                        if (ret) {
                                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                        "Failed parsing GF_ID");
                                goto out;
                        }

                        /*Retrive Link Buffer - column index 1*/
                        text_column = (char *)sqlite3_column_text
                                                (prep_stmt, 1);
                        /* Get link string. Do shallow copy here
                         * query_callback function should do a
                         * deep copy and then do operations on this field*/
                        gfdb_query_record->_link_info_str = text_column;
                        gfdb_query_record->link_info_size = strlen
                                                                (text_column);

                        /* Call the call back function provided*/
                        ret = query_callback (gfdb_query_record,
                                                        _query_cbk_args);
                        if (ret) {
                                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                        "Query Call back failed!");
                                goto out;
                        }

                }

        }

        if (ret != SQLITE_DONE) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed retriving records from db : %s",
                        sqlite3_errmsg (db_conn));
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        gfdb_query_record_fini (&gfdb_query_record);
        return ret;
}



int
gf_sql_clear_counters (gf_sql_connection_t *sql_conn)
{
        int ret                 = -1;
        char *sql_strerror      = NULL;
        char *query_str         = NULL;

        CHECK_SQL_CONN (sql_conn, out);

        query_str = "BEGIN;UPDATE "
                    GF_FILE_TABLE
                    " SET " GF_COL_READ_FREQ_CNTR " = 0 , "
                    GF_COL_WRITE_FREQ_CNTR " = 0 ;COMMIT;";

        ret = sqlite3_exec (sql_conn->sqlite3_db_conn, query_str, NULL, NULL,
                                &sql_strerror);
        if (ret != SQLITE_OK) {
                gf_log (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        "Failed executing: %s : %s",
                        query_str, sql_strerror);
                sqlite3_free (sql_strerror);
                        ret = -1;
                        goto out;
        }

        ret = 0;
out:
        return ret;
}
