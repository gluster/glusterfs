
#include "gfdb_data_store_helper.h"
#include "syscall.h"

/******************************************************************************
 *
 *                       Query record related functions
 *
 * ****************************************************************************/

/*Create a single link info structure*/
gfdb_link_info_t*
gfdb_link_info_new ()
{
        gfdb_link_info_t *link_info = NULL;

        link_info = GF_CALLOC (1, sizeof(gfdb_link_info_t),
                                        gf_mt_gfdb_link_info_t);
        if (!link_info) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, ENOMEM,
                        LG_MSG_NO_MEMORY, "Memory allocation failed for "
                        "link_info ");
                goto out;
        }

        INIT_LIST_HEAD (&link_info->list);

out:

        return link_info;
}

/*Destroy a link info structure*/
void
gfdb_link_info_free(gfdb_link_info_t *link_info)
{
        GF_FREE (link_info);
}


/*Function to create the query_record*/
gfdb_query_record_t *
gfdb_query_record_new()
{
        int ret = -1;
        gfdb_query_record_t *query_record = NULL;

        query_record = GF_CALLOC (1, sizeof(gfdb_query_record_t),
                                        gf_mt_gfdb_query_record_t);
        if (!query_record) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, ENOMEM,
                        LG_MSG_NO_MEMORY, "Memory allocation failed for "
                        "query_record ");
                goto out;
        }

        INIT_LIST_HEAD (&query_record->link_list);

        ret = 0;
out:
        if (ret == -1) {
                GF_FREE (query_record);
        }
        return query_record;
}


/*Function to delete a single linkinfo from list*/
static void
gfdb_delete_linkinfo_from_list (gfdb_link_info_t **link_info)
{
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, link_info, out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, *link_info, out);

        /*Remove hard link from list*/
        list_del(&(*link_info)->list);
        gfdb_link_info_free (*link_info);
        link_info = NULL;
out:
        return;
}


/*Function to destroy link_info list*/
void
gfdb_free_link_info_list (gfdb_query_record_t *query_record)
{
        gfdb_link_info_t        *link_info = NULL;
        gfdb_link_info_t        *temp = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);

        list_for_each_entry_safe(link_info, temp,
                        &query_record->link_list, list)
        {
                gfdb_delete_linkinfo_from_list (&link_info);
                link_info = NULL;
        }

out:
        return;
}



/* Function to add linkinfo to the query record */
int
gfdb_add_link_to_query_record (gfdb_query_record_t      *query_record,
                           uuid_t                   pgfid,
                           char               *base_name)
{
        int ret                                 = -1;
        gfdb_link_info_t *link_info             = NULL;
        int base_name_len                       = 0;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, pgfid, out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, base_name, out);

        link_info = gfdb_link_info_new ();
        if (!link_info) {
                goto out;
        }

        gf_uuid_copy (link_info->pargfid, pgfid);
        base_name_len = strlen (base_name);
        memcpy (link_info->file_name, base_name, base_name_len);
        link_info->file_name[base_name_len] = '\0';

        list_add_tail (&link_info->list,
                        &query_record->link_list);

        query_record->link_count++;

        ret = 0;
out:
        if (ret) {
                gfdb_link_info_free (link_info);
                link_info = NULL;
        }
        return ret;
}



/*Function to destroy query record*/
void
gfdb_query_record_free(gfdb_query_record_t *query_record)
{
        if (query_record) {
                gfdb_free_link_info_list (query_record);
                GF_FREE (query_record);
        }
}


/******************************************************************************
                SERIALIZATION/DE-SERIALIZATION OF QUERY RECORD
*******************************************************************************/
/******************************************************************************
 The on disk format of query record is as follows,

+---------------------------------------------------------------------------+
| Length of serialized query record |       Serialized Query Record         |
+---------------------------------------------------------------------------+
             4 bytes                     Length of serialized query record
                                                      |
                                                      |
     -------------------------------------------------|
     |
     |
     V
   Serialized Query Record Format:
   +---------------------------------------------------------------------------+
   | GFID |  Link count   |  <LINK INFO>  |.....                      | FOOTER |
   +---------------------------------------------------------------------------+
     16 B        4 B         Link Length                                  4 B
                                |                                          |
                                |                                          |
   -----------------------------|                                          |
   |                                                                       |
   |                                                                       |
   V                                                                       |
   Each <Link Info> will be serialized as                                  |
   +-----------------------------------------------+                       |
   | PGID | BASE_NAME_LENGTH |      BASE_NAME      |                       |
   +-----------------------------------------------+                       |
     16 B       4 B             BASE_NAME_LENGTH                           |
                                                                           |
                                                                           |
   ------------------------------------------------------------------------|
   |
   |
   V
   FOOTER is a magic number 0xBAADF00D indicating the end of the record.
   This also serves as a serialized schema validator.
 * ****************************************************************************/

#define GFDB_QUERY_RECORD_FOOTER 0xBAADF00D
#define UUID_LEN                 16

/*Function to get the potential length of the serialized buffer*/
static int32_t
gfdb_query_record_serialized_length (gfdb_query_record_t *query_record)
{
        int32_t len                         = -1;
        gfdb_link_info_t *link_info     = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);

        /* Length of GFID */
        len = UUID_LEN;

        /* length of number of links*/
        len += sizeof (int32_t);

        list_for_each_entry (link_info, &query_record->link_list, list) {

                /* length of PFID */
                len += UUID_LEN;

                /* Add size of base name length*/
                len += sizeof (int32_t);

                /* Length of base_name */
                len += strlen (link_info->file_name);

        }

        /* length of footer */
        len += sizeof (int32_t);
out:
        return len;
}

/* Function for serializing query record.
 *
 * Query Record Serialization Format
 * +---------------------------------------------------------------------------+
 * | GFID |  Link count   |  <LINK INFO>  |.....                      | FOOTER |
 * +---------------------------------------------------------------------------+
 *   16 B        4 B         Link Length                                  4 B
 *
 *
 * Each <Link Info> will be serialized as
 * +-----------------------------------------------+
 * | PGID | BASE_NAME_LENGTH |      BASE_NAME      |
 * +-----------------------------------------------+
 *   16 B       4 B             BASE_NAME_LENGTH
 *
 *
 * FOOTER is a magic number 0xBAADF00D indicating the end of the record.
 * This also serves as a serialized schema validator.
 *
 * The function will allocate memory to the serialized buffer,
 * the caller needs to free it.
 * Returns the length of the serialized buffer on success
 * or -1 on failure.
 *
 * */
static int
gfdb_query_record_serialize (gfdb_query_record_t *query_record,
                             char **in_buffer)
{
        gfdb_link_info_t *link_info     = NULL;
        int              count          = -1;
        int              base_name_len  = 0;
        int              buffer_length  = 0;
        int              footer         = GFDB_QUERY_RECORD_FOOTER;
        char             *buffer        = NULL;
        char             *ret_buffer    = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE,
                             (query_record->link_count > 0), out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, in_buffer, out);


        /* Calculate the total length of the serialized buffer */
        buffer_length = gfdb_query_record_serialized_length (query_record);
        if (buffer_length <= 0) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Failed to calculate the length of "
                        "serialized buffer");
                goto out;
        }

        /* Allocate memory to the serialized buffer */
        ret_buffer = GF_CALLOC (1, buffer_length,  gf_common_mt_char);
        if (!ret_buffer) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Memory allocation failed for "
                        "serialized buffer.");
                goto out;
        }

        buffer = ret_buffer;

        count = 0;

        /* Copying the GFID */
        memcpy (buffer, query_record->gfid, UUID_LEN);
        buffer += UUID_LEN;
        count += UUID_LEN;

        /* Copying the number of links */
        memcpy (buffer, &query_record->link_count, sizeof (int32_t));
        buffer += sizeof (int32_t);
        count += sizeof (int32_t);

        list_for_each_entry (link_info, &query_record->link_list, list) {

                /* Copying the PFID */
                memcpy(buffer, link_info->pargfid, UUID_LEN);
                buffer += UUID_LEN;
                count += UUID_LEN;

                /* Copying base name length*/
                base_name_len = strlen (link_info->file_name);
                memcpy (buffer, &base_name_len, sizeof (int32_t));
                buffer += sizeof (int32_t);
                count += sizeof (int32_t);

                /* Length of base_name */
                memcpy(buffer, link_info->file_name, base_name_len);
                buffer += base_name_len;
                count += base_name_len;

        }

        /* Copying the Footer of the record */
        memcpy (buffer, &footer, sizeof (int32_t));
        buffer += sizeof (int32_t);
        count += sizeof (int32_t);

out:
        if (count < 0) {
                GF_FREE (ret_buffer);
                ret_buffer = NULL;
        }
        *in_buffer = ret_buffer;
        return count;
}

static gf_boolean_t
is_serialized_buffer_valid (char *in_buffer, int buffer_length) {
        gf_boolean_t    ret        = _gf_false;
        int             footer     = 0;

        /* Read the footer */
        in_buffer += (buffer_length - sizeof (int32_t));
        memcpy (&footer, in_buffer, sizeof (int32_t));

        /*
         * if the footer is not GFDB_QUERY_RECORD_FOOTER
         * then the serialized record is invalid
         *
         * */
        if (footer != GFDB_QUERY_RECORD_FOOTER) {
                goto out;
        }

        ret = _gf_true;
out:
        return ret;
}


static int
gfdb_query_record_deserialize (char *in_buffer,
                               int buffer_length,
                               gfdb_query_record_t **query_record)
{
        int ret                                 = -1;
        char *buffer                            = NULL;
        int i                                   = 0;
        gfdb_link_info_t *link_info             = NULL;
        int count                               = 0;
        int base_name_len                       = 0;
        gfdb_query_record_t *ret_qrecord        = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, in_buffer, out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, (buffer_length > 0), out);

        if (!is_serialized_buffer_valid (in_buffer, buffer_length)) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Invalid serialized query record");
                goto out;
        }

        buffer = in_buffer;

        ret_qrecord = gfdb_query_record_new ();
        if (!ret_qrecord) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Failed to allocate space to "
                        "gfdb_query_record_t");
                goto out;
        }

        /* READ GFID */
        memcpy ((ret_qrecord)->gfid, buffer, UUID_LEN);
        buffer += UUID_LEN;
        count += UUID_LEN;

        /* Read the number of link */
        memcpy (&(ret_qrecord->link_count), buffer, sizeof (int32_t));
        buffer += sizeof (int32_t);
        count += sizeof (int32_t);

        /* Read all the links */
        for (i = 0; i < ret_qrecord->link_count; i++) {
                if (count >= buffer_length) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_DB_ERROR, "Invalid serialized "
                                "query record");
                        ret = -1;
                        goto out;
                }

                link_info = gfdb_link_info_new ();
                if (!link_info) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_DB_ERROR, "Failed to create link_info");
                        goto out;
                }

                /* READ PGFID */
                memcpy (link_info->pargfid, buffer, UUID_LEN);
                buffer += UUID_LEN;
                count += UUID_LEN;

                /* Read base name length */
                memcpy (&base_name_len, buffer, sizeof (int32_t));
                buffer += sizeof (int32_t);
                count += sizeof (int32_t);

                /* READ basename */
                memcpy (link_info->file_name, buffer, base_name_len);
                buffer += base_name_len;
                count += base_name_len;
                link_info->file_name[base_name_len] = '\0';

                /* Add link_info to the list */
                list_add_tail (&link_info->list,
                               &(ret_qrecord->link_list));

                /* Reseting link_info */
                link_info = NULL;
        }

        ret = 0;
out:
        if (ret) {
                gfdb_query_record_free (ret_qrecord);
                ret_qrecord = NULL;
        }
        *query_record = ret_qrecord;
        return ret;
}





/* Function to write query record to file
 *
 * Disk format
 * +---------------------------------------------------------------------------+
 * | Length of serialized query record |       Serialized Query Record         |
 * +---------------------------------------------------------------------------+
 *            4 bytes                     Length of serialized query record
 *
 * Please refer gfdb_query_record_serialize () for format of
 * Serialized Query Record
 *
 * */
int
gfdb_write_query_record (int fd,
                        gfdb_query_record_t *query_record)
{
        int ret                 = -1;
        int buffer_len          = 0;
        char *buffer            = NULL;
        int write_len           = 0;
        char *write_buffer      = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, (fd >= 0), out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);

        buffer_len = gfdb_query_record_serialize (query_record, &buffer);
        if (buffer_len < 0) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Failed to serialize query record");
                goto out;
        }

        /* Serialize the buffer length and write to file */
        ret = write (fd, &buffer_len, sizeof (int32_t));
        if (ret < 0) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_DB_ERROR, "Failed to write buffer length"
                                " to file");
                goto out;
        }

        /* Write the serialized query record to file */
        write_len = buffer_len;
        write_buffer = buffer;
        while ((ret = write (fd, write_buffer, write_len)) <  write_len) {
                if (ret < 0) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, errno,
                                LG_MSG_DB_ERROR, "Failed to write serialized "
                                "query record to file");
                        goto out;
                }

                write_buffer += ret;
                write_len -= ret;
        }

        ret = 0;
out:
        GF_FREE (buffer);
        return ret;
}



/* Function to read query record from file.
 * Allocates memory to query record and
 * returns length of serialized query record when successful
 * Return -1 when failed.
 * Return 0 when reached EOF.
 * */
int
gfdb_read_query_record (int fd,
                        gfdb_query_record_t **query_record)
{
        int ret                 = -1;
        int buffer_len          = 0;
        int read_len            = 0;
        char *buffer            = NULL;
        char *read_buffer       = NULL;

        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, (fd >= 0), out);
        GF_VALIDATE_OR_GOTO (GFDB_DATA_STORE, query_record, out);


        /* Read serialized query record length from the file*/
        ret = sys_read (fd, &buffer_len, sizeof (int32_t));
        if (ret < 0) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_DB_ERROR, "Failed reading buffer length"
                                " from file");
                goto out;
        }
        /* EOF */
        else if (ret == 0) {
                ret = 0;
                goto out;
        }

        /* Allocating memory to the serialization buffer */
        buffer = GF_CALLOC (1, buffer_len,  gf_common_mt_char);
        if (!buffer) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Failed to allocate space to "
                        "serialized buffer");
                goto out;
        }


        /* Read the serialized query record from file */
        read_len = buffer_len;
        read_buffer = buffer;
        while ((ret = sys_read (fd, read_buffer, read_len)) < read_len) {

                /*Any error */
                if (ret < 0) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, errno,
                                LG_MSG_DB_ERROR, "Failed to read serialized "
                                "query record from file");
                        goto out;
                }
                /* EOF */
                else if (ret == 0) {
                        gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                                LG_MSG_DB_ERROR, "Invalid query record or "
                                "corrupted query file");
                        ret = -1;
                        goto out;
                }

                read_buffer += ret;
                read_len -= ret;
        }

        ret = gfdb_query_record_deserialize (buffer, buffer_len,
                                             query_record);
        if (ret) {
                gf_msg (GFDB_DATA_STORE, GF_LOG_ERROR, 0,
                        LG_MSG_DB_ERROR, "Failed to de-serialize query record");
                goto out;
        }

        ret = buffer_len;
out:
        GF_FREE (buffer);
        return ret;
}
