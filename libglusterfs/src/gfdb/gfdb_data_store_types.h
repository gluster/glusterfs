/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GFDB_DATA_STORE_TYPE_H
#define __GFDB_DATA_STORE_TYPE_H

#include "gfdb_data_store_helper.h"

/*
 * Helps in dynamically choosing log level
 * */
static inline gf_loglevel_t
_gfdb_log_level (gf_loglevel_t given_level,
                 gf_boolean_t ignore_level)
{
        return (ignore_level) ? GF_LOG_DEBUG : given_level;
}

typedef enum gf_db_operation {
        GFDB_INVALID_DB_OP = -1,
        /* Query DB OPS : All the Query DB_OP should be added */
        /* in between START and END */
        GFDB_QUERY_DB_OP_START, /* Start of Query DB_OP  */
        GFDB_QUERY_DB_OP,
        GF_FTABLE_EXISTS_DB_OP,
        GFDB_QUERY_DB_OP_END, /* End of Query DB_OP */
        /* Non-Query DB OPS */
        GFDB_DB_CREATE_DB_OP,
        GFDB_GFID_EXIST_DB_OP,
        GFDB_W_INSERT_DB_OP,
        GFDB_WU_INSERT_DB_OP,
        GFDB_W_UPDATE_DB_OP,
        GFDB_WU_UPDATE_DB_OP,
        GFDB_W_DELETE_DB_OP,
        GFDB_UW_DELETE_DB_OP,
        GFDB_WFC_UPDATE_DB_OP,
        GFDB_RFC_UPDATE_DB_OP,
        GFDB_DB_COMPACT_DB_OP /* Added for VACUUM/manual compaction support */
} gf_db_operation_t;


#define GF_COL_MAX_NUM                  2
#define GF_COL_ALL                      " * "

/* Column/fields names used in the DB.
 * If any new field is added should be updated here*/
#define GF_COL_GF_ID                    "GF_ID"
#define GF_COL_GF_PID                   "GF_PID"
#define GF_COL_FILE_NAME                "FNAME"
#define GF_COL_WSEC                     "W_SEC"
#define GF_COL_WMSEC                    "W_MSEC"
#define GF_COL_UWSEC                    "UW_SEC"
#define GF_COL_UWMSEC                   "UW_MSEC"
#define GF_COL_WSEC_READ                "W_READ_SEC"
#define GF_COL_WMSEC_READ               "W_READ_MSEC"
#define GF_COL_UWSEC_READ               "UW_READ_SEC"
#define GF_COL_UWMSEC_READ              "UW_READ_MSEC"
#define GF_COL_WDEL_FLAG                "W_DEL_FLAG"
#define GF_COL_WRITE_FREQ_CNTR          "WRITE_FREQ_CNTR"
#define GF_COL_READ_FREQ_CNTR           "READ_FREQ_CNTR"
#define GF_COL_LINK_UPDATE              "LINK_UPDATE"


/***********************Time related********************************/
/*1 sec = 1000000 microsec*/
#define GFDB_MICROSEC         1000000

/*All the gfdb times are represented using this structure*/
typedef  struct timeval gfdb_time_t;

/*Convert time into  seconds*/
static inline uint64_t
gfdb_time_2_usec(gfdb_time_t *gfdb_time)
{
        GF_ASSERT(gfdb_time);
        return ((uint64_t) gfdb_time->tv_sec * GFDB_MICROSEC) + gfdb_time->tv_usec;
}

/******************************************************************************
 *
 *              Insert/Update Record related data structures/functions
 *
 * ****************************************************************************/

/*Indicated a generic synchronous write to the db
 * This may or may not be implemented*/
typedef enum gfdb_sync_type {
        GFDB_INVALID_SYNC = -1,
        GFDB_DB_ASYNC,
        GFDB_DB_SYNC
} gfdb_sync_type_t;

/*Strings related to the abvove sync type*/
#define GFDB_STR_DB_ASYNC        "async"
#define GFDB_STR_DB_SYNC         "sync"

/*To convert sync type from string to gfdb_sync_type_t*/
static inline int
gf_string2gfdbdbsync (char *sync_option)
{
        int ret = -1;

        if (!sync_option)
                goto out;
        if (strcmp(sync_option, GFDB_STR_DB_ASYNC) == 0) {
                ret = GFDB_DB_ASYNC;
        } else if (strcmp(sync_option, GFDB_STR_DB_SYNC) == 0) {
                ret = GFDB_DB_SYNC;
        }
out:
        return ret;
}

/*Indicated different types of db*/
typedef enum gfdb_db_type {
        GFDB_INVALID_DB = -1,
        GFDB_HASH_FILE_STORE,
        GFDB_ROCKS_DB,
        GFDB_SQLITE3,
        GFDB_HYPERDEX,
        GFDB_DB_END /*Add DB type Entries above this only*/
} gfdb_db_type_t;

/*String related to the db types*/
#define GFDB_STR_HASH_FILE_STORE      "hashfile"
#define GFDB_STR_ROCKS_DB             "rocksdb"
#define GFDB_STR_SQLITE3              "sqlite3"
#define GFDB_STR_HYPERDEX             "hyperdex"

/*Convert db type in string to gfdb_db_type_t*/
static inline int
gf_string2gfdbdbtype (char *db_option)
{
        int ret = -1;

        if (!db_option)
                goto out;
        if (strcmp(db_option, GFDB_STR_HASH_FILE_STORE) == 0) {
                ret = GFDB_HASH_FILE_STORE;
        } else if (strcmp(db_option, GFDB_STR_ROCKS_DB) == 0) {
                ret = GFDB_ROCKS_DB;
        } else if (strcmp(db_option, GFDB_STR_SQLITE3) == 0) {
                ret = GFDB_SQLITE3;
        } else if (strcmp(db_option, GFDB_STR_HYPERDEX) == 0) {
                ret = GFDB_HYPERDEX;
        }
out:
        return ret;
}

/*Tells the path of the fop*/
typedef enum gfdb_fop_path {
        GFDB_FOP_INVALID = -1,
        /*Filler value for zero*/
        GFDB_FOP_PATH_ZERO = 0,
        /*have wind path below this*/
        GFDB_FOP_WIND = 1,
        GFDB_FOP_WDEL = 2,
        /*have unwind path below this*/
        GFDB_FOP_UNWIND = 4,
        /*Delete unwind path*/
        GFDB_FOP_UNDEL = 8,
        GFDB_FOP_UNDEL_ALL = 16
} gfdb_fop_path_t;
/*Strings related to the above fop path*/
#define GFDB_STR_FOP_INVALID "INVALID"
#define GFDB_STR_FOP_WIND "ENTRY"
#define GFDB_STR_FOP_UNWIND "EXIT"
#define GFDB_STR_FOP_WDEL "WDEL"
#define GFDB_STR_FOP_UNDEL "UNDEL"

static inline gf_boolean_t
iswindpath(gfdb_fop_path_t gfdb_fop_path)
{
        return ((gfdb_fop_path == GFDB_FOP_WIND) ||
                        (gfdb_fop_path == GFDB_FOP_WDEL)) ?
                                                _gf_true : _gf_false;
}

static inline gf_boolean_t
isunwindpath(gfdb_fop_path_t gfdb_fop_path)
{
        return (gfdb_fop_path >= GFDB_FOP_UNWIND) ? _gf_true : _gf_false;
}

/*Tell what type of fop it was
 * Like whether a dentry fop or a inode fop
 * Read fop or a write fop etc*/
typedef enum gfdb_fop_type {
        GFDB_FOP_INVALID_OP = -1,
        /*Filler value for zero*/
        GFDB_FOP_TYPE_ZERO = 0,
        GFDB_FOP_DENTRY_OP = 1,
        GFDB_FOP_DENTRY_CREATE_OP = 2,
        GFDB_FOP_INODE_OP = 4,
        GFDB_FOP_WRITE_OP = 8,
        GFDB_FOP_READ_OP = 16
} gfdb_fop_type_t;

#define GFDB_FOP_INODE_WRITE\
        (GFDB_FOP_INODE_OP | GFDB_FOP_WRITE_OP)

#define GFDB_FOP_DENTRY_WRITE\
        (GFDB_FOP_DENTRY_OP | GFDB_FOP_WRITE_OP)

#define GFDB_FOP_CREATE_WRITE\
        (GFDB_FOP_DENTRY_CREATE_OP | GFDB_FOP_WRITE_OP)

#define GFDB_FOP_INODE_READ\
        (GFDB_FOP_INODE_OP | GFDB_FOP_READ_OP)

static inline gf_boolean_t
isreadfop(gfdb_fop_type_t fop_type)
{
        return (fop_type & GFDB_FOP_READ_OP) ? _gf_true : _gf_false;
}

static inline gf_boolean_t
isdentryfop(gfdb_fop_type_t fop_type)
{
        return ((fop_type & GFDB_FOP_DENTRY_OP) ||
                (fop_type & GFDB_FOP_DENTRY_CREATE_OP)) ? _gf_true : _gf_false;
}

static inline gf_boolean_t
isdentrycreatefop(gfdb_fop_type_t fop_type)
{
        return (fop_type & GFDB_FOP_DENTRY_CREATE_OP) ?
                        _gf_true : _gf_false;
}

/*The structure that is used to send insert/update the databases
 * using insert_db api*/
typedef struct gfdb_db_record {
        /* GFID */
        uuid_t                          gfid;
        /* Used during a rename refer ctr_rename() in changetimerecorder
         * xlator*/
        uuid_t                          old_gfid;
        /* Parent GFID */
        uuid_t                          pargfid;
        uuid_t                          old_pargfid;
        /* File names */
        char                            file_name[GF_NAME_MAX + 1];
        char                            old_file_name[GF_NAME_MAX + 1];
        /* FOP type and FOP path*/
        gfdb_fop_type_t                 gfdb_fop_type;
        gfdb_fop_path_t                 gfdb_fop_path;
        /*Time of change or access*/
        gfdb_time_t                     gfdb_wind_change_time;
        gfdb_time_t                     gfdb_unwind_change_time;
        /* For crash consistancy while inserting/updating hard links */
        gf_boolean_t                    islinkupdate;
        /* For link consistency we do a double update i.e mark the link
         * during the wind and during the unwind we update/delete the link.
         * This has a performance hit. We give a choice here whether we need
         * link consistency to be spoton or not using link_consistency flag.
         * This will have only one link update */
         gf_boolean_t                   link_consistency;
        /* For dentry fops we can choose to ignore recording of unwind time */
        /* For inode fops "record_exit" volume option does the trick,       */
        /* but for dentry fops we update the LINK_UPDATE, so an extra       */
        /* flag is provided to ignore the recording of the unwind time.     */
        gf_boolean_t                    do_record_uwind_time;
        /* Global flag to record or not record counters */
        gf_boolean_t                    do_record_counters;
        /* Global flag to Record/Not Record wind or wind time.
         * This flag will overrule do_record_uwind_time*/
        gf_boolean_t                    do_record_times;
        /* Ignoring errors while inserting.
         * */
        gf_boolean_t                    ignore_errors;
} gfdb_db_record_t;


/*******************************************************************************
 *
 *                           Signatures for the plugin functions
 *                           i.e Any plugin should implementment
 *                           these functions to integrate with
 *                           libgfdb.
 *
 * ****************************************************************************/

/*Call back function for querying the database*/
typedef int
(*gf_query_callback_t)(gfdb_query_record_t *, void *);

/* Used to initialize db connection
 * Arguments:
 *      args         : Dictionary containing database specific parameters
 *      db_conn      : pointer to plugin specific data base connection
 *                     that will be created. If the call is successful
 *                     db_conn will contain the plugin specific connection
 *                     If call is unsuccessful will have NULL.
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_init_db_t)(dict_t *args, void **db_conn);




/* Used to terminate/de-initialize db connection
 *                      (Destructor function for db connection object)
 * Arguments:
 *      db_conn        : plugin specific data base connection
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_fini_db_t)(void **db_conn);




/*Used to insert/updated records in the database
 * Arguments:
 *      db_conn        : plugin specific data base connection
 *      gfdb_db_record :  Record to be inserted/updated
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_insert_record_t)(void *db_conn,
                        gfdb_db_record_t *db_record);




/*Used to delete record from the database
 * Arguments:
 *      db_conn        : plugin specific data base connection
 *      gfdb_db_record :  Record to be deleted
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_delete_record_t)(void *db_conn,
                        gfdb_db_record_t *db_record);




/*Used to compact the database
 * Arguments:
 *      db_conn                        :  GFDB Connection node
 *      compact_active                 :  Is compaction currently on?
 *      compact_mode_switched          :  Was the compaction switch flipped?
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_compact_db_t)(void *db_conn, gf_boolean_t compact_active,
                     gf_boolean_t compact_mode_switched);




/* Query all the records from the database
 * Arguments:
 *      db_conn        : plugin specific data base connection
 *      query_callback  : Call back function that will be called
 *                        for every record found
 *      _query_cbk_args : Custom argument passed for the call back
 *                        function query_callback
 *      query_limit     : 0 - list all files
 *                        positive value - add the LIMIT clause to
 *                        the SQL query to limit the number of records
 *                        returned
 *
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_find_all_t)(void *db_conn,
                               gf_query_callback_t query_callback,
                               void *_cbk_args,
                               int query_limit);




/* Query records/files that have not changed/accessed
 *                      from a time in past to current time
 * Arguments:
 *      db_conn                 : plugin specific data base connection
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _cbk_args               : Custom argument passed for the call back
 *                                function query_callback
 *      for_time                : Time from where the file/s are not
 *                                changed/accessed
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_find_unchanged_for_time_t)(void *db_conn,
                                  gf_query_callback_t query_callback,
                                  void *_cbk_args,
                                  gfdb_time_t *_time);



/* Query records/files that have changed/accessed from a
 *                      time in past to current time
 * Arguments:
 *      db_conn                 : plugin specific data base connection
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _cbk_args               : Custom argument passed for the call back
 *                                function query_callback
 *      _time                   : Time from where the file/s are
 *                                changed/accessed
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_find_recently_changed_files_t)(void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_cbk_args, gfdb_time_t *_time);

/* Query records/files that have not changed/accessed
 * from a time in past to current time, with
 * a desired frequency
 *
 * Arguments:
 *      db_conn                 : plugin specific data base connection
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _cbk_args               : Custom argument passed for the call back
 *                                function query_callback
 *      _time                   : Time from where the file/s are not
 *                                changed/accessed
 *      _write_freq             : Desired Write Frequency lower limit
 *      _read_freq              : Desired Read Frequency lower limit
 *      _clear_counters         : If true, Clears all the frequency counters of
 *                                all files.
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_find_unchanged_for_time_freq_t)
                                        (void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_cbk_args, gfdb_time_t *_time,
                                        int _write_freq, int _read_freq,
                                        gf_boolean_t _clear_counters);




/* Query records/files that have changed/accessed from a
 * time in past to current time, with a desired frequency
 * Arguments:
 *      db_conn                 : plugin specific data base connection
 *      query_callback          : Call back function that will be called
 *                                for every record found
 *      _cbk_args               : Custom argument passed for the call back
 *                                function query_callback
 *      _time                   : Time from where the file/s are
 *                                changed/accessed
 *      _write_freq             : Desired Write Frequency lower limit
 *      _read_freq              : Desired Read Frequency lower limit
 *      _clear_counters         : If true, Clears all the frequency counters of
 *                                all files.
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_find_recently_changed_files_freq_t)(void *db_conn,
                                        gf_query_callback_t query_callback,
                                        void *_cbk_args, gfdb_time_t *_time,
                                        int _write_freq, int _read_freq,
                                        gf_boolean_t _clear_counters);


typedef int (*gfdb_clear_files_heat_t)(void *db_conn);

typedef int (*gfdb_get_db_version_t)(void *db_conn,
                                        char **version);

typedef int (*gfdb_get_db_params_t)(void *db_conn,
                                char *param_key,
                                char **param_value);

typedef int (*gfdb_set_db_params_t)(void *db_conn,
                                char *param_key,
                                char *param_value);



/*Data structure holding all the above plugin function pointers*/
typedef struct gfdb_db_operations {
        gfdb_init_db_t                        init_db_op;
        gfdb_fini_db_t                        fini_db_op;
        gfdb_insert_record_t                  insert_record_op;
        gfdb_delete_record_t                  delete_record_op;
        gfdb_compact_db_t                     compact_db_op;
        gfdb_find_all_t                       find_all_op;
        gfdb_find_unchanged_for_time_t        find_unchanged_for_time_op;
        gfdb_find_recently_changed_files_t    find_recently_changed_files_op;
        gfdb_find_unchanged_for_time_freq_t
                                        find_unchanged_for_time_freq_op;
        gfdb_find_recently_changed_files_freq_t
                                        find_recently_changed_files_freq_op;
        gfdb_clear_files_heat_t clear_files_heat_op;
        gfdb_get_db_version_t           get_db_version;
        gfdb_get_db_params_t           get_db_params;
        gfdb_set_db_params_t           set_db_params;
} gfdb_db_operations_t;

/*******************************************************************************
 *
 * Database connection object: This objected is maitained by libgfdb for each
 * database connection created.
 * gf_db_connection     : DB connection specific to the plugin
 * gfdb_db_operations   : Contains all the libgfdb API implementation
 *                        from the plugin.
 * gfdb_db_type         : Type of database
 *
 * ****************************************************************************/


typedef struct gfdb_connection {
        void *gf_db_connection;
        gfdb_db_operations_t gfdb_db_operations;
        gfdb_db_type_t gfdb_db_type;
} gfdb_connection_t;




/*******************************************************************************
 *
 *                    Macros for get and set db options
 *
 * ****************************************************************************/


/*Set param_key : str_value into param_dict*/
#define SET_DB_PARAM_TO_DICT(comp_name, params_dict, param_key,\
                                                        str_value, ret, error)\
        do {\
                data_t *data     = NULL;\
                data            = str_to_data (str_value);\
                if (!data)\
                        goto error;\
                ret = dict_add (params_dict, param_key, data);\
                if (ret) {\
                        gf_msg (comp_name, GF_LOG_ERROR, 0,\
                                LG_MSG_SET_PARAM_FAILED, "Failed setting %s "\
                                "to params dictionary", param_key);\
                        data_destroy (data);\
                        goto error;\
                };\
        } while (0)

/*get str_value of param_key from param_dict*/
#define GET_DB_PARAM_FROM_DICT(comp_name, params_dict, param_key, str_value,\
                                error)\
        do {\
                data_t *data    = NULL;\
                data = dict_get (params_dict, param_key);\
                if (!data) {\
                        gf_msg (comp_name, GF_LOG_ERROR, 0,\
                                LG_MSG_GET_PARAM_FAILED, "Failed to retrieve "\
                                "%s from params", param_key);\
                        goto error;\
                } else {\
                        str_value = data->data;\
                };\
        } while (0)


/*get str_value of param_key from param_dict. if param_key is not present
 * set _default_v to str_value */
#define GET_DB_PARAM_FROM_DICT_DEFAULT(comp_name, params_dict, param_key,\
                                str_value, _default_v)\
        do {\
                data_t *data    = NULL;\
                data = dict_get (params_dict, param_key);\
                if (!data) {\
                        str_value = _default_v;\
                        gf_msg (comp_name, GF_LOG_TRACE, 0,\
                                LG_MSG_GET_PARAM_FAILED, "Failed to retrieve "\
                                "%s from params.Assigning default value: %s",\
                                param_key, _default_v);\
                } else {\
                        str_value = data->data;\
                };\
        } while (0)


#endif
