/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "gfdb_sqlite3_helper.h"


#define GFDB_SQL_STMT_SIZE 256

/*****************************************************************************
 *
 *                 Helper function to execute actual sql queries
 *
 *
 * ****************************************************************************/

static int
gf_sql_delete_all (gf_sql_connection_t  *sql_conn,
                  char                  *gfid,
                  gf_boolean_t          ignore_errors)
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing delete "
                        "statement %s : %s", delete_link_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (delete_link_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Execute the prepare statement*/
        if (sqlite3_step (delete_link_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing delete "
                        "statement %s : %s", delete_file_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (delete_file_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (delete_file_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
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

static int
gf_sql_delete_link (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   gf_boolean_t         ignore_errors)
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing delete "
                        "statement %s : %s", delete_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (delete_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED,
                        "Failed binding gfid %s : %s", gfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind pargfid*/
        ret = sqlite3_bind_text (delete_stmt, 2, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent gfid %s "
                        ": %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basename*/
        ret = sqlite3_bind_text (delete_stmt, 3, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding basename %s : "
                        "%s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step(delete_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
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



static int
gf_sql_update_link_flags (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   int                  update_flag,
                   gf_boolean_t         is_update_or_delete,
                   gf_boolean_t         ignore_errors)
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing update "
                        "statement %s : %s", update_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Bind link_update*/
        ret = sqlite3_bind_int (update_stmt, 1, update_flag);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding update_flag %d "
                        ": %s", update_flag,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (update_stmt, 2, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind pargfid*/
        ret = sqlite3_bind_text (update_stmt, 3, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent gfid %s "
                        ": %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basename*/
        ret = sqlite3_bind_text (update_stmt, 4, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding basename %s : "
                        "%s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }


        /*Execute the prepare statement*/
        if (sqlite3_step(update_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
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


static int
gf_sql_insert_link (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   gf_boolean_t         link_consistency,
                   gf_boolean_t         ignore_errors)
{
        int ret = -1;
        sqlite3_stmt *insert_stmt = NULL;
        char insert_str[GFDB_SQL_STMT_SIZE] = "";

        sprintf (insert_str, "INSERT INTO "
                           GF_FILE_LINK_TABLE
                           " (GF_ID, GF_PID, FNAME,"
                           " W_DEL_FLAG, LINK_UPDATE) "
                           " VALUES (?, ?, ?, 0, %d);",
                           link_consistency);

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basename, out);

        /*Prepare statement*/
        ret = sqlite3_prepare (sql_conn->sqlite3_db_conn, insert_str, -1,
                                &insert_stmt, 0);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED,
                        "Failed preparing insert "
                        "statement %s : %s", insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (insert_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED,
                        "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind pargfid*/
        ret = sqlite3_bind_text (insert_stmt, 2, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        0, LG_MSG_BINDING_FAILED,
                        "Failed binding parent gfid %s "
                        ": %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind basename*/
        ret = sqlite3_bind_text (insert_stmt, 3, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        0, LG_MSG_BINDING_FAILED,
                        "Failed binding basename %s : %s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (insert_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors),
                        0, LG_MSG_EXEC_FAILED,
                        "Failed executing the prepared "
                        "stmt %s %s %s %s : %s",
                        gfid, pargfid, basename, insert_str,
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


static int
gf_sql_update_link (gf_sql_connection_t  *sql_conn,
                   char                 *gfid,
                   char                 *pargfid,
                   char                 *basename,
                   char                 *old_pargfid,
                   char                 *old_basename,
                   gf_boolean_t         link_consistency,
                   gf_boolean_t         ignore_errors)
{
        int ret = -1;
        sqlite3_stmt *insert_stmt = NULL;
        char insert_str[GFDB_SQL_STMT_SIZE] = "";

        sprintf (insert_str, "INSERT INTO "
                            GF_FILE_LINK_TABLE
                            " (GF_ID, GF_PID, FNAME,"
                            " W_DEL_FLAG, LINK_UPDATE) "
                            " VALUES (? , ?, ?, 0, %d);",
                            link_consistency);

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, basename, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, old_pargfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, old_basename, out);

        /*
         *
         * Delete the old link
         *
         * */
         ret = gf_sql_delete_link (sql_conn, gfid, old_pargfid,
                                old_basename, ignore_errors);
        if (ret) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_DELETE_FAILED, "Failed deleting old link");
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing insert "
                        "statement %s : %s", insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (insert_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind new pargfid*/
        ret = sqlite3_bind_text (insert_stmt, 2, pargfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent gfid %s "
                        ": %s", pargfid,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind new basename*/
        ret = sqlite3_bind_text (insert_stmt, 3, basename, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding basename %s : "
                        "%s", basename,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (insert_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
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

static int
gf_sql_insert_write_wind_time (gf_sql_connection_t      *sql_conn,
                                char                    *gfid,
                                gfdb_time_t             *wind_time,
                                gf_boolean_t            ignore_errors)
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing insert "
                        "statement %s : %s", insert_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (insert_stmt, 1, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind wind secs*/
        ret = sqlite3_bind_int (insert_stmt, 2, wind_time->tv_sec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent wind "
                        "secs %ld : %s", wind_time->tv_sec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind wind msecs*/
        ret = sqlite3_bind_int (insert_stmt, 3, wind_time->tv_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent wind "
                        "msecs %ld : %s", wind_time->tv_usec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (insert_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
                        "Failed executing the prepared stmt GFID:%s %s : %s",
                        gfid, insert_str,
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
static int
gf_update_time (gf_sql_connection_t    *sql_conn,
                char                    *gfid,
                gfdb_time_t             *update_time,
                gf_boolean_t            record_counter,
                gf_boolean_t            is_wind,
                gf_boolean_t            is_read,
                gf_boolean_t            ignore_errors)
{
        int ret = -1;
        sqlite3_stmt *update_stmt = NULL;
        char update_str[1024] = "";
        char *freq_cntr_str = NULL;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, update_time, out);

        /*
         * Constructing the prepare statement string.
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_PREPARE_FAILED, "Failed preparing insert "
                        "statement %s : %s", update_str,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind time secs*/
        ret = sqlite3_bind_int (update_stmt, 1, update_time->tv_sec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent wind "
                        "secs %ld : %s", update_time->tv_sec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind time msecs*/
        ret = sqlite3_bind_int (update_stmt, 2, update_time->tv_usec);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding parent wind "
                        "msecs %ld : %s", update_time->tv_usec,
                        sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Bind gfid*/
        ret = sqlite3_bind_text (update_stmt, 3, gfid, -1, NULL);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_BINDING_FAILED, "Failed binding gfid %s : %s",
                        gfid, sqlite3_errmsg (sql_conn->sqlite3_db_conn));
                ret = -1;
                goto out;
        }

        /*Execute the prepare statement*/
        if (sqlite3_step (update_stmt) != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3,
                        _gfdb_log_level (GF_LOG_ERROR, ignore_errors), 0,
                        LG_MSG_EXEC_FAILED,
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_CREATE_FAILED,
                        "Creating gfid string failed.");
                goto out;
        }

        modtime = &gfdb_db_record->gfdb_wind_change_time;

        /* handle all dentry based operations */
        if (isdentryfop (gfdb_db_record->gfdb_fop_type)) {
                /*Parent GFID is always set*/
                pargfid_str = gf_strdup (uuid_utoa (gfdb_db_record->pargfid));
                if (!pargfid_str) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                0, LG_MSG_CREATE_FAILED, "Creating gfid string "
                                "failed.");
                        goto out;
                }

                /* handle create, mknod */
                if (isdentrycreatefop (gfdb_db_record->gfdb_fop_type)) {
                        /*insert link*/
                        ret = gf_sql_insert_link(sql_conn,
                                        gfid_str, pargfid_str,
                                        gfdb_db_record->file_name,
                                        gfdb_db_record->link_consistency,
                                        _gf_true);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3,
                                        _gfdb_log_level (GF_LOG_WARNING,
                                                gfdb_db_record->ignore_errors),
                                        0,
                                        LG_MSG_INSERT_FAILED, "Failed "
                                        "inserting link in DB");
                                /* Even if link creation is failed we
                                 * continue with the creation of file record.
                                 * This covers to cases
                                 * 1) Lookup heal: If the file record from
                                 * gf_file_tb is deleted but the link record
                                 * still exist. Lookup heal will attempt a heal
                                 * with create_wind set. The link heal will fail
                                 * as there is already a record and if we dont
                                 * ignore the error we will not heal the
                                 * gf_file_tb.
                                 * 2) Rename file in cold tier: During a rename
                                 * of a file that is there in cold tier. We get
                                 * an link record created in hot tier for the
                                 * linkto file. When the file gets heated and
                                 * moves to hot tier there will be attempt from
                                 * ctr lookup heal to create link and file
                                 * record and If we dont ignore the error we
                                 * will not heal the gf_file_tb.
                                 * */
                        }
                        gfdb_db_record->islinkupdate = gfdb_db_record->
                                                        link_consistency;

                        /*
                         * Only for create/mknod insert wind time
                         * for the first time
                         * */
                        ret = gf_sql_insert_write_wind_time (sql_conn, gfid_str,
                                        modtime, gfdb_db_record->ignore_errors);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3,
                                        _gfdb_log_level (GF_LOG_ERROR,
                                                gfdb_db_record->ignore_errors),
                                        0, LG_MSG_INSERT_FAILED,
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
                                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                                0, LG_MSG_CREATE_FAILED,
                                                "Creating gfid string failed.");
                                        goto out;
                                }
                                ret = gf_sql_update_link (sql_conn, gfid_str,
                                                pargfid_str,
                                                gfdb_db_record->file_name,
                                                old_pargfid_str,
                                                gfdb_db_record->old_file_name,
                                                gfdb_db_record->
                                                        link_consistency,
                                                gfdb_db_record->ignore_errors);
                                if (ret) {
                                        gf_msg (GFDB_STR_SQLITE3,
                                                _gfdb_log_level (GF_LOG_ERROR,
                                                gfdb_db_record->ignore_errors),
                                                0, LG_MSG_UPDATE_FAILED,
                                                "Failed updating link");
                                        goto out;
                                }
                                gfdb_db_record->islinkupdate = gfdb_db_record->
                                                        link_consistency;
                        }
                        /*link*/
                        else {
                                ret = gf_sql_insert_link (sql_conn,
                                                gfid_str, pargfid_str,
                                                gfdb_db_record->file_name,
                                                gfdb_db_record->
                                                        link_consistency,
                                                gfdb_db_record->ignore_errors);
                                if (ret) {
                                        gf_msg (GFDB_STR_SQLITE3,
                                                _gfdb_log_level (GF_LOG_ERROR,
                                                gfdb_db_record->ignore_errors),
                                                0, LG_MSG_INSERT_FAILED,
                                                "Failed inserting link in DB");
                                        goto out;
                                }
                                gfdb_db_record->islinkupdate = gfdb_db_record->
                                                        link_consistency;
                        }
                }
        }

        /* update times only when said!*/
        if (gfdb_db_record->do_record_times) {
                /*All fops update times read or write*/
                ret = gf_update_time (sql_conn, gfid_str, modtime,
                                gfdb_db_record->do_record_counters,
                                its_wind,
                                isreadfop (gfdb_db_record->gfdb_fop_type),
                                gfdb_db_record->ignore_errors);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3,
                                _gfdb_log_level (GF_LOG_ERROR,
                                gfdb_db_record->ignore_errors), 0,
                                LG_MSG_UPDATE_FAILED, "Failed update wind time"
                                " in DB");
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_CREATE_FAILED, "Creating gfid string failed.");
                goto out;
        }

        /*Only update if recording unwind is set*/
        if (gfdb_db_record->do_record_times &&
                gfdb_db_record->do_record_uwind_time) {
                modtime = &gfdb_db_record->gfdb_unwind_change_time;
                ret = gf_update_time (sql_conn, gfid_str, modtime,
                        gfdb_db_record->do_record_counters,
                        (!its_wind),
                        isreadfop (gfdb_db_record->gfdb_fop_type),
                        gfdb_db_record->ignore_errors);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3,
                                _gfdb_log_level (GF_LOG_ERROR,
                                        gfdb_db_record->ignore_errors),
                                0, LG_MSG_UPDATE_FAILED, "Failed update unwind "
                                "time in DB");
                        goto out;
                }
        }

        /*For link creation and changes we use link updated*/
        if (gfdb_db_record->islinkupdate &&
                isdentryfop(gfdb_db_record->gfdb_fop_type)) {

                pargfid_str = gf_strdup(uuid_utoa(gfdb_db_record->pargfid));
                if (!pargfid_str) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                0, LG_MSG_CREATE_FAILED,
                                "Creating pargfid_str string failed.");
                        goto out;
                }

                ret = gf_sql_update_link_flags (sql_conn, gfid_str, pargfid_str,
                                        gfdb_db_record->file_name, 0, _gf_true,
                                        gfdb_db_record->ignore_errors);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3,
                                _gfdb_log_level (GF_LOG_ERROR,
                                        gfdb_db_record->ignore_errors),
                                0, LG_MSG_UPDATE_FAILED,
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
        char *gfid_str          = NULL;
        char *pargfid_str       = NULL;

        CHECK_SQL_CONN (sql_conn, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, gfdb_db_record, out);

        gfid_str = gf_strdup (uuid_utoa(gfdb_db_record->gfid));
        if (!gfid_str) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_CREATE_FAILED,
                        "Creating gfid string failed.");
                goto out;
        }

        pargfid_str = gf_strdup (uuid_utoa(gfdb_db_record->pargfid));
        if (!pargfid_str) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                0, LG_MSG_CREATE_FAILED, "Creating pargfid_str "
                                "string failed.");
                        goto out;
        }

        if (gfdb_db_record->link_consistency) {
                ret = gf_sql_update_link_flags (sql_conn, gfid_str, pargfid_str,
                                        gfdb_db_record->file_name, 1,
                                        _gf_false,
                                        gfdb_db_record->ignore_errors);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3,
                                _gfdb_log_level (GF_LOG_ERROR,
                                        gfdb_db_record->ignore_errors),
                                0, LG_MSG_UPDATE_FAILED,
                                "Failed updating link flags in wind");
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
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_CREATE_FAILED,
                        "Creating gfid string failed.");
                goto out;
        }

        /*Nuke all the entries for this GFID from DB*/
        if (gfdb_db_record->gfdb_fop_path == GFDB_FOP_UNDEL_ALL) {
                gf_sql_delete_all (sql_conn, gfid_str,
                                gfdb_db_record->ignore_errors);
        }
        /*Remove link entries only*/
        else if (gfdb_db_record->gfdb_fop_path == GFDB_FOP_UNDEL) {

                pargfid_str = gf_strdup(uuid_utoa(gfdb_db_record->pargfid));
                if (!pargfid_str) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                0, LG_MSG_CREATE_FAILED, "Creating pargfid_str "
                                "string failed.");
                        goto out;
                }

                /* Special performance case:
                 * Updating wind time in unwind for delete. This is done here
                 * as in the wind path we will not know whether its the last
                 * link or not. For a last link there is not use to update any
                 * wind or unwind time!*/
                if (gfdb_db_record->do_record_times) {
                        /*Update the wind write times*/
                        modtime = &gfdb_db_record->gfdb_wind_change_time;
                        ret = gf_update_time (sql_conn, gfid_str, modtime,
                                gfdb_db_record->do_record_counters,
                                _gf_true,
                                isreadfop (gfdb_db_record->gfdb_fop_type),
                                gfdb_db_record->ignore_errors);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3,
                                        _gfdb_log_level (GF_LOG_ERROR,
                                                gfdb_db_record->ignore_errors),
                                        0, LG_MSG_UPDATE_FAILED,
                                        "Failed update wind time in DB");
                                goto out;
                        }
                }

                modtime = &gfdb_db_record->gfdb_unwind_change_time;

                ret = gf_sql_delete_link(sql_conn, gfid_str, pargfid_str,
                                        gfdb_db_record->file_name,
                                        gfdb_db_record->ignore_errors);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_DELETE_FAILED, "Failed deleting link");
                        goto out;
                }

                if (gfdb_db_record->do_record_times &&
                        gfdb_db_record->do_record_uwind_time) {
                        ret = gf_update_time (sql_conn, gfid_str, modtime,
                                gfdb_db_record->do_record_counters,
                                _gf_false,
                                isreadfop(gfdb_db_record->gfdb_fop_type),
                                gfdb_db_record->ignore_errors);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3,
                                        _gfdb_log_level (GF_LOG_ERROR,
                                                gfdb_db_record->ignore_errors),
                                        0, LG_MSG_UPDATE_FAILED,
                                        "Failed update unwind time in DB");
                                goto out;
                        }
                }
        } else {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                        0, LG_MSG_INVALID_UPLINK, "Invalid unlink option");
                goto out;
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
        int ret                                         = -1;
        gfdb_query_record_t *query_record               = NULL;
        char *text_column                               = NULL;
        sqlite3 *db_conn                                = NULL;
        uuid_t  prev_gfid                               = {0};
        uuid_t  curr_gfid                               = {0};
        uuid_t  pgfid                                   = {0};
        char *base_name                                 = NULL;
        gf_boolean_t is_first_record                    = _gf_true;
        gf_boolean_t is_query_empty                     = _gf_true;

        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, prep_stmt, out);
        GF_VALIDATE_OR_GOTO (GFDB_STR_SQLITE3, query_callback, out);

        db_conn = sqlite3_db_handle(prep_stmt);

        /*
         * Loop to access queried rows
         * Each db record will have 3 columns
         * GFID, PGFID, FILE_NAME
         *
         * For file with multiple hard links we will get multiple query rows
         * with the same GFID, but different PGID and FILE_NAME Combination
         * For Example if a file with
         *         GFID = 00000000-0000-0000-0000-000000000006
         * has 3 hardlinks file1, file2 and file3 in 3 different folder
         * with GFID's
         * 00000000-0000-0000-0000-0000EFC00001,
         * 00000000-0000-0000-0000-00000ABC0001 and
         * 00000000-0000-0000-0000-00000ABC00CD
         * Then there will be 3 records
         *         GFID         : 00000000-0000-0000-0000-000000000006
         *         PGFID        : 00000000-0000-0000-0000-0000EFC00001
         *         FILE_NAME    : file1
         *
         *         GFID         : 00000000-0000-0000-0000-000000000006
         *         PGFID        : 00000000-0000-0000-0000-00000ABC0001
         *         FILE_NAME    : file2
         *
         *         GFID         : 00000000-0000-0000-0000-000000000006
         *         PGFID        : 00000000-0000-0000-0000-00000ABC00CD
         *         FILE_NAME    : file3
         *
         * This is retrieved and added to a single query_record
         *
         * query_record->gfid = 00000000-0000-0000-0000-000000000006
         *                  ->link_info = {00000000-0000-0000-0000-0000EFC00001,
         *                                 "file1"}
         *                                  |
         *                                  V
         *             link_info = {00000000-0000-0000-0000-00000ABC0001,
         *                                 "file2"}
         *                                  |
         *                                  V
         *             link_info = {00000000-0000-0000-0000-00000ABC0001,
         *                                 "file3",
         *                                 list}
         *
         * This query record is sent to the registered query_callback()
         *
         * */
        while ((ret = sqlite3_step (prep_stmt)) == SQLITE_ROW) {

                if (sqlite3_column_count(prep_stmt) > 0) {

                        is_query_empty = _gf_false;

                        /*Retrieving GFID - column index is 0*/
                        text_column = (char *)sqlite3_column_text
                                                        (prep_stmt, 0);
                        if (!text_column) {
                                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                        LG_MSG_GET_ID_FAILED, "Failed to"
                                        "retrieve GFID");
                                goto out;
                        }
                        ret = gf_uuid_parse (text_column, curr_gfid);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                        LG_MSG_PARSE_FAILED, "Failed to parse "
                                        "GFID");
                                goto out;
                        }

                        /*
                         * if the previous record was not of the current gfid
                         * call the call_back function and send the
                         * query record, which will have all the link_info
                         * objects associated with this gfid
                         *
                         * */
                        if (gf_uuid_compare (curr_gfid, prev_gfid) != 0) {

                                /* If this is not the first record */
                                if (!is_first_record) {
                                        /*Call the call_back function provided*/
                                        ret = query_callback (query_record,
                                                        _query_cbk_args);
                                        if (ret) {
                                                gf_msg (GFDB_STR_SQLITE3,
                                                        GF_LOG_ERROR, 0,
                                                LG_MSG_QUERY_CALL_BACK_FAILED,
                                                        "Query call back "
                                                        "failed");
                                                goto out;
                                        }

                                }

                                /*Clear the query record*/
                                gfdb_query_record_free (query_record);
                                query_record = NULL;
                                query_record = gfdb_query_record_new ();
                                if (!query_record) {
                                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR,
                                                0, LG_MSG_CREATE_FAILED,
                                                "Failed to create "
                                                "query_record");
                                        goto out;
                                }

                                gf_uuid_copy(query_record->gfid,
                                                                curr_gfid);
                                gf_uuid_copy(prev_gfid, curr_gfid);

                        }

                        /* Get PGFID */
                        text_column = (char *)sqlite3_column_text
                                                        (prep_stmt, 1);
                        if (!text_column) {
                                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                        LG_MSG_GET_ID_FAILED, "Failed to"
                                        " retrieve GF_ID");
                                goto out;
                        }
                        ret = gf_uuid_parse (text_column, pgfid);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                        LG_MSG_PARSE_FAILED, "Failed to parse "
                                        "GF_ID");
                                goto out;
                        }

                        /* Get Base name */
                        text_column = (char *)sqlite3_column_text
                                                        (prep_stmt, 2);
                        if (!text_column) {
                                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                        LG_MSG_GET_ID_FAILED, "Failed to"
                                        " retrieve GF_ID");
                                goto out;
                        }
                        base_name = text_column;


                        /* Add link info to the list */
                        ret = gfdb_add_link_to_query_record (query_record,
                                                         pgfid, base_name);
                        if (ret) {
                                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                        LG_MSG_GET_ID_FAILED, "Failed to"
                                        " add link info to query record");
                                goto out;
                        }

                        is_first_record = _gf_false;

                }

        }

        if (ret != SQLITE_DONE) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                        LG_MSG_GET_RECORD_FAILED, "Failed to retrieve records "
                        "from db : %s", sqlite3_errmsg (db_conn));
                ret = -1;
                goto out;
        }


        if (!is_query_empty) {
                /*
                 * Call the call_back function for the last record from the
                 * Database
                 * */
                ret = query_callback (query_record, _query_cbk_args);
                if (ret) {
                        gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0,
                                LG_MSG_QUERY_CALL_BACK_FAILED,
                                "Query call back failed");
                        goto out;
                }
        }

        ret = 0;
out:
        gfdb_query_record_free (query_record);
        query_record = NULL;
        return ret;
}



int
gf_sql_clear_counters (gf_sql_connection_t *sql_conn)
{
        int ret                 = -1;
        char *sql_strerror      = NULL;
        char *query_str         = NULL;

        CHECK_SQL_CONN (sql_conn, out);

        query_str = "UPDATE "
                    GF_FILE_TABLE
                    " SET " GF_COL_READ_FREQ_CNTR " = 0 , "
                    GF_COL_WRITE_FREQ_CNTR " = 0 ;";

        ret = sqlite3_exec (sql_conn->sqlite3_db_conn, query_str, NULL, NULL,
                                &sql_strerror);
        if (ret != SQLITE_OK) {
                gf_msg (GFDB_STR_SQLITE3, GF_LOG_ERROR, 0, LG_MSG_EXEC_FAILED,
                        "Failed to execute: %s : %s",
                        query_str, sql_strerror);
                sqlite3_free (sql_strerror);
                        ret = -1;
                        goto out;
        }

        ret = 0;
out:
        return ret;
}
