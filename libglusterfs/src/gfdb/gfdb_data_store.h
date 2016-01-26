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
} gfdb_ipc_ctr_params_t;


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
 *      _conn_node              : GFDB Connection node
 *
 * Returns : if successful return 0 or
 *          -ve value in case of failure
 **/
int
clear_files_heat (gfdb_conn_node_t *_conn_node);

typedef int (*clear_files_heat_t) (gfdb_conn_node_t *_conn_node);



/* Libgfdb API Function: Function to extract version of the db
 *  Arguments:
 *      gfdb_conn_node_t *_conn_node        : GFDB Connection node
 *      char **version  : the version is extracted as a string
 *                   and will be stored in this variable.
 *                   The freeing of the memory should be done by the caller.
 * Return:
 *      On success return the length of the version string that is
 *      extracted.
 *      On failure return -1
 * */
int
get_db_version (gfdb_conn_node_t *_conn_node, char **version);

typedef int (*get_db_version_t)(gfdb_conn_node_t *_conn_node,
                                        char **version);


/* Libgfdb API Function: Function to extract param from the db
 *  Arguments:
 *      gfdb_conn_node_t *_conn_node        : GFDB Connection node
 *      char *param_key     : param to be extracted
 *      char **param_value  : the value of the param that is
 *                       extracted. This function will allocate memory
 *                       to pragma_value. The caller should free the memory.
 * Return:
 *      On success return the lenght of the param value that is
 *      extracted.
 *      On failure return -1
 * */
int
get_db_params (gfdb_conn_node_t *_conn_node,
                char *param_key,
                char **param_value);

typedef int (*get_db_params_t)(gfdb_conn_node_t *db_conn,
                                     char *param_key,
                                     char **param_value);


/* Libgfdb API Function: Function to set db params
 * Arguments:
 *      gfdb_conn_node_t *_conn_node        : GFDB Connection node
 *      char *param_key     : param to be set
 * char *param_value  : param value
 * Return:
 *      On success return 0
 *      On failure return -1
 * */
int
set_db_params (gfdb_conn_node_t *_conn_node,
                char *param_key,
                char *param_value);

typedef int (*set_db_params_t)(gfdb_conn_node_t *db_conn,
                                     char *param_key,
                                     char *param_value);



typedef struct gfdb_methods_s {
        init_db_t                       init_db;
        fini_db_t                       fini_db;
        find_unchanged_for_time_t       find_unchanged_for_time;
        find_recently_changed_files_t   find_recently_changed_files;
        find_unchanged_for_time_freq_t  find_unchanged_for_time_freq;
        find_recently_changed_files_freq_t find_recently_changed_files_freq;
        clear_files_heat_t              clear_files_heat;
        get_db_version_t                get_db_version;
        get_db_params_t                 get_db_params;
        set_db_params_t                 set_db_params;
        /* Do not expose dbpath directly. Expose it via an */
        /* access function: get_db_path_key(). */
        char                            *dbpath;
        get_db_path_key_t               get_db_path_key;

        /* Query Record related functions */
        gfdb_query_record_new_t         gfdb_query_record_new;
        gfdb_query_record_free_t        gfdb_query_record_free;
        gfdb_add_link_to_query_record_t gfdb_add_link_to_query_record;
        gfdb_write_query_record_t       gfdb_write_query_record;
        gfdb_read_query_record_t        gfdb_read_query_record;

        /* Link info related functions */
        gfdb_link_info_new_t            gfdb_link_info_new;
        gfdb_link_info_free_t           gfdb_link_info_free;

} gfdb_methods_t;

void get_gfdb_methods (gfdb_methods_t *methods);

typedef void (*get_gfdb_methods_t) (gfdb_methods_t *methods);

#endif
