/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "gfdb_sqlite3.h"
#include "gfdb_data_store.h"
#include "list.h"
#include "libglusterfs-messages.h"

/******************************************************************************
 *
 *                     Database Connection utils/internals
 *
 * ****************************************************************************/

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
struct gfdb_conn_node_t {
        gfdb_connection_t       gfdb_connection;
        struct list_head        conn_list;
};


/*
 * db_conn_list is the circular linked list which
 * will have all the database connections for the process
 *
 * */
static gfdb_conn_node_t *db_conn_list;

/*
 * db_conn_mutex is the mutex for db_conn_list
 *
 * */
static pthread_mutex_t db_conn_mutex = PTHREAD_MUTEX_INITIALIZER;


/*Checks the sanity of the connection node*/
#define CHECK_CONN_NODE(_conn_node)\
do {\
        GF_ASSERT (_conn_node);\
        GF_ASSERT (_conn_node->gfdb_connection.gf_db_connection);\
} while (0)

/* Checks the sanity of the connection node and goto */
#define CHECK_CONN_NODE_GOTO(_conn_node, label)\
do {\
        if (!_conn_node) {\
                goto label;\
        };\
        if (!_conn_node->gfdb_connection.gf_db_connection) {\
                goto label;\
        };\
} while (0)

/*Check if the conn node is first in the list*/
#define IS_FIRST_NODE(db_conn_list, _conn_node)\
        ((_conn_node == db_conn_list) ? _gf_true : _gf_false)


/*Check if the conn node is the only node in the list*/
#define IS_THE_ONLY_NODE(_conn_node)\
((_conn_node->conn_list.next == _conn_node->conn_list.prev)\
        ? _gf_true : _gf_false)



/*Internal Function: Adds connection node to the end of
 * the db connection list.*/
static int
add_connection_node (gfdb_conn_node_t *_conn_node) {
        int ret = -1;

        GF_ASSERT (_conn_node);

        /*Lock the list*/
        ret = pthread_mutex_lock (&db_conn_mutex);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, ret,
                        LG_MSG_LOCK_LIST_FAILED, "Failed lock db connection "
                        "list %s", strerror(ret));
                ret = -1;
                goto out;
        }

        if (db_conn_list == NULL) {
                db_conn_list = _conn_node;
        } else {
                list_add_tail (&_conn_node->conn_list,
                                &db_conn_list->conn_list);
        }

        /*unlock the list*/
        ret = pthread_mutex_unlock (&db_conn_mutex);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, ret,
                        LG_MSG_UNLOCK_LIST_FAILED, "Failed unlock db "
                        "connection list %s", strerror(ret));
                ret = -1;
                /*TODO What if the unlock fails.
                * Will it lead to deadlock?
                * Most of the gluster code
                * no check for unlock or destory of mutex!*/
        }
        ret = 0;
out:
        return ret;
}


/*Internal Function:
 * Delete connection node from the list*/
static int
delete_conn_node (gfdb_conn_node_t *_conn_node)
{
        int ret = -1;

        GF_ASSERT (_conn_node);

        /*Lock of the list*/
        ret = pthread_mutex_lock (&db_conn_mutex);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, ret,
                        LG_MSG_LOCK_LIST_FAILED, "Failed lock on db connection"
                        " list %s", strerror(ret));
                goto out;
        }

        /*Remove the connection object from list*/
        if (IS_THE_ONLY_NODE(_conn_node)) {
                db_conn_list = NULL;
                GF_FREE (_conn_node);
        } else {
                if (IS_FIRST_NODE(db_conn_list, _conn_node)) {
                        db_conn_list = list_entry (db_conn_list->conn_list.next,
                                                gfdb_conn_node_t, conn_list);
                }
                list_del(&_conn_node->conn_list);
                GF_FREE (_conn_node);
        }

        /*Release the list lock*/
        ret =  pthread_mutex_unlock (&db_conn_mutex);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_WARNING, ret,
                        LG_MSG_UNLOCK_LIST_FAILED, "Failed unlock on db "
                        "connection list %s", strerror(ret));
                /*TODO What if the unlock fails.
                * Will it lead to deadlock?
                * Most of the gluster code
                * no check for unlock or destory of mutex!*/
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;
}


/*Internal function: Used initialize/map db operation of
 * specified type of db plugin*/
static int
init_db_operations (gfdb_db_type_t       gfdb_db_type,
                    gfdb_db_operations_t *gfdb_db_operations)
{

        int ret = -1;

        GF_ASSERT (gfdb_db_operations);

        /*Clear the gfdb_db_operations*/
        gfdb_db_operations = memset(gfdb_db_operations, 0,
                                        sizeof(*gfdb_db_operations));
        switch (gfdb_db_type) {
        case GFDB_SQLITE3:
                gf_sqlite3_fill_db_operations (gfdb_db_operations);
                ret = 0;
                break;
        case GFDB_HYPERDEX:
        case GFDB_HASH_FILE_STORE:
        case GFDB_ROCKS_DB:
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_UNSUPPORTED_PLUGIN, "Plugin not supported");
                break;
        case GFDB_INVALID_DB:
        case GFDB_DB_END:
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_INVALID_DB_TYPE, "Invalid DB Type");
                break;
        }
        return ret;
}


/******************************************************************************
 *
 *                      LIBGFDB API Functions
 *
 * ****************************************************************************/


/*Libgfdb API Function: Used to initialize a db connection
 *                      (Constructor function for db connection object)
 * Arguments:
 *      args         :  Dictionary containing database specific parameters
 *                      eg: For sqlite3, pagesize, cachesize, db name, db path
                        etc
 *      gfdb_db_type :  Type of data base used i.e sqlite or hyperdex etc
 * Returns : if successful return the GFDB Connection node to the caller or
 *          NULL in case of failure*/
gfdb_conn_node_t *
init_db (dict_t *args, gfdb_db_type_t gfdb_db_type)
{
        int ret                                 = -1;
        gfdb_conn_node_t *_conn_node            = NULL;
        gfdb_db_operations_t *db_operations_t   = NULL;

        /*Create data base connection object*/
        _conn_node = GF_CALLOC (1, sizeof(gfdb_conn_node_t),
                                        gf_mt_db_conn_node_t);
        if (!_conn_node) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, ENOMEM,
                        LG_MSG_NO_MEMORY, "Failed mem alloc for "
                        "gfdb_conn_node_t");
                goto alloc_failed;
        }

        /*Init the list component of db conneciton object*/
        INIT_LIST_HEAD (&_conn_node->conn_list);


        /*Add created connection node to the list*/
        ret = add_connection_node (_conn_node);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_ADD_TO_LIST_FAILED, "Failed to add connection "
                        "node to list");
                goto _conn_failed;
        }

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;

        /*init the db ops object of db connection object*/
        ret = init_db_operations(gfdb_db_type, db_operations_t);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_INIT_DB_FAILED, "Failed initializing database "
                        "operation failed.");
                ret = -1;
                goto init_db_failed;
        }

        /*Calling the init_db_op of the respected db type*/
        GF_ASSERT (db_operations_t->init_db_op);
        ret = db_operations_t->init_db_op (args, &_conn_node->gfdb_connection.
                                           gf_db_connection);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_INIT_DB_FAILED, "Failed initializing database");
                ret = -1;
                goto init_db_failed;
        }
        _conn_node->gfdb_connection.gfdb_db_type = gfdb_db_type;
        ret = 0;

        return _conn_node;

        /*****Error Handling********/
        /* If init_db_operations or init_db of plugin failed delete
        * conn node from the list.
        * connection node will be free by delete_conn_node*/
init_db_failed:
        ret = delete_conn_node (_conn_node);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DELETE_FROM_LIST_FAILED, "Failed deleting "
                        "connection node from list");
        }
        return NULL;
        /*if adding to the list failed free connection node*/
_conn_failed:
        GF_FREE (_conn_node);
        /*if allocation failed*/
alloc_failed:
        return NULL;
        /*****Error Handling********/
}





/*Libgfdb API Function: Used to terminate/de-initialize db connection
 *                      (Destructor function for db connection object)
 * Arguments:
 *      _conn_node  :  GFDB Connection node
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int
fini_db (gfdb_conn_node_t *_conn_node)
{
        int ret                                 = -1;
        gfdb_db_operations_t *db_operations_t   = NULL;

        CHECK_CONN_NODE_GOTO (_conn_node, empty);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;

        GF_ASSERT (db_operations_t->fini_db_op);

        ret = db_operations_t->fini_db_op(&_conn_node->gfdb_connection.
                                          gf_db_connection);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_CLOSE_CONNECTION_FAILED, "Failed close the db "
                        "connection");
                goto out;
        }

        ret = delete_conn_node (_conn_node);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DELETE_FROM_LIST_FAILED, "Failed deleting "
                        "connection node from list");
        }
empty:
        ret = 0;
out:
        return ret;
}






/*Libgfdb API Function: Used to insert/update records in the database
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
insert_record (gfdb_conn_node_t *_conn_node,
               gfdb_db_record_t *gfdb_db_record)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->insert_record_op) {

                ret = db_operations_t->insert_record_op (gf_db_connection,
                                                         gfdb_db_record);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, _gfdb_log_level (GF_LOG_ERROR,
                                gfdb_db_record->ignore_errors), 0,
                                LG_MSG_INSERT_OR_UPDATE_FAILED, "Insert/Update"
                                " operation failed");
                }
        }

        return ret;
}




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
delete_record (gfdb_conn_node_t *_conn_node,
               gfdb_db_record_t *gfdb_db_record)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->delete_record_op) {

                ret = db_operations_t->delete_record_op (gf_db_connection,
                                                         gfdb_db_record);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_DELETE_FAILED, "Delete operation "
                                "failed");
                }

        }

        return ret;
}

/*Libgfdb API Function: Compact the database.
 *
 * Arguments:
 *      _conn_node                      :  GFDB Connection node
 *      _compact_active                 :  Is compaction currently on?
 *      _compact_mode_switched          :  Was the compaction switch flipped?
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int
compact_db (gfdb_conn_node_t *_conn_node, gf_boolean_t _compact_active,
            gf_boolean_t _compact_mode_switched)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->compact_db_op) {

                ret = db_operations_t->compact_db_op (gf_db_connection,
                                                      _compact_active,
                                                      _compact_mode_switched);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_COMPACT_FAILED, "Compaction operation "
                                "failed");
                }

        }

        return ret;
}





/*Libgfdb API Function: Query all the records from the database
 * Arguments:
 *      _conn_node      : GFDB Connection node
 *      query_callback  : Call back function that will be called
 *                        for every record found
 *      _query_cbk_args : Custom argument passed for the call back
 *                        function query_callback
 *      query_limit     : number to limit number of rows returned by the query
 * Returns : if successful return 0 or
 *          -ve value in case of failure*/
int
find_all (gfdb_conn_node_t      *_conn_node,
          gf_query_callback_t   query_callback,
          void                  *_query_cbk_args,
          int                   query_limit)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->find_all_op) {
                ret = db_operations_t->find_all_op (gf_db_connection,
                                                    query_callback,
                                                    _query_cbk_args,
                                                    query_limit);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED, "Find all operation "
                                "failed");
                }

        }

        return ret;
}



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
int
find_unchanged_for_time(gfdb_conn_node_t        *_conn_node,
                        gf_query_callback_t     query_callback,
                        void                    *_query_cbk_args,
                        gfdb_time_t             *for_time)
{

        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->find_unchanged_for_time_op) {

                ret = db_operations_t->find_unchanged_for_time_op
                                (gf_db_connection, query_callback,
                                _query_cbk_args, for_time);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED, "Find unchanged "
                                "operation failed");
                }

        }

        return ret;
}

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
int
find_recently_changed_files(gfdb_conn_node_t    *_conn_node,
                            gf_query_callback_t query_callback,
                            void                *_query_cbk_args,
                            gfdb_time_t         *from_time)
{

        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->find_recently_changed_files_op) {

                ret =  db_operations_t->find_recently_changed_files_op (
                                gf_db_connection, query_callback,
                                _query_cbk_args, from_time);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED,
                                "Find changed operation failed");
                }

        }

        return ret;

}

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
int
find_unchanged_for_time_freq(gfdb_conn_node_t *_conn_node,
                                        gf_query_callback_t query_callback,
                                        void *_query_cbk_args,
                                        gfdb_time_t *for_time,
                                        int write_freq_thresold,
                                        int read_freq_thresold,
                                        gf_boolean_t _clear_counters)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->find_unchanged_for_time_freq_op) {

                ret = db_operations_t->find_unchanged_for_time_freq_op(
                                gf_db_connection, query_callback,
                                _query_cbk_args, for_time,
                                write_freq_thresold, read_freq_thresold,
                                _clear_counters);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED,
                                "Find unchanged with freq operation failed");
                }

        }

        return ret;
}

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
int
find_recently_changed_files_freq(gfdb_conn_node_t *_conn_node,
                                gf_query_callback_t query_callback,
                                void *_query_cbk_args,
                                gfdb_time_t *from_time,
                                int write_freq_thresold,
                                int read_freq_thresold,
                                gf_boolean_t _clear_counters)
{

        int ret                                 = 0;
        gfdb_db_operations_t *db_operations_t   = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(_conn_node);

        db_operations_t = &_conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = _conn_node->gfdb_connection.gf_db_connection;

        if (db_operations_t->find_recently_changed_files_freq_op) {

                ret =  db_operations_t->find_recently_changed_files_freq_op(
                                gf_db_connection, query_callback,
                                _query_cbk_args, from_time,
                                write_freq_thresold, read_freq_thresold,
                                _clear_counters);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED,
                                "Find changed with freq operation failed");
                }

        }

        return ret;

}



/*Libgfdb API Function: Clear the heat for all the files
 *
 *  Arguments:
 *    conn_node              : GFDB Connection node
 *
 * Returns : if successful return 0 or
 *           -ve value in case of failure
 **/

int
clear_files_heat (gfdb_conn_node_t *conn_node)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations     = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(conn_node);

        db_operations = &conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = conn_node->gfdb_connection.gf_db_connection;

        if (db_operations->clear_files_heat_op) {
                ret =  db_operations->clear_files_heat_op (gf_db_connection);
                if (ret) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_INSERT_OR_UPDATE_FAILED,
                                "Clear files heat operation failed");
                }
        }

        return ret;
}


/* Libgfdb API Function: Function to extract version of the db
 * Input:
 * gfdb_conn_node_t *conn_node        : GFDB Connection node
 * char **version  : the version is extracted as a string and will be stored in
 *                   this variable. The freeing of the memory should be done by
 *                   the caller.
 * Return:
 *      On success return the lenght of the version string that is
 *      extracted.
 *      On failure return -1
 * */
int
get_db_version (gfdb_conn_node_t *conn_node, char **version)
{
        int ret                                 = 0;
        gfdb_db_operations_t *db_operations     = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(conn_node);

        db_operations = &conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = conn_node->gfdb_connection.gf_db_connection;

        if (db_operations->get_db_version) {
                ret =  db_operations->get_db_version (gf_db_connection,
                                                      version);
                if (ret < 0) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED,
                                "Get version failed");
                }
        }

        return ret;
}

int
get_db_params (gfdb_conn_node_t *conn_node, char *param_key,
                char **param_value)
{
        int ret                                 = -1;
        gfdb_db_operations_t *db_operations     = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(conn_node);

        db_operations = &conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = conn_node->gfdb_connection.gf_db_connection;

        if (db_operations->get_db_params) {
                ret =  db_operations->get_db_params (gf_db_connection,
                                                     param_key,
                                                     param_value);
                if (ret < 0) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_FIND_OP_FAILED,
                                "Get setting failed");
                }
        }

        return ret;
}


int
set_db_params (gfdb_conn_node_t *conn_node, char *param_key,
                char *param_value)
{
        int ret                                 = -1;
        gfdb_db_operations_t *db_operations     = NULL;
        void *gf_db_connection                  = NULL;

        CHECK_CONN_NODE(conn_node);

        db_operations = &conn_node->gfdb_connection.gfdb_db_operations;
        gf_db_connection = conn_node->gfdb_connection.gf_db_connection;

        if (db_operations->set_db_params) {
                ret =  db_operations->set_db_params (gf_db_connection,
                                                     param_key,
                                                     param_value);
                if (ret < 0) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_INSERT_OR_UPDATE_FAILED,
                                "Failed to set database setting");
                }
        }

        return ret;
}




static const
char *get_db_path_key()
{
        return GFDB_SQL_PARAM_DBPATH;
}

void get_gfdb_methods (gfdb_methods_t *methods)
{
        methods->init_db = init_db;
        methods->fini_db = fini_db;
        methods->find_all = find_all;
        methods->find_unchanged_for_time = find_unchanged_for_time;
        methods->find_recently_changed_files = find_recently_changed_files;
        methods->find_unchanged_for_time_freq = find_unchanged_for_time_freq;
        methods->find_recently_changed_files_freq =
                                               find_recently_changed_files_freq;
        methods->clear_files_heat = clear_files_heat;
        methods->get_db_version = get_db_version;
        methods->get_db_params = get_db_params;
        methods->set_db_params = set_db_params;
        methods->get_db_path_key = get_db_path_key;

        /* Query Record related functions */
        methods->gfdb_query_record_new = gfdb_query_record_new;
        methods->gfdb_query_record_free = gfdb_query_record_free;
        methods->gfdb_add_link_to_query_record = gfdb_add_link_to_query_record;
        methods->gfdb_write_query_record = gfdb_write_query_record;
        methods->gfdb_read_query_record = gfdb_read_query_record;

        /* Link info related functions */
        methods->gfdb_link_info_new = gfdb_link_info_new;
        methods->gfdb_link_info_free = gfdb_link_info_free;

        /* Compaction related functions */
        methods->compact_db = compact_db;
}

