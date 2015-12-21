/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GFDB_SQLITE3_HELPER_H
#define __GFDB_SQLITE3_HELPER_H


#include "gfdb_sqlite3.h"

/******************************************************************************
 *
 *                      Helper functions for gf_sqlite3_insert()
 *
 * ****************************************************************************/


int
gf_sql_insert_wind (gf_sql_connection_t  *sql_conn,
                   gfdb_db_record_t     *gfdb_db_record);

int
gf_sql_insert_unwind (gf_sql_connection_t  *sql_conn,
                     gfdb_db_record_t     *gfdb_db_record);


int
gf_sql_update_delete_wind (gf_sql_connection_t  *sql_conn,
                          gfdb_db_record_t     *gfdb_db_record);

int
gf_sql_delete_unwind (gf_sql_connection_t  *sql_conn,
                          gfdb_db_record_t     *gfdb_db_record);





/******************************************************************************
 *
 *                      Find/Query helper functions
 *
 * ****************************************************************************/


int
gf_sql_query_function (sqlite3_stmt              *prep_stmt,
                      gf_query_callback_t       query_callback,
                      void                      *_query_cbk_args);

int
gf_sql_clear_counters (gf_sql_connection_t *sql_conn);

#endif
