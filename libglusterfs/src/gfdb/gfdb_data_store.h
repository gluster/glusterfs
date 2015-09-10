/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GFDB_DATA_STORE_H
#define __GFDB_DATA_STORE_H


#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "common-utils.h"
#include <time.h>
#include <sys/time.h>

#include "gfdb_data_store_types.h"

/* GFDB Connection Node:
 * ~~~~~~~~~~~~~~~~~~~~
 * Represents the connection to the database while using libgfdb
 * The connection node is not thread safe as far as fini_db is concerned.
 * You can use a single connection node
 * to do multithreaded db operations like insert/delete/find of records.
 * But you need to wait for all the operating threads to complete i.e
 * pthread_join() and then do fini_db() to kill the connection node.
 * gfdb_conn_node_t is an opaque structure.
 * */
typedef struct gfdb_conn_node_t gfdb_conn_node_t;




/*Libgfdb API Function: Used to initialize db connection
 * Arguments:
 *      args         :  Dictionary containing database specific parameters
 *                      eg: For sqlite3, pagesize, cachesize, db name, db path
                        etc
 *      gfdb_db_type :  Type of data base used i.e sqlite or hyperdex etc
 * Returns : if successful return the GFDB Connection Node to the caller or
 *          NULL value in case of failure*/
gfdb_conn_node_t *
init_db(dict_t *arg, gfdb_db_type_t db_type);

typedef gfdb_conn_node_t * (*init_db_t) (dict_t *args,
                                         gfdb_db_type_t gfdb_db_type);




/*Libgfdb API Function: Used to terminate/de-initialize db connection
 *                      (Destructor function for db connection object)
 * Arguments:
 *      _conn_node  :  DB Connection Index of the DB Connection
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int
fini_db(gfdb_conn_node_t *);

typedef int (*fini_db_t) (gfdb_conn_node_t *_conn_node);



/*Libgfdb API Function: Used to insert/updated records in the database
 *                      NOTE: In current gfdb_sqlite plugin we use that
 *                      same function to delete the record. Set the
 *                      gfdb_fop_path to GFDB_FOP_UNDEL to delete the
 *                      link of inode from GF_FLINK_TB and
 *                      GFDB_FOP_UNDEL_ALL to delete all the records from
 *                      GF_FLINK_TB and GF_FILE_TB.
 *                      TODO: Should seperate this function into the
 *                      delete_record function
 *                      Refer CTR Xlator features/changetimerecorder for usage
 * Arguments:
 *      _conn_node     :  GFDB Connection node
 *      gfdb_db_record :  Record to be inserted/updated
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int
insert_record(gfdb_conn_node_t *, gfdb_db_record_t *gfdb_db_record);




/*Libgfdb API Function: Used to delete record from the database
 *                      NOTE: In the current gfdb_sqlite3 plugin
 *                      implementation this function is dummy.
 *                      Use the insert_record function.
 *                      Refer CTR Xlator features/changetimerecorder for usage
 * Arguments:
 *      _conn_node     :  GFDB Connection node
 *      gfdb_db_record :  Record to be deleted
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int
delete_record(gfdb_conn_node_t *, gfdb_db_record_t *gfdb_db_record);





/*Libgfdb API Function: Query all the records from the database
 * Arguments:
 *      _conn_node      : GFDB Connection node
 *      query_callback  : Call back function that will be called
 *                        for every record found
 *      _query_cbk_args : Custom argument passed for the call back
 *                        function query_callback
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int find_all(gfdb_conn_node_t *, gf_query_callback_t query_callback,
                void *_query_cbk_args);





/*Libgfdb API Function: Query records/files that have not changed/accessed
 *                      from a time in past to current time
 * Arguments:
 *      _conn_node              : GFDB Connection node
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _query_cbk_args         : Custom argument passed for the call back
 *                                function query_callback
 *      for_time                : Time from where the file/s are not
 *                                changed/accessed
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int find_unchanged_for_time(gfdb_conn_node_t *,
                        gf_query_callback_t query_callback,
                        void *_query_cbk_args, gfdb_time_t *for_time);

typedef int (*find_unchanged_for_time_t) (gfdb_conn_node_t *_conn_node,
                                          gf_query_callback_t query_callback,
                                          void *_query_cbk_args,
                                          gfdb_time_t *for_time);




/*Libgfdb API Function: Query records/files that have changed/accessed from a
 *                      time in past to current time
 * Arguments:
 *      _conn_node              : GFDB Connection node
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _query_cbk_args         : Custom argument passed for the call back
 *                                function query_callback
 *      for_time                : Time from where the file/s are
 *                                changed/accessed
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int find_recently_changed_files(gfdb_conn_node_t *_conn,
                gf_query_callback_t query_callback, void *_query_cbk_args,
                gfdb_time_t *from_time);

typedef int (*find_recently_changed_files_t) (gfdb_conn_node_t *_conn_node,
                                              gf_query_callback_t query_callback,
                                              void *_query_cbk_args,
                                              gfdb_time_t *from_time);




/*Libgfdb API Function: Query records/files that have not changed/accessed
 *                      from a time in past to current time, with
 *                      a desired frequency
 * Arguments:
 *      _conn_node              : GFDB Connection node
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _query_cbk_args         : Custom argument passed for the call back
 *                                function query_callback
 *      for_time                : Time from where the file/s are not
 *                                changed/accessed
 *      write_freq_thresold     : Desired Write Frequency lower limit
 *      read_freq_thresold      : Desired Read Frequency lower limit
 *      _clear_counters         : If true, Clears all the frequency counters of
 *                                all files.
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int find_unchanged_for_time_freq(gfdb_conn_node_t *_conn,
                                        gf_query_callback_t query_callback,
                                        void *_query_cbk_args,
                                        gfdb_time_t *for_time,
                                        int write_freq_thresold,
                                        int read_freq_thresold,
                                        gf_boolean_t _clear_counters);

typedef int (*find_unchanged_for_time_freq_t) (gfdb_conn_node_t *_conn_node,
                                               gf_query_callback_t query_callback,
                                               void *_query_cbk_args,
                                               gfdb_time_t *for_time,
                                               int write_freq_thresold,
                                               int read_freq_thresold,
                                               gf_boolean_t _clear_counters);




/*Libgfdb API Function: Query records/files that have changed/accessed from a
 *                      time in past to current time, with
 *                      a desired frequency
 * Arguments:
 *      _conn_node              : GFDB Connection node
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _query_cbk_args         : Custom argument passed for the call back
 *                                function query_callback
 *      for_time                : Time from where the file/s are
 *                                changed/accessed
 *      write_freq_thresold     : Desired Write Frequency lower limit
 *      read_freq_thresold      : Desired Read Frequency lower limit
 *      _clear_counters         : If true, Clears all the frequency counters of
 *                                all files.
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int find_recently_changed_files_freq(gfdb_conn_node_t *_conn,
                                gf_query_callback_t query_callback,
                                void *_query_cbk_args,
                                gfdb_time_t *from_time,
                                int write_freq_thresold,
                                int read_freq_thresold,
                                gf_boolean_t _clear_counters);

typedef int (*find_recently_changed_files_freq_t) (gfdb_conn_node_t *_conn_node,
                                                   gf_query_callback_t query_callback,
                                                   void *_query_cbk_args,
                                                   gfdb_time_t *from_time,
                                                   int write_freq_thresold,
                                                   int read_freq_thresold,
                                                   gf_boolean_t _clear_counters);

typedef const
char *(*get_db_path_key_t)();

/*Libgfdb API Function: Clear the heat for all the files
 *
 * Arguments:
 *  _conn_node              : GFDB Connection node
 *
 * Returns : if successful return 0 or
 *          -ve value in case of failure
 **/
int
clear_files_heat (gfdb_conn_node_t *_conn_node);

typedef struct gfdb_methods_s {
        init_db_t init_db;
        fini_db_t fini_db;
        find_unchanged_for_time_t find_unchanged_for_time;
        find_recently_changed_files_t find_recently_changed_files;
        find_unchanged_for_time_freq_t find_unchanged_for_time_freq;
        find_recently_changed_files_freq_t find_recently_changed_files_freq;
        /* Do not expose dbpath directly. Expose it via an */
        /* access function: get_db_path_key(). */
        char *dbpath;
        get_db_path_key_t get_db_path_key;
} gfdb_methods_t;

void get_gfdb_methods (gfdb_methods_t *methods);

typedef void (*get_gfdb_methods_t) (gfdb_methods_t *methods);


#endif
