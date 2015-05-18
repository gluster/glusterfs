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


#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "common-utils.h"
#include "compat-uuid.h"
#include "gfdb_mem-types.h"
#include "dict.h"

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
        GFDB_RFC_UPDATE_DB_OP
} gf_db_operation_t;


#define GF_COL_MAX_NUM                  2
#define GF_COL_ALL                      " * "

/* Column/fields names used in the DB.
 * If any new field is added should be updated here*/
#define GF_COL_GF_ID                    "GF_ID"
#define GF_COL_GF_PID                   "GF_PID"
#define GF_COL_FILE_NAME                "FNAME"
#define GF_COL_FPATH                    "FPATH"
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
static inline long int
gfdb_time_2_usec(gfdb_time_t *gfdb_time)
{
        GF_ASSERT(gfdb_time);
        return gfdb_time->tv_sec * GFDB_MICROSEC + gfdb_time->tv_usec;
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
#define GFDB_DATA_STORE               "gfdbdatastore"
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
        uuid_t                          gfid;
        uuid_t                          pargfid;
        uuid_t                          old_pargfid;
        char                            file_name[PATH_MAX];
        char                            file_path[PATH_MAX];
        char                            old_file_name[PATH_MAX];
        char                            old_path[PATH_MAX];
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
} gfdb_db_record_t;



/*******************************************************************************
 *
 *                     Query related data structure and functions
 *
 * ****************************************************************************/



/*Structure used for querying purpose*/
typedef struct gfdb_query_record {
        /*Inode info*/
        uuid_t                          gfid;
        /*All the hard link of the inode
         * All the hard links will be queried as
         * "GF_PID,FNAME,FPATH,W_DEL_FLAG,LINK_UPDATE"
         * and multiple hardlinks will be seperated by "::"*/
        /*Do only shallow copy. The gf_query_callback_t */
        /* function should do the deep copy.*/
        char                            *_link_info_str;
        ssize_t                         link_info_size;
} gfdb_query_record_t;

/*Function to create the query_record*/
static inline gfdb_query_record_t *
gfdb_query_record_init()
{
        int ret = -1;
        gfdb_query_record_t *gfdb_query_record = NULL;

        gfdb_query_record = GF_CALLOC (1, sizeof(gfdb_query_record_t),
                                        gf_mt_gfdb_query_record_t);
        if (!gfdb_query_record) {
                gf_log (GFDB_DATA_STORE, GF_LOG_ERROR,
                        "Error allocating memory to gfdb_query_record ");
                goto out;
        }
        ret = 0;
out:
        if (ret == -1) {
                GF_FREE (gfdb_query_record);
        }
        return gfdb_query_record;
}

/*Function to destroy query record*/
static inline void
gfdb_query_record_fini(gfdb_query_record_t
                                                **gfdb_query_record) {
        GF_FREE (*gfdb_query_record);
}








/*Structure to hold the link information*/
typedef struct gfdb_link_info {
        uuid_t                          pargfid;
        char                            file_name[PATH_MAX];
        char                            file_path[PATH_MAX];
        gf_boolean_t                    is_link_updated;
        gf_boolean_t                    is_del_flag_set;
} gfdb_link_info_t;

/*Create a single link info structure*/
static inline gfdb_link_info_t *
gfdb_link_info_init ()
{
        gfdb_link_info_t *gfdb_link_info = NULL;

        gfdb_link_info = GF_CALLOC (1, sizeof(gfdb_link_info_t),
                                        gf_mt_gfdb_link_info_t);
        if (!gfdb_link_info) {
                gf_log (GFDB_DATA_STORE, GF_LOG_ERROR,
                        "Error allocating memory to gfdb_link_info ");
        }

        return gfdb_link_info;
}

/*Destroy a link info structure*/
static inline void
gfdb_link_info_fini(gfdb_link_info_t **gfdb_link_info)
{
        if (gfdb_link_info)
                GF_FREE (*gfdb_link_info);
}


/*Length of each hard link string */
#define DEFAULT_LINK_INFO_STR_LEN       1024

/* Parse a single link string into link_info structure
 * Input format of str_link
 *      "GF_PID,FNAME,FPATH,W_DEL_FLAG,LINK_UPDATE"
 *
 * */
static inline int
str_to_link_info (char *str_link,
                             gfdb_link_info_t *link_info)
{
        int     ret = -1;
        const char *delimiter = ",";
        char *token_str = NULL;
        char *saveptr = NULL;
        char gfid[200] = "";

        GF_ASSERT (str_link);
        GF_ASSERT (link_info);

        /*Parent GFID*/
        token_str = strtok_r(str_link, delimiter, &saveptr);
        if (token_str != NULL) {
                strcpy (gfid, token_str);
                ret = gf_uuid_parse (gfid, link_info->pargfid);
                if (ret == -1)
                        goto out;
        }

        /*Filename*/
        token_str = strtok_r(NULL, delimiter, &saveptr);
        if (token_str != NULL) {
                strcpy (link_info->file_name, token_str);
        }

        /*Filepath*/
        token_str = strtok_r(NULL, delimiter, &saveptr);
        if (token_str != NULL) {
                strcpy (link_info->file_path, token_str);
        }

        /*is_link_updated*/
        token_str = strtok_r(NULL, delimiter, &saveptr);
        if (token_str != NULL) {
                link_info->is_link_updated = atoi(token_str);
                if (link_info->is_link_updated != 0 &&
                                link_info->is_link_updated != 1) {
                        goto out;
                }
        }

        /*is_del_flag_set*/
        token_str = strtok_r(NULL, delimiter, &saveptr);
        if (token_str != NULL) {
                link_info->is_del_flag_set = atoi (token_str);
                if (link_info->is_del_flag_set != 0 &&
                                link_info->is_del_flag_set != 1) {
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}


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




/* Query all the records from the database
 * Arguments:
 *      db_conn        : plugin specific data base connection
 *      query_callback  : Call back function that will be called
 *                        for every record found
 *      _query_cbk_args : Custom argument passed for the call back
 *                        function query_callback
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
typedef int
(*gfdb_find_all_t)(void *db_conn,
                               gf_query_callback_t query_callback,
                               void *_cbk_args);




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




/*Data structure holding all the above plugin function pointers*/
typedef struct gfdb_db_operations {
        gfdb_init_db_t                        init_db_op;
        gfdb_fini_db_t                        fini_db_op;
        gfdb_insert_record_t                  insert_record_op;
        gfdb_delete_record_t                  delete_record_op;
        gfdb_find_all_t                       find_all_op;
        gfdb_find_unchanged_for_time_t        find_unchanged_for_time_op;
        gfdb_find_recently_changed_files_t    find_recently_changed_files_op;
        gfdb_find_unchanged_for_time_freq_t
                                        find_unchanged_for_time_freq_op;
        gfdb_find_recently_changed_files_freq_t
                                        find_recently_changed_files_freq_op;
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
                        gf_log (comp_name, GF_LOG_ERROR,\
                                "Failed setting %s to params dictionary",\
                                param_key);\
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
                        gf_log (comp_name, GF_LOG_ERROR,\
                                "Failed to retrieve %s from params", param_key);\
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
                        gf_log (comp_name, GF_LOG_WARNING,\
                                "Failed to retrieve %s from params."\
                                "Assigning default value: %s",\
                                param_key, _default_v);\
                } else {\
                        str_value = data->data;\
                };\
        } while (0)


#endif


