/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GFDB_DATA_STORE_HELPER_H
#define __GFDB_DATA_STORE_HELPER_H

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <fcntl.h>

#include "common-utils.h"
#include "compat-uuid.h"
#include "gfdb_mem-types.h"
#include "dict.h"
#include "byte-order.h"
#include "libglusterfs-messages.h"


#define GFDB_DATA_STORE               "gfdbdatastore"

/*******************************************************************************
 *
 *                     Query related data structure and functions
 *
 * ****************************************************************************/

#ifdef NAME_MAX
#define GF_NAME_MAX NAME_MAX
#else
#define GF_NAME_MAX 255
#endif

/*Structure to hold the link information*/
typedef struct gfdb_link_info {
        uuid_t                          pargfid;
        char                            file_name[GF_NAME_MAX];
        struct list_head                list;
} gfdb_link_info_t;


/*Structure used for querying purpose*/
typedef struct gfdb_query_record {
        uuid_t                          gfid;
        /*This is the hardlink list*/
        struct list_head                link_list;
        int                             link_count;
} gfdb_query_record_t;

/*Create a single link info structure*/
gfdb_link_info_t *gfdb_link_info_new ();
typedef gfdb_link_info_t *(*gfdb_link_info_new_t) ();

/*Destroy a link info structure*/
void
gfdb_link_info_free (gfdb_link_info_t *gfdb_link_info);
typedef void
(*gfdb_link_info_free_t) (gfdb_link_info_t *gfdb_link_info);

/* Function to create the query_record */
gfdb_query_record_t *
gfdb_query_record_new();
typedef gfdb_query_record_t *
(*gfdb_query_record_new_t)();




/* Fuction to add linkinfo to query record */
int
gfdb_add_link_to_query_record (gfdb_query_record_t      *gfdb_query_record,
                           uuid_t                   pgfid,
                           char               *base_name);
typedef int
(*gfdb_add_link_to_query_record_t) (gfdb_query_record_t *, uuid_t, char *);




/*Function to destroy query record*/
void
gfdb_query_record_free (gfdb_query_record_t *gfdb_query_record);
typedef void
(*gfdb_query_record_free_t) (gfdb_query_record_t *);






/* Function to write query record to file */
int
gfdb_write_query_record (int fd,
                        gfdb_query_record_t *gfdb_query_record);
typedef int
(*gfdb_write_query_record_t) (int, gfdb_query_record_t *);





/* Function to read query record from file.
 * Allocates memory to query record and return 0 when successful
 * Return -1 when failed.
 * Return 0 when EOF.
 * */
int
gfdb_read_query_record (int fd,
                        gfdb_query_record_t **gfdb_query_record);
typedef int
(*gfdb_read_query_record_t) (int, gfdb_query_record_t **);


#endif