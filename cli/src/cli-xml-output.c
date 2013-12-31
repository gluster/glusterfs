/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <stdlib.h>
#include "cli.h"
#include "cli1-xdr.h"
#include "run.h"
#include "compat.h"
#include "syscall.h"


enum gf_task_types {
    GF_TASK_TYPE_REBALANCE,
    GF_TASK_TYPE_REMOVE_BRICK
};

/*
 * IMPORTANT NOTE:
 * All exported functions in this file which use libxml need use a
 * #if (HAVE_LIB_XML), #else, #endif
 * For eg,
 *      int exported_func () {
 *              #if (HAVE_LIB_XML)
 *                      <Stuff using libxml>
 *              #else
 *                      return 0;
 *              #endif
 *      }
 *
 *  All other functions, which are called internally within this file need to be
 *  within #if (HAVE_LIB_XML), #endif statements
 *  For eg,
 *      #if (HAVE_LIB_XML)
 *      int internal_func ()
 *      {
 *      }
 *      #endif
 *
 *  Following the above formate ensures that all xml related code is compliled
 *  only when libxml2 is present, and also keeps the rest of the codebase free
 *  of #if (HAVE_LIB_XML)
 */


#if (HAVE_LIB_XML)

#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

#define XML_RET_CHECK_AND_GOTO(ret, label)      do {            \
                if (ret < 0) {                                  \
                        ret = -1;                               \
                        goto label;                             \
                }                                               \
                else                                            \
                        ret = 0;                                \
        }while (0)                                              \

int
cli_begin_xml_output (xmlTextWriterPtr *writer, xmlDocPtr *doc)
{
        int             ret = -1;

        *writer = xmlNewTextWriterDoc (doc, 0);
        if (writer == NULL) {
                ret = -1;
                goto out;
        }

        ret = xmlTextWriterStartDocument (*writer, "1.0", "UTF-8", "yes");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <cliOutput> */
        ret = xmlTextWriterStartElement (*writer, (xmlChar *)"cliOutput");
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_end_xml_output (xmlTextWriterPtr writer, xmlDocPtr doc)
{
        int             ret = -1;

        /* </cliOutput> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterEndDocument (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);


        /* Dump xml document to stdout and pretty format it */
        xmlSaveFormatFileEnc ("-", doc, "UTF-8", 1);

        xmlFreeTextWriter (writer);
        xmlFreeDoc (doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_common (xmlTextWriterPtr writer, int op_ret, int op_errno,
                       char *op_errstr)
{
        int             ret = -1;

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"opRet",
                                               "%d", op_ret);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"opErrno",
                                               "%d", op_errno);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"opErrstr",
                                                "%s", op_errstr);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_str (char *op, char *str, int op_ret, int op_errno,
                    char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        if (op) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"cliOp",
                                                       "%s", op);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        if (str) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"output",
                                                       "%s", str);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_data_pair (dict_t *this, char *key, data_t *value,
                          void *data)
{
        int                     ret = -1;
        xmlTextWriterPtr        *writer = NULL;

        writer = (xmlTextWriterPtr *)data;

        ret = xmlTextWriterWriteFormatElement (*writer, (xmlChar *)key,
                                               "%s", value->data);

        return ret;
}
#endif

int
cli_xml_output_dict ( char *op, dict_t *dict, int op_ret, int op_errno,
                      char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <"op"> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)op);
        XML_RET_CHECK_AND_GOTO (ret, out);

        if (dict)
                dict_foreach (dict, cli_xml_output_data_pair, &writer);

        /* </"op"> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_vol_status_common (xmlTextWriterPtr writer, dict_t *dict,
                                  int   brick_index, int *online,
                                  gf_boolean_t *node_present)
{
        int             ret = -1;
        char            *hostname = NULL;
        char            *path = NULL;
        char            *uuid = NULL;
        int             port = 0;
        int             status = 0;
        int             pid = 0;
        char            key[1024] = {0,};

        snprintf (key, sizeof (key), "brick%d.hostname", brick_index);
        ret = dict_get_str (dict, key, &hostname);
        if (ret) {
                *node_present = _gf_false;
                goto out;
        }
        *node_present = _gf_true;

        /* <node>
         * will be closed in the calling function cli_xml_output_vol_status()*/
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"node");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"hostname",
                                               "%s", hostname);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.path", brick_index);
        ret = dict_get_str (dict, key, &path);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"path",
                                               "%s", path);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.peerid", brick_index);
        ret = dict_get_str (dict, key, &uuid);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"peerid",
                                               "%s", uuid);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.status", brick_index);
        ret = dict_get_int32 (dict, key, &status);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"status",
                                               "%d", status);
        XML_RET_CHECK_AND_GOTO (ret, out);
        *online = status;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.port", brick_index);
        ret = dict_get_int32 (dict, key, &port);
        if (ret)
                goto out;

        /* If the process is either offline or doesn't provide a port (shd)
         * port = "N/A"
         * else print the port number of the process.
         */

        if (*online == 1 && port != 0)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"port",
                                                       "%d", port);
        else
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"port",
                                                       "%s", "N/A");
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.pid", brick_index);
        ret = dict_get_int32 (dict, key, &pid);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"pid",
                                               "%d", pid);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_detail (xmlTextWriterPtr writer, dict_t *dict,
                                  int brick_index)
{
        int             ret = -1;
        uint64_t        size_total = 0;
        uint64_t        size_free = 0;
        char            *device = NULL;
        uint64_t        block_size = 0;
        char            *mnt_options = NULL;
        char            *fs_name = NULL;
        char            *inode_size = NULL;
        uint64_t        inodes_total = 0;
        uint64_t        inodes_free = 0;
        char            key[1024] = {0,};

        snprintf (key, sizeof (key), "brick%d.total", brick_index);
        ret = dict_get_uint64 (dict, key, &size_total);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"sizeTotal",
                                               "%"PRIu64, size_total);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.free", brick_index);
        ret = dict_get_uint64 (dict, key, &size_free);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"sizeFree",
                                               "%"PRIu64, size_free);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.device", brick_index);
        ret = dict_get_str (dict, key, &device);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"device",
                                               "%s", device);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.block_size", brick_index);
        ret = dict_get_uint64 (dict, key, &block_size);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"blockSize",
                                               "%"PRIu64, block_size);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mnt_options", brick_index);
        ret = dict_get_str (dict, key, &mnt_options);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"mntOptions",
                                               "%s", mnt_options);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.fs_name", brick_index);
        ret = dict_get_str (dict, key, &fs_name);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"fsName",
                                               "%s", fs_name);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* inode details are only available for ext 2/3/4 & xfs */
        if (!IS_EXT_FS(fs_name) || strcmp (fs_name, "xfs")) {
                        ret = 0;
                        goto out;
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.inode_size", brick_index);
        ret = dict_get_str (dict, key, &inode_size);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"inodeSize",
                                               "%s", fs_name);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.total_inodes", brick_index);
        ret = dict_get_uint64 (dict, key, &inodes_total);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"inodesTotal",
                                               "%"PRIu64, inodes_total);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.free_inodes", brick_index);
        ret = dict_get_uint64 (dict, key, &inodes_free);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"inodesFree",
                                               "%"PRIu64, inodes_free);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_mempool (xmlTextWriterPtr writer, dict_t *dict,
                                   char *prefix)
{
        int             ret = -1;
        int             mempool_count = 0;
        char            *name = NULL;
        int             hotcount = 0;
        int             coldcount = 0;
        uint64_t        paddedsizeof = 0;
        uint64_t        alloccount = 0;
        int             maxalloc = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <mempool> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"mempool");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.mempool-count", prefix);
        ret = dict_get_int32 (dict, key, &mempool_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"count",
                                               "%d", mempool_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < mempool_count; i++) {
                /* <pool> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"pool");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.name", prefix, i);
                ret = dict_get_str (dict, key, &name);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"name",
                                                       "%s", name);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.hotcount", prefix, i);
                ret = dict_get_int32 (dict, key, &hotcount);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"hotCount",
                                                       "%d", hotcount);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.coldcount", prefix, i);
                ret = dict_get_int32 (dict, key, &coldcount);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"coldCount",
                                                       "%d", coldcount);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.paddedsizeof",
                          prefix, i);
                ret = dict_get_uint64 (dict, key, &paddedsizeof);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"padddedSizeOf", "%"PRIu64,
                         paddedsizeof);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.alloccount", prefix, i);
                ret = dict_get_uint64 (dict, key, &alloccount);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"allocCount",
                                                       "%"PRIu64, alloccount);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.max_alloc", prefix, i);
                ret = dict_get_int32 (dict, key, &maxalloc);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"maxAlloc",
                                                       "%d", maxalloc);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.pool-misses", prefix, i);
                ret = dict_get_uint64 (dict, key, &alloccount);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"poolMisses",
                                                       "%"PRIu64, alloccount);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.pool%d.max-stdalloc", prefix, i);
                ret = dict_get_int32 (dict, key, &maxalloc);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"maxStdAlloc",
                                                       "%d", maxalloc);
                XML_RET_CHECK_AND_GOTO (ret, out);


                /* </pool> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </mempool> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_mem (xmlTextWriterPtr writer, dict_t *dict,
                               int brick_index)
{
        int             ret = -1;
        int             arena = 0;
        int             ordblks = 0;
        int             smblks = 0;
        int             hblks = 0;
        int             hblkhd = 0;
        int             usmblks = 0;
        int             fsmblks = 0;
        int             uordblks = 0;
        int             fordblks = 0;
        int             keepcost = 0;
        char            key[1024] = {0,};

        /* <memStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"memStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <mallinfo> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"mallinfo");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "brick%d.mallinfo.arena", brick_index);
        ret = dict_get_int32 (dict, key, &arena);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"arena",
                                               "%d", arena);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.ordblks", brick_index);
        ret = dict_get_int32 (dict, key, &ordblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"ordblks",
                                               "%d", ordblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.smblks", brick_index);
        ret = dict_get_int32 (dict, key, &smblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"smblks",
                                               "%d", smblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.hblks", brick_index);
        ret = dict_get_int32 (dict, key, &hblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"hblks",
                                               "%d", hblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.hblkhd", brick_index);
        ret = dict_get_int32 (dict, key, &hblkhd);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"hblkhd",
                                               "%d", hblkhd);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.usmblks", brick_index);
        ret = dict_get_int32 (dict, key, &usmblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"usmblks",
                                               "%d", usmblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.fsmblks", brick_index);
        ret = dict_get_int32 (dict, key, &fsmblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"fsmblks",
                                               "%d", fsmblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.uordblks", brick_index);
        ret = dict_get_int32 (dict, key, &uordblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"uordblks",
                                               "%d", uordblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.fordblks", brick_index);
        ret = dict_get_int32 (dict, key, &fordblks);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"fordblks",
                                               "%d", fordblks);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mallinfo.keepcost", brick_index);
        ret = dict_get_int32 (dict, key, &keepcost);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"keepcost",
                                               "%d", keepcost);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </mallinfo> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d", brick_index);
        ret = cli_xml_output_vol_status_mempool (writer, dict, key);
        if (ret)
                goto out;

        /* </memStatus> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_clients (xmlTextWriterPtr writer, dict_t *dict,
                                   int brick_index)
{
        int             ret = -1;
        int             client_count = 0;
        char            *hostname = NULL;
        uint64_t        bytes_read = 0;
        uint64_t        bytes_write = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <clientsStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"clientsStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "brick%d.clientcount", brick_index);
        ret = dict_get_int32 (dict, key, &client_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer,
                                               (xmlChar *)"clientCount",
                                               "%d", client_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < client_count; i++) {
                /* <client> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"client");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.client%d.hostname",
                          brick_index, i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"hostname",
                                                       "%s", hostname);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.client%d.bytesread",
                          brick_index, i);
                ret = dict_get_uint64 (dict, key, &bytes_read);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"bytesRead",
                                                       "%"PRIu64, bytes_read);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.client%d.byteswrite",
                          brick_index, i);
                ret = dict_get_uint64 (dict, key, &bytes_write);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"bytesWrite",
                                                       "%"PRIu64, bytes_write);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </client> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </clientsStatus> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_inode_entry (xmlTextWriterPtr writer, dict_t *dict,
                                       char *prefix)
{
        int             ret = -1;
        char            *gfid = NULL;
        uint64_t        nlookup = 0;
        uint32_t        ref = 0;
        int             ia_type = 0;
        char            key[1024] = {0,};

        /* <inode> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"inode");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.gfid", prefix);
        ret = dict_get_str (dict, key, &gfid);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"gfid",
                                               "%s", gfid);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key,0, sizeof (key));
        snprintf (key, sizeof (key), "%s.nlookup", prefix);
        ret = dict_get_uint64 (dict, key, &nlookup);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"nLookup",
                                               "%"PRIu64, nlookup);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key,0, sizeof (key));
        snprintf (key, sizeof (key), "%s.ref", prefix);
        ret = dict_get_uint32 (dict, key, &ref);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"ref",
                                               "%"PRIu32, ref);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key,0, sizeof (key));
        snprintf (key, sizeof (key), "%s.ia_type", prefix);
        ret = dict_get_int32 (dict, key, &ia_type);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"iaType",
                                               "%d", ia_type);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </inode> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_itable (xmlTextWriterPtr writer, dict_t *dict,
                                  char *prefix)
{
        int             ret = -1;
        uint32_t        active_size = 0;
        uint32_t        lru_size = 0;
        uint32_t        purge_size = 0;
        char            key[1024] = {0,};
        int             i = 0;

        snprintf (key, sizeof (key), "%s.active_size", prefix);
        ret = dict_get_uint32 (dict, key, &active_size);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"activeSize",
                                               "%"PRIu32, active_size);
        XML_RET_CHECK_AND_GOTO (ret, out);
        if (active_size != 0) {
                /* <active> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"active");
                XML_RET_CHECK_AND_GOTO (ret, out);

                for (i = 0; i < active_size; i++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.active%d", prefix, i);
                        ret = cli_xml_output_vol_status_inode_entry
                                (writer, dict, key);
                        if (ret)
                                goto out;
                }
                /* </active> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.lru_size", prefix);
        ret = dict_get_uint32 (dict, key, &lru_size);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"lruSize",
                                               "%"PRIu32, lru_size);
        XML_RET_CHECK_AND_GOTO (ret, out);
        if (lru_size != 0) {
                /* <lru> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"lru");
                XML_RET_CHECK_AND_GOTO (ret, out);

                for (i = 0; i < lru_size; i++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.lru%d", prefix, i);
                        ret = cli_xml_output_vol_status_inode_entry
                                (writer, dict, key);
                        if (ret)
                                goto out;
                }
                /* </lru> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.purge_size", prefix);
        ret = dict_get_uint32 (dict, key, &purge_size);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"purgeSize",
                                               "%"PRIu32, purge_size);
        XML_RET_CHECK_AND_GOTO (ret, out);
        if (purge_size != 0) {
                /* <purge> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"purge");
                XML_RET_CHECK_AND_GOTO (ret, out);

                for (i = 0; i < purge_size; i++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%s.purge%d", prefix, i);
                        ret = cli_xml_output_vol_status_inode_entry
                                (writer, dict, key);
                        if (ret)
                                goto out;
                }
                /* </purge> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_inode (xmlTextWriterPtr writer, dict_t *dict,
                                 int brick_index)
{
        int             ret = -1;
        int             conn_count = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <inodeStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"inodeStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "brick%d.conncount", brick_index);
        ret = dict_get_int32 (dict, key, &conn_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"connections",
                                               "%d", conn_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < conn_count; i++) {
                /* <connection> */
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"connection");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.conn%d.itable",
                          brick_index, i);
                ret = cli_xml_output_vol_status_itable (writer, dict, key);
                if (ret)
                        goto out;

                /* </connection> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </inodeStatus> */
        ret= xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_fdtable (xmlTextWriterPtr writer, dict_t *dict,
                                   char *prefix)
{
        int             ret = -1;
        int             refcount = 0;
        uint32_t        maxfds = 0;
        int             firstfree = 0;
        int             openfds = 0;
        int             fd_pid = 0;
        int             fd_refcount = 0;
        int             fd_flags = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <fdTable> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"fdTable");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.refcount", prefix);
        ret = dict_get_int32 (dict, key, &refcount);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"refCount",
                                               "%d", refcount);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.maxfds", prefix);
        ret = dict_get_uint32 (dict, key, &maxfds);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"maxFds",
                                               "%"PRIu32, maxfds);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.firstfree", prefix);
        ret = dict_get_int32 (dict, key, &firstfree);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"firstFree",
                                               "%d", firstfree);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.openfds", prefix);
        ret = dict_get_int32 (dict, key, &openfds);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"openFds",
                                               "%d", openfds);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < maxfds; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.fdentry%d.pid", prefix, i);
                ret = dict_get_int32 (dict, key, &fd_pid);
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.fdentry%d.refcount",
                          prefix, i);
                ret = dict_get_int32 (dict, key, &fd_refcount);
                if (ret)
                        continue;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.fdentry%d.flags", prefix, i);
                ret = dict_get_int32 (dict, key, &fd_flags);
                if (ret)
                        continue;

                /* <fd> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"fd");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"entry",
                                                       "%d", i+1);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"pid",
                                                       "%d", fd_pid);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"refCount",
                                                       "%d", fd_refcount);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"flags",
                                                       "%d", fd_flags);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </fd> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </fdTable> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_fd (xmlTextWriterPtr writer, dict_t *dict,
                              int brick_index)
{
        int             ret = -1;
        int             conn_count = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <fdStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"fdStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "brick%d.conncount", brick_index);
        ret = dict_get_int32 (dict, key, &conn_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"connections",
                                               "%d", conn_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < conn_count; i++) {
                /* <connection> */
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"connection");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.conn%d.fdtable",
                          brick_index, i);
                ret = cli_xml_output_vol_status_fdtable (writer, dict, key);
                if (ret)
                        goto out;

                /* </connection> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </fdStatus> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_callframe (xmlTextWriterPtr writer, dict_t *dict,
                                     char *prefix)
{
        int             ret = -1;
        int             ref_count = 0;
        char            *translator = NULL;
        int             complete = 0;
        char            *parent = NULL;
        char            *wind_from = NULL;
        char            *wind_to = NULL;
        char            *unwind_from = NULL;
        char            *unwind_to = NULL;
        char            key[1024] = {0,};

        /* <callFrame> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"callFrame");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.refcount", prefix);
        ret = dict_get_int32 (dict, key, &ref_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"refCount",
                                               "%d", ref_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.translator", prefix);
        ret = dict_get_str (dict, key, &translator);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"translator",
                                               "%s", translator);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.complete", prefix);
        ret = dict_get_int32 (dict, key, &complete);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"complete",
                                               "%d", complete);
        XML_RET_CHECK_AND_GOTO (ret ,out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.parent", prefix);
        ret = dict_get_str (dict, key, &parent);
        if (!ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"parent",
                                                       "%s", parent);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.windfrom", prefix);
        ret = dict_get_str (dict, key, &wind_from);
        if (!ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"windFrom",
                                                       "%s", wind_from);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.windto", prefix);
        ret = dict_get_str (dict, key, &wind_to);
        if (!ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"windTo",
                                                       "%s", wind_to);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unwindfrom", prefix);
        ret = dict_get_str (dict, key, &unwind_from);
        if (!ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"unwindFrom",
                                                       "%s", unwind_from);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unwindto", prefix);
        ret = dict_get_str (dict, key, &unwind_to);
        if (!ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"unwindTo",
                                                       "%s", unwind_to);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </callFrame> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_callstack (xmlTextWriterPtr writer, dict_t *dict,
                                     char *prefix)
{
        int             ret = -1;
        int             uid = 0;
        int             gid = 0;
        int             pid = 0;
        uint64_t        unique = 0;
        int             frame_count = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <callStack> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"callStack");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.uid", prefix);
        ret = dict_get_int32 (dict, key, &uid);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"uid",
                                               "%d", uid);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.gid", prefix);
        ret = dict_get_int32 (dict, key, &gid);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"gid",
                                               "%d", gid);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.pid", prefix);
        ret = dict_get_int32 (dict, key, &pid);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"pid",
                                               "%d", pid);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.unique", prefix);
        ret = dict_get_uint64 (dict, key, &unique);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"unique",
                                               "%"PRIu64, unique);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.count", prefix);
        ret = dict_get_int32 (dict, key, &frame_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"frameCount",
                                               "%d", frame_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < frame_count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.frame%d", prefix, i);
                ret = cli_xml_output_vol_status_callframe (writer, dict,
                                                           key);
                if (ret)
                        goto out;
        }

        /* </callStack> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_callpool (xmlTextWriterPtr writer, dict_t *dict,
                                    int brick_index)
{
        int             ret = -1;
        int             call_count = 0;
        char            key[1024] = {0,};
        int             i = 0;

        /* <callpoolStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"callpoolStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "brick%d.callpool.count", brick_index);
        ret = dict_get_int32 (dict, key, &call_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"count",
                                               "%d", call_count);

        for (i = 0; i < call_count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.callpool.stack%d",
                          brick_index, i);
                ret = cli_xml_output_vol_status_callstack (writer, dict,
                                                           key);
                if (ret)
                        goto out;
        }

        /* </callpoolStatus> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_vol_status_begin (cli_local_t *local, int op_ret, int op_errno,
                                 char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;

        ret = cli_begin_xml_output (&(local->writer), &(local->doc));
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_xml_output_common (local->writer, op_ret, op_errno,
                                     op_errstr);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <volStatus> */
        ret = xmlTextWriterStartElement (local->writer,
                                         (xmlChar *) "volStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <volumes> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"volumes");
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_status_end (cli_local_t *local)
{
#if (HAVE_LIB_XML)
        int     ret = -1;

        /* </volumes> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </volStatus> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO(ret, out);

        ret = cli_end_xml_output (local->writer, local->doc);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_remove_brick_task_params (xmlTextWriterPtr writer, dict_t *dict,
                                         char *prefix)
{
        int             ret = -1;
        char            key[1024] = {0,};
        int             count = 0;
        int             i = 0;
        char            *brick = NULL;

        /* <params> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"params");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.count", prefix);
        ret = dict_get_int32 (dict, key, &count);
        if (ret)
                goto out;

        for (i = 1; i <= count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.brick%d", prefix, i);
                ret = dict_get_str (dict, key, &brick);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"brick",
                                                       "%s", brick);
                XML_RET_CHECK_AND_GOTO (ret, out);
                brick = NULL;
        }

        /* </param> */
        ret = xmlTextWriterEndElement (writer);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_replace_brick_task_params (xmlTextWriterPtr writer, dict_t *dict,
                                          char *prefix)
{

        int             ret = -1;
        char            key[1024] = {0,};
        char            *brick = NULL;

        /* <params> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"params");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.src-brick", prefix);
        ret = dict_get_str (dict, key, &brick);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"srcBrick",
                                               "%s", brick);
        XML_RET_CHECK_AND_GOTO (ret, out);


        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%s.dst-brick", prefix);
        ret = dict_get_str (dict, key, &brick);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"dstBrick",
                                               "%s", brick);
        XML_RET_CHECK_AND_GOTO (ret, out);


        /* </param> */
        ret = xmlTextWriterEndElement (writer);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_status_tasks (cli_local_t *local, dict_t *dict) {
        int                     ret = -1;
        char                    *task_type = NULL;
        char                    *task_id_str = NULL;
        int                     status = 0;
        int                     tasks = 0;
        char                    key[1024] = {0,};
        int                     i = 0;

        /* <tasks> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"tasks");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "tasks", &tasks);
        if (ret)
                goto out;

        for (i = 0; i < tasks; i++) {
                /* <task> */
                ret = xmlTextWriterStartElement (local->writer,
                                                 (xmlChar *)"task");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.type", i);
                ret = dict_get_str (dict, key, &task_type);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"type",
                                                       "%s", task_type);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.id", i);
                ret = dict_get_str (dict, key, &task_id_str);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"id",
                                                       "%s", task_id_str);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d.status", i);
                ret = dict_get_int32 (dict, key, &status);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"status",
                                                       "%d", status);
                XML_RET_CHECK_AND_GOTO (ret, out);

                if (!strcmp (task_type, "Replace brick")) {
                    if (status) {
                        status = GF_DEFRAG_STATUS_COMPLETE;
                    } else {
                        status = GF_DEFRAG_STATUS_STARTED;
                    }
                }

                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"statusStr",
                                                       "%s",
                                             cli_vol_task_status_str[status]);

                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d", i);
                if (!strcmp (task_type, "Replace brick")) {
                        ret = cli_xml_output_replace_brick_task_params
                                (local->writer, dict, key);
                        if (ret)
                                goto out;
                } else if (!strcmp (task_type, "Remove brick")) {
                        ret = cli_xml_output_remove_brick_task_params
                                (local->writer, dict, key);
                        if (ret)
                                goto out;
                }


                /* </task> */
                ret = xmlTextWriterEndElement (local->writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </tasks> */
        ret = xmlTextWriterEndElement (local->writer);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

#endif

int
cli_xml_output_vol_status_tasks_detail (cli_local_t *local, dict_t *dict)
{
#if (HAVE_LIB_XML)
        int    ret     = -1;
        char  *volname = NULL;

        /*<volume>*/
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"volume");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"volName", "%s",
                                               volname);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_xml_output_vol_status_tasks (local, dict);
        if (ret)
                goto out;

        /* </volume> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_status (cli_local_t *local, dict_t *dict)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        char                    *volname = NULL;
        int                     brick_count = 0;
        int                     brick_index_max = -1;
        int                     other_count = 0;
        int                     index_max = 0;
        uint32_t                cmd = GF_CLI_STATUS_NONE;
        int                     online = 0;
        gf_boolean_t            node_present = _gf_true;
        int                     i;


        /* <volume> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"volume");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"volName", "%s",
                                               volname);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"nodeCount", "%d",
                                               brick_count);
        if (ret)
                goto out;

        ret = dict_get_uint32 (dict, "cmd", &cmd);
        if (ret)
                goto out;

        ret = dict_get_int32 (dict, "brick-index-max", &brick_index_max);
        if (ret)
                goto out;
        ret = dict_get_int32 (dict, "other-count", &other_count);
        if (ret)
                goto out;

        index_max = brick_index_max + other_count;

        for (i = 0; i <= index_max; i++) {
                ret = cli_xml_output_vol_status_common (local->writer, dict, i,
                                                        &online, &node_present);
                if (ret) {
                        if (node_present)
                                goto out;
                        else
                                continue;
                }

                switch (cmd & GF_CLI_STATUS_MASK) {
                case GF_CLI_STATUS_DETAIL:
                        ret = cli_xml_output_vol_status_detail (local->writer,
                                                                dict, i);
                        if (ret)
                                goto out;
                        break;

                case GF_CLI_STATUS_MEM:
                        if (online) {
                                ret = cli_xml_output_vol_status_mem
                                        (local->writer, dict, i);
                                if (ret)
                                        goto out;
                        }
                        break;

                case GF_CLI_STATUS_CLIENTS:
                        if (online) {
                                ret = cli_xml_output_vol_status_clients
                                        (local->writer, dict, i);
                                if (ret)
                                        goto out;
                        }
                        break;

                case GF_CLI_STATUS_INODE:
                        if (online) {
                                ret = cli_xml_output_vol_status_inode
                                        (local->writer, dict, i);
                                if (ret)
                                        goto out;
                        }
                        break;

                case GF_CLI_STATUS_FD:
                        if (online) {
                                ret = cli_xml_output_vol_status_fd
                                        (local->writer, dict, i);
                                if (ret)
                                        goto out;
                        }
                        break;

                case GF_CLI_STATUS_CALLPOOL:
                        if (online) {
                                ret = cli_xml_output_vol_status_callpool
                                        (local->writer, dict, i);
                                if (ret)
                                        goto out;
                        }
                        break;
                default:
                        break;

                }

                /* </node>  was opened in cli_xml_output_vol_status_common()*/
                ret = xmlTextWriterEndElement (local->writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* Tasks are only present when a normal volume status call is done on a
         * single volume or on all volumes
         */
        if (((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE) &&
            (cmd & (GF_CLI_STATUS_VOL|GF_CLI_STATUS_ALL))) {
                ret = cli_xml_output_vol_status_tasks (local, dict);
                if (ret)
                        goto out;
        }

        /* </volume> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_vol_top_rw_perf (xmlTextWriterPtr writer, dict_t *dict,
                                int brick_index, int member_index)
{
        int        ret = -1;
        char      *filename = NULL;
        uint64_t   throughput = 0;
        long int   time_sec = 0;
        long int   time_usec = 0;
        char       timestr[256] = {0,};
        char       key[1024] = {0,};

        /* <file> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"file");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%d-filename-%d", brick_index,
                  member_index);
        ret = dict_get_str (dict, key, &filename);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"filename",
                                               "%s", filename);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-value-%d", brick_index, member_index);
        ret = dict_get_uint64 (dict, key, &throughput);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"count",
                                               "%"PRIu64, throughput);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-time-sec-%d", brick_index,
                  member_index);
        ret = dict_get_int32 (dict, key, (int32_t *)&time_sec);
        if (ret)
                goto out;

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-time-usec-%d", brick_index,
                  member_index);
        ret = dict_get_int32 (dict, key, (int32_t *)&time_usec);
        if (ret)
                goto out;

        gf_time_fmt (timestr, sizeof timestr, time_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr),
                  sizeof timestr - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, time_usec);
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"time",
                                               "%s", timestr);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </file> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
cli_xml_output_vol_top_other (xmlTextWriterPtr writer, dict_t *dict,
                                int brick_index, int member_index)
{
        int             ret = -1;
        char            *filename = NULL;
        uint64_t        count = 0;
        char            key[1024] = {0,};

        /* <file> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"file");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%d-filename-%d", brick_index,
                  member_index);
        ret = dict_get_str (dict, key, &filename);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"filename",
                                               "%s", filename);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-value-%d", brick_index, member_index);
        ret = dict_get_uint64 (dict, key, &count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"count",
                                               "%"PRIu64, count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </file> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_vol_top (dict_t *dict, int op_ret, int op_errno,
                        char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        int                     brick_count = 0;
        int                     top_op = GF_CLI_TOP_NONE;
        char                    *brick_name = NULL;
        int                     members = 0;
        uint64_t                current_open = 0;
        uint64_t                max_open = 0;
        char                    *max_open_time = NULL;
        double                  throughput = 0.0;
        double                  time_taken = 0.0;
        char                    key[1024] = {0,};
        int                     i = 0;
        int                     j = 0;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volTop> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volTop");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"brickCount",
                                               "%d", brick_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "1-top-op", &top_op);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"topOp",
                                               "%d", top_op);
        XML_RET_CHECK_AND_GOTO (ret, out);

        while (i < brick_count) {
                i++;

                /* <brick> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"brick");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-brick", i);
                ret = dict_get_str (dict, key, &brick_name);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"name",
                                                       "%s", brick_name);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key , sizeof (key), "%d-members", i);
                ret = dict_get_int32 (dict, key, &members);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"members",
                                                       "%d", members);
                XML_RET_CHECK_AND_GOTO (ret, out);

                switch (top_op) {
                case GF_CLI_TOP_OPEN:
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-current-open", i);
                        ret = dict_get_uint64 (dict, key, &current_open);
                        if (ret)
                                goto out;
                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"currentOpen", "%"PRIu64,
                                 current_open);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-max-open", i);
                        ret = dict_get_uint64 (dict, key, &max_open);
                        if (ret)
                                goto out;
                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"maxOpen", "%"PRIu64,
                                 max_open);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-max-openfd-time", i);
                        ret = dict_get_str (dict, key, &max_open_time);
                        if (ret)
                                goto out;
                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"maxOpenTime", "%s",
                                 max_open_time);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                case GF_CLI_TOP_READ:
                case GF_CLI_TOP_WRITE:
                case GF_CLI_TOP_OPENDIR:
                case GF_CLI_TOP_READDIR:

                        break;

                case GF_CLI_TOP_READ_PERF:
                case GF_CLI_TOP_WRITE_PERF:
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-throughput", i);
                        ret = dict_get_double (dict, key, &throughput);
                        if (!ret) {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key), "%d-time", i);
                                ret = dict_get_double (dict, key, &time_taken);
                        }

                        if (!ret) {
                                ret = xmlTextWriterWriteFormatElement
                                        (writer, (xmlChar *)"throughput",
                                         "%f", throughput);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatElement
                                        (writer, (xmlChar *)"timeTaken",
                                         "%f", time_taken);
                        }

                        break;

                default:
                        ret = -1;
                        goto out;
                }

                for (j = 1; j <= members; j++) {
                        if (top_op == GF_CLI_TOP_READ_PERF ||
                            top_op == GF_CLI_TOP_WRITE_PERF) {
                                ret = cli_xml_output_vol_top_rw_perf
                                        (writer, dict, i, j);
                        } else {
                                ret = cli_xml_output_vol_top_other
                                        (writer, dict, i, j);
                        }
                        if (ret)
                                goto out;
                }


                /* </brick> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </volTop> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_vol_profile_stats (xmlTextWriterPtr writer, dict_t *dict,
                                  int brick_index, int interval)
{
        int                     ret = -1;
        uint64_t                read_count = 0;
        uint64_t                write_count = 0;
        uint64_t                hits = 0;
        double                  avg_latency = 0.0;
        double                  max_latency = 0.0;
        double                  min_latency = 0.0;
        uint64_t                duration = 0;
        uint64_t                total_read = 0;
        uint64_t                total_write = 0;
        char                    key[1024] = {0};
        int                     i = 0;

        /* <cumulativeStats> || <intervalStats> */
        if (interval == -1)
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"cumulativeStats");
        else
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"intervalStats");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <blockStats> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"blockStats");
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < 32; i++) {
                /* <block> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"block");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"size", "%"PRIu32, (1 << i));
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-read-%d", brick_index,
                          interval, (1 << i));
                ret = dict_get_uint64 (dict, key, &read_count);
                if (ret)
                        read_count = 0;
                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"reads", "%"PRIu64, read_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-write-%d", brick_index,
                          interval, (1 << i));
                ret = dict_get_uint64 (dict, key, &write_count);
                if (ret)
                        write_count = 0;
                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"writes", "%"PRIu64, write_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </block> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </blockStats> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <fopStats> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"fopStats");
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-hits", brick_index,
                          interval, i);
                ret = dict_get_uint64 (dict, key, &hits);
                if (ret)
                        goto cont;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-avglatency", brick_index,
                          interval, i);
                ret = dict_get_double (dict, key, &avg_latency);
                if (ret)
                        goto cont;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-minlatency", brick_index,
                          interval, i);
                ret = dict_get_double (dict, key, &min_latency);
                if (ret)
                        goto cont;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-maxlatency", brick_index,
                          interval, i);
                ret = dict_get_double (dict, key, &max_latency);
                if (ret)
                        goto cont;

                /* <fop> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"fop");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"name","%s", gf_fop_list[i]);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"hits", "%"PRIu64, hits);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"avgLatency", "%f", avg_latency);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"minLatency", "%f", min_latency);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"maxLatency", "%f", max_latency);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </fop> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

cont:
                hits = 0;
                avg_latency = 0.0;
                min_latency = 0.0;
                max_latency = 0.0;
        }

        /* </fopStats> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-%d-duration", brick_index, interval);
        ret = dict_get_uint64 (dict, key, &duration);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"duration",
                                               "%"PRIu64, duration);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-%d-total-read", brick_index, interval);
        ret = dict_get_uint64 (dict, key, &total_read);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"totalRead",
                                               "%"PRIu64, total_read);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "%d-%d-total-write", brick_index, interval);
        ret = dict_get_uint64 (dict, key, &total_write);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"totalWrite",
                                               "%"PRIu64, total_write);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </cumulativeStats> || </intervalStats> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_vol_profile (dict_t *dict, int op_ret, int op_errno,
                            char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *volname = NULL;
        int                     op = GF_CLI_STATS_NONE;
        int                     info_op = GF_CLI_INFO_NONE;
        int                     brick_count = 0;
        char                    *brick_name = NULL;
        int                     interval = 0;
        char                    key[1024] = {0,};
        int                     i = 0;
        int                     stats_cleared = 0;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volProfile> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volProfile");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"volname",
                                               "%s", volname);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "op", &op);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"profileOp",
                                               "%d", op);
        XML_RET_CHECK_AND_GOTO (ret, out);

        if (GF_CLI_STATS_INFO != op)
                goto cont;

        ret = dict_get_int32 (dict, "count", &brick_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"brickCount",
                                               "%d", brick_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "info-op", &info_op);
        if (ret)
                goto out;

        while (i < brick_count) {
                i++;

                /* <brick> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"brick");
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%d-brick", i);
                ret = dict_get_str (dict, key, &brick_name);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"brickName", "%s", brick_name);
                XML_RET_CHECK_AND_GOTO (ret, out);

                if (GF_CLI_INFO_CLEAR == info_op) {
                        snprintf (key, sizeof (key), "%d-stats-cleared", i);
                        ret = dict_get_int32 (dict, key, &stats_cleared);
                        if (ret)
                                goto out;

                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"clearStats", "%s", 
                                stats_cleared ? "Cleared stats." :
                                                 "Failed to clear stats.");
                        if (ret)
                                goto out;
                } else {
                        snprintf (key, sizeof (key), "%d-cumulative", i);
                        ret = dict_get_int32 (dict, key, &interval);
                        if (ret == 0) {
                                ret = cli_xml_output_vol_profile_stats
                                        (writer, dict, i, interval);
                                if (ret)
                                        goto out;
                        }

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "%d-interval", i);
                        ret = dict_get_int32 (dict, key, &interval);
                        if (ret == 0) {
                                ret = cli_xml_output_vol_profile_stats
                                        (writer, dict, i, interval);
                                if (ret)
                                        goto out;
                        }
                }

                /* </brick> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

cont:
        /* </volProfile> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_list (dict_t *dict, int op_ret, int op_errno,
                         char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        int                     count = 0;
        char                    *volname = NULL;
        char                    key[1024] = {0,};
        int                     i = 0;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volList> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volList");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"count",
                                               "%d", count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d", i);
                ret = dict_get_str (dict, key, &volname);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"volume",
                                                       "%s", volname);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </volList> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_vol_info_option (xmlTextWriterPtr writer, char *substr,
                                char *optstr, char *valstr)
{
        int             ret = -1;
        char            *ptr1 = NULL;
        char            *ptr2 = NULL;

        ptr1 = substr;
        ptr2 = optstr;

        while (ptr1) {
                if (*ptr1 != *ptr2)
                        break;
                ptr1++;
                ptr2++;
                if (!ptr1)
                        goto out;
                if (!ptr2)
                        goto out;
        }
        if (*ptr2 == '\0')
                goto out;

        /* <option> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"option");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"name",
                                               "%s", ptr2);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"value",
                                               "%s", valstr);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </option> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

struct tmp_xml_option_logger {
        char *key;
        xmlTextWriterPtr writer;
};

static int
_output_vol_info_option (dict_t *d, char *k, data_t *v,
                         void *data)
{
        int                           ret   = 0;
        char                         *ptr   = NULL;
        struct tmp_xml_option_logger *tmp   = NULL;

        tmp = data;

        ptr = strstr (k, "option.");
        if (!ptr)
                goto out;

        if (!v) {
                ret = -1;
                goto out;
        }
        ret = cli_xml_output_vol_info_option (tmp->writer, tmp->key, k,
                                              v->data);

out:
        return ret;
}

int
cli_xml_output_vol_info_options (xmlTextWriterPtr writer, dict_t *dict,
                                 char *prefix)
{
        int             ret = -1;
        int             opt_count = 0;
        char            key[1024] = {0,};
        struct tmp_xml_option_logger tmp = {0,};

        snprintf (key, sizeof (key), "%s.opt_count", prefix);
        ret = dict_get_int32 (dict, key, &opt_count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"optCount",
                                               "%d", opt_count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <options> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"options");
        XML_RET_CHECK_AND_GOTO (ret, out);
        snprintf (key, sizeof (key), "%s.option.", prefix);

        tmp.key = key;
        tmp.writer = writer;
        ret = dict_foreach (dict, _output_vol_info_option, &tmp);
        if (ret)
                goto out;

        /* </options> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_vol_info (cli_local_t *local, dict_t *dict)
{
#if (HAVE_LIB_XML)
        int                     ret = 0;
        int                     count = 0;
        char                    *volname = NULL;
        char                    *volume_id = NULL;
        char                    *uuid = NULL;
        int                     type = 0;
        int                     status = 0;
        int                     brick_count = 0;
        int                     dist_count = 0;
        int                     stripe_count = 0;
        int                     replica_count = 0;
        int                     transport = 0;
        char                    *brick = NULL;
        char                    key[1024] = {0,};
        int                     i = 0;
        int                     j = 1;
        char                    *caps = NULL;
        int                     k __attribute__((unused)) = 0;

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        for (i = 0; i < count; i++) {
                /* <volume> */
                ret = xmlTextWriterStartElement (local->writer,
                                                 (xmlChar *)"volume");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.name", i);
                ret = dict_get_str (dict, key, &volname);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"name",
                                                       "%s", volname);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.volume_id", i);
                ret = dict_get_str (dict, key, &volume_id);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"id",
                                                       "%s", volume_id);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.status", i);
                ret = dict_get_int32 (dict, key, &status);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"status",
                                                       "%d", status);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret =xmlTextWriterWriteFormatElement
                        (local->writer, (xmlChar *)"statusStr", "%s",
                         cli_vol_status_str[status]);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.brick_count", i);
                ret = dict_get_int32 (dict, key, &brick_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"brickCount",
                                                       "%d", brick_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.dist_count", i);
                ret = dict_get_int32 (dict, key, &dist_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"distCount",
                                                       "%d", dist_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.stripe_count", i);
                ret = dict_get_int32 (dict, key, &stripe_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"stripeCount",
                                                       "%d", stripe_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.replica_count", i);
                ret = dict_get_int32 (dict, key, &replica_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"replicaCount",
                                                       "%d", replica_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.type", i);
                ret = dict_get_int32 (dict, key, &type);
                if (ret)
                        goto out;
                /* For Distributed-(stripe,replicate,stipe-replicate) types */
                if ((type > 0) && (dist_count < brick_count))
                        type += 3;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"type",
                                                       "%d", type);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"typeStr",
                                                       "%s",
                                                       cli_vol_type_str[type]);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.transport", i);
                ret = dict_get_int32 (dict, key, &transport);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"transport",
                                                       "%d", transport);
                XML_RET_CHECK_AND_GOTO (ret, out);

#ifdef HAVE_BD_XLATOR
                /* <xlators> */
                ret = xmlTextWriterStartElement (local->writer,
                                                 (xmlChar *)"xlators");
                XML_RET_CHECK_AND_GOTO (ret, out);

                for (k = 0; ; k++) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key),"volume%d.xlator%d", i, k);
                        ret = dict_get_str (dict, key, &caps);
                        if (ret)
                                break;

                        /* <xlator> */
                        ret = xmlTextWriterStartElement (local->writer,
                                                         (xmlChar *)"xlator");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement
                                (local->writer, (xmlChar *)"name", "%s", caps);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        /* <capabilities> */
                        ret = xmlTextWriterStartElement (local->writer,
                                                         (xmlChar *)
                                                         "capabilities");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        j = 0;
                        for (j = 0; ;j++) {
                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.xlator%d.caps%d", i, k, j);
                                ret = dict_get_str (dict, key, &caps);
                                if (ret)
                                        break;
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"capability",
                                         "%s", caps);
                                XML_RET_CHECK_AND_GOTO (ret, out);
                        }
                        /* </capabilities> */
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                        /* </xlator> */
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }
                ret = xmlTextWriterFullEndElement (local->writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
                /* </xlators> */
#else
                caps = 0; /* Avoid compiler warnings when BD not enabled */
#endif
                j = 1;

                /* <bricks> */
                ret = xmlTextWriterStartElement (local->writer,
                                                 (xmlChar *)"bricks");
                XML_RET_CHECK_AND_GOTO (ret, out);
                while (j <= brick_count) {
                        ret = xmlTextWriterStartElement
                                (local->writer, (xmlChar *)"brick");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "volume%d.brick%d.uuid",
                                  i, j);
                        ret = dict_get_str (dict, key, &uuid);
                        if (ret)
                                goto out;
                        ret = xmlTextWriterWriteFormatAttribute
                                (local->writer, (xmlChar *)"uuid", "%s",
                                 uuid);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "volume%d.brick%d", i, j);
                        ret = dict_get_str (dict, key, &brick);
                        if (ret)
                                goto out;
                        ret = xmlTextWriterWriteFormatString
                                (local->writer, "%s", brick);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement
                                (local->writer, (xmlChar *)"name", "%s",
                                 brick);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement
                                (local->writer, (xmlChar *)"hostUuid", "%s",
                                 uuid);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        /* </brick> */
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        j++;
                }
                /* </bricks> */
                ret = xmlTextWriterEndElement (local->writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d", i);
                ret = cli_xml_output_vol_info_options (local->writer, dict,
                                                       key);
                if (ret)
                        goto out;

                /* </volume> */
                ret = xmlTextWriterEndElement (local->writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        if (volname) {
                GF_FREE (local->get_vol.volname);
                local->get_vol.volname = gf_strdup (volname);
                local->vol_count += count;
        }
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_info_begin (cli_local_t *local, int op_ret, int op_errno,
                               char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;

        GF_ASSERT (local);

        ret = cli_begin_xml_output (&(local->writer), &(local->doc));
        if (ret)
                goto out;

        ret = cli_xml_output_common (local->writer, op_ret, op_errno,
                                     op_errstr);
        if (ret)
                goto out;

        /* <volInfo> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"volInfo");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <volumes> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"volumes");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* Init vol count */
        local->vol_count = 0;

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_info_end (cli_local_t *local)
{
#if (HAVE_LIB_XML)
        int             ret = -1;

        GF_ASSERT (local);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"count",
                                               "%d", local->vol_count);

        /* </volumes> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </volInfo> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (local->writer, local->doc);

out:
        gf_log ("cli", GF_LOG_ERROR, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_quota_limit_list (char *volname, char *limit_list,
                                     int op_ret, int op_errno,
                                     char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        int64_t                 size = 0;
        int64_t                 limit_value = 0;
        int                     i = 0;
        int                     j = 0;
        int                     k = 0;
        int                     len = 0;
        char                    *size_str = NULL;
        char                    path[PATH_MAX] = {0,};
        char                    ret_str[1024] = {0,};
        char                    value[1024] = {0,};
        char                    mountdir[] = "/tmp/mountXXXXXX";
        char                    abspath[PATH_MAX] = {0,};
        runner_t                runner = {0,};

        GF_ASSERT (volname);
        GF_ASSERT (limit_list);

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volQuota> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volQuota");
        XML_RET_CHECK_AND_GOTO (ret, out);

        if (!limit_list)
                goto cont;

        len = strlen (limit_list);
        if (len == 0)
                goto cont;

        if (mkdtemp (mountdir) == NULL) {
                gf_log ("cli", GF_LOG_ERROR, "failed to create a temporary"
                        " mount directory");
                ret = -1;
                goto out;
        }

        ret = runcmd (SBIN_DIR"/glusterfs", "-s", "localhost",
                      "--volfile-id", volname, "-l",
                      DEFAULT_LOG_FILE_DIRECTORY"/quota-list-xml.log",
                      mountdir, NULL);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR,
                        "failed to mount glusterfs client");
                ret = -1;
                goto rm_dir;
        }

        while (i < len) {
                j = 0;
                k = 0;
                size = 0;

                while (limit_list[i] != ':')
                        path[k++] = limit_list[i++];
                path[k] = '\0';

                i++;

                while (limit_list[i] != ',' && limit_list[i] != '\0')
                        value[j++] = limit_list[i++];
                i++;

                snprintf (abspath, sizeof (abspath), "%s/%s", mountdir, path);
                ret = sys_lgetxattr (abspath, "trusted.limit.list",
                                     (void *)ret_str, 4096);
                if (ret >= 0) {
                        sscanf (ret_str, "%"SCNd64",%"SCNd64, &size,
                                &limit_value);
                        size_str = gf_uint64_2human_readable ((uint64_t)size);
                }

                /* <quota> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"quota");
                XML_RET_CHECK_AND_GOTO (ret, unmount);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"path", "%s", path);
                XML_RET_CHECK_AND_GOTO (ret, unmount);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"limit", "%s", value);
                XML_RET_CHECK_AND_GOTO (ret, unmount);

                if (size_str) {
                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"size", "%s", size_str);
                        XML_RET_CHECK_AND_GOTO (ret, unmount);
                        GF_FREE (size_str);
                } else {
                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"size", "%"PRId64, size);
                        XML_RET_CHECK_AND_GOTO (ret, unmount);
                }

                /* </quota> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, unmount);

        }

unmount:
        runinit (&runner);
        runner_add_args (&runner, "umount",
#if GF_LINUX_HOST_OS
                         "-l",
#endif
                         mountdir, NULL);
        ret = runner_run_reuse (&runner);
        if (ret)
                runner_log (&runner, "cli", GF_LOG_WARNING, "error executing");
        runner_end (&runner);

rm_dir:
        rmdir (mountdir);

cont:
        /* </volQuota> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);

out:
        GF_FREE (size_str);
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_peer_status (dict_t *dict, int op_ret, int op_errno,
                            char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        int                     count = 0;
        char                    *uuid = NULL;
        char                    *hostname = NULL;
        int                     connected = 0;
        int                     state_id = 0;
        char                    *state_str = NULL;
        int                     i = 1;
        char                    key[1024] = {0,};

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <peerStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"peerStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        if (!dict)
                goto cont;

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;

        while (i <= count) {
                /* <peer> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"peer");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d.uuid", i);
                ret = dict_get_str (dict, key, &uuid);
                if (ret)
                        goto out;

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"uuid",
                                                       "%s", uuid);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d.hostname", i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"hostname",
                                                       "%s", hostname);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d.connected", i);
                ret = dict_get_int32 (dict, key, &connected);
                if (ret)
                        goto out;

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"connected",
                                                       "%d", connected);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d.stateId", i);
                ret = dict_get_int32 (dict, key, &state_id);
                if (!ret) {
                        /* ignore */

                        ret = xmlTextWriterWriteFormatElement (writer,
                                           (xmlChar *)"state", "%d", state_id);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "friend%d.state", i);
                ret = dict_get_str (dict, key, &state_str);
                if (!ret) {
                        /* ignore */

                        ret = xmlTextWriterWriteFormatElement (writer,
                                       (xmlChar *)"stateStr", "%s", state_str);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }

                /* </peer> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                i++;
        }

cont:
        /* </peerStatus> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
/* Used for rebalance stop/status, remove-brick status */
int
cli_xml_output_vol_rebalance_status (xmlTextWriterPtr writer, dict_t *dict,
                                     enum gf_task_types task_type)
{
        int                     ret = -1;
        int                     count = 0;
        char                    *node_name = NULL;
        char                    *node_uuid = NULL;
        uint64_t                files = 0;
        uint64_t                size = 0;
        uint64_t                lookups = 0;
        int                     status_rcd = 0;
        uint64_t                failures = 0;
        uint64_t                skipped = 0;
        uint64_t                total_files = 0;
        uint64_t                total_size = 0;
        uint64_t                total_lookups = 0;
        uint64_t                total_failures = 0;
        uint64_t                total_skipped = 0;
        char                    key[1024] = {0,};
        int                     i = 0;
        int                     overall_status = -1;
        double                  elapsed = 0;
        double                  overall_elapsed = 0;

        if (!dict) {
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (dict, "count", &count);
        if (ret)
                goto out;
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"nodeCount",
                                               "%d", count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        while (i < count) {
                i++;
                /* Getting status early, to skip nodes that don't have the
                 * rebalance process started
                 */
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "status-%d", i);
                ret = dict_get_int32 (dict, key, &status_rcd);

                /* If glusterd is down it fails to get the status, try
                 getting status from other nodes */
                if (ret)
                    continue;
                if (GF_DEFRAG_STATUS_NOT_STARTED == status_rcd)
                        continue;

                /* <node> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"node");
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "node-name-%d", i);
                ret = dict_get_str (dict, key, &node_name);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"nodeName",
                                                       "%s", node_name);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "node-uuid-%d", i);
                ret = dict_get_str (dict, key, &node_uuid);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"id",
                                                       "%s", node_uuid);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "files-%d", i);
                ret = dict_get_uint64 (dict, key, &files);
                if (ret)
                        goto out;
                total_files += files;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"files",
                                                       "%"PRIu64, files);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "size-%d", i);
                ret = dict_get_uint64 (dict, key, &size);
                if (ret)
                        goto out;
                total_size += size;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"size",
                                                       "%"PRIu64,size);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "lookups-%d", i);
                ret = dict_get_uint64 (dict, key, &lookups);
                if (ret)
                        goto out;
                total_lookups += lookups;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"lookups",
                                                       "%"PRIu64, lookups);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "failures-%d", i);
                ret = dict_get_uint64 (dict, key, &failures);
                if (ret)
                        goto out;
                total_failures += failures;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"failures",
                                                       "%"PRIu64, failures);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* skipped-%d is not available for remove brick in dict,
                   so using failures as skipped count in case of remove-brick
                   similar to logic used in CLI(non xml output) */
                if (task_type == GF_TASK_TYPE_REBALANCE) {
                    memset (key, 0, sizeof (key));
                    snprintf (key, sizeof (key), "skipped-%d", i);
                }
                else {
                    memset (key, 0, sizeof (key));
                    snprintf (key, sizeof (key), "failures-%d", i);
                }

                ret = dict_get_uint64 (dict, key, &skipped);
                if (ret)
                        goto out;
                total_skipped += skipped;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"skipped",
                                                       "%"PRIu64, skipped);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"status",
                                                       "%d", status_rcd);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"statusStr",
                                                       "%s",
                                         cli_vol_task_status_str[status_rcd]);

                memset (key, 0, 256);
                snprintf (key, 256, "run-time-%d", i);
                ret = dict_get_double (dict, key, &elapsed);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"runtime",
                                                       "%.2f", elapsed);
                XML_RET_CHECK_AND_GOTO (ret, out);

                if (elapsed > overall_elapsed) {
                    overall_elapsed = elapsed;
                }

                /* Rebalance has 5 states,
                 * NOT_STARTED, STARTED, STOPPED, COMPLETE, FAILED
                 * The precedence used to determine the aggregate status is as
                 * below,
                 * STARTED > FAILED > STOPPED > COMPLETE > NOT_STARTED
                 */
                /* TODO: Move this to a common place utilities that both CLI and
                 * glusterd need.
                 * Till then if the below algorithm is changed, change it in
                 * glusterd_volume_status_aggregate_tasks_status in
                 * glusterd-utils.c
                 */

                if (-1 == overall_status)
                        overall_status = status_rcd;
                int rank[] = {
                        [GF_DEFRAG_STATUS_STARTED] = 1,
                        [GF_DEFRAG_STATUS_FAILED] = 2,
                        [GF_DEFRAG_STATUS_STOPPED] = 3,
                        [GF_DEFRAG_STATUS_COMPLETE] = 4,
                        [GF_DEFRAG_STATUS_NOT_STARTED] = 5
                };
                if (rank[status_rcd] <= rank[overall_status])
                        overall_status = status_rcd;

                /* </node> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* Aggregate status */
        /* <aggregate> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"aggregate");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"files",
                                               "%"PRIu64, total_files);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"size",
                                               "%"PRIu64, total_size);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"lookups",
                                               "%"PRIu64, total_lookups);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"failures",
                                               "%"PRIu64, total_failures);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"skipped",
                                               "%"PRIu64, total_skipped);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"status",
                                               "%d", overall_status);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"statusStr",
                                               "%s",
                                      cli_vol_task_status_str[overall_status]);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (writer,(xmlChar *)"runtime",
                                               "%.2f", overall_elapsed);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </aggregate> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_vol_rebalance (gf_cli_defrag_type op, dict_t *dict, int op_ret,
                              int op_errno, char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *task_id_str = NULL;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volRebalance> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volRebalance");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, GF_REBALANCE_TID_KEY, &task_id_str);
        if (ret == 0) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"task-id",
                                                       "%s", task_id_str);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"op",
                                               "%d", op);
        XML_RET_CHECK_AND_GOTO (ret, out);

        if ((GF_DEFRAG_CMD_STOP == op) || (GF_DEFRAG_CMD_STATUS == op)) {
                ret = cli_xml_output_vol_rebalance_status (writer, dict,
                                                      GF_TASK_TYPE_REBALANCE);
                if (ret)
                        goto out;
        }

        /* </volRebalance> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);


        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_remove_brick (gf_boolean_t status_op, dict_t *dict,
                                 int op_ret, int op_errno, char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *task_id_str = NULL;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volRemoveBrick> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volRemoveBrick");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, GF_REMOVE_BRICK_TID_KEY, &task_id_str);
        if (ret == 0) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"task-id",
                                                       "%s", task_id_str);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        if (status_op) {
                ret = cli_xml_output_vol_rebalance_status (writer, dict,
                                                   GF_TASK_TYPE_REMOVE_BRICK);
                if (ret)
                        goto out;
        }

        /* </volRemoveBrick> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);


        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_replace_brick (gf1_cli_replace_op op, dict_t *dict,
                                  int op_ret, int op_errno, char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        int                     status = 0;
        uint64_t                files = 0;
        char                    *current_file = 0;
        char                    *task_id_str = NULL;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <volReplaceBrick> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volReplaceBrick");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, GF_REPLACE_BRICK_TID_KEY, &task_id_str);
        if (ret == 0) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"task-id",
                                                       "%s", task_id_str);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"op",
                                               "%d", op);
        XML_RET_CHECK_AND_GOTO (ret, out);

        if (GF_REPLACE_OP_STATUS == op) {
                ret = dict_get_int32 (dict, "status", &status);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"status",
                                                       "%d", status);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = dict_get_uint64 (dict, "files", &files);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"files",
                                                       "%"PRIu64, files);
                XML_RET_CHECK_AND_GOTO (ret, out);

                if (status)
                        goto cont;

                ret = dict_get_str (dict, "current_file", &current_file);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"currentFile",
                                                       "%s", current_file);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }
cont:
        /* </volReplaceBrick> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_create (dict_t *dict, int op_ret, int op_errno,
                           char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *volname = NULL;
        char                    *volid = NULL;

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        if (dict) {
                /* <volCreate> */
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"volCreate");
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* <volume> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"volume");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = dict_get_str (dict, "volname", &volname);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *) "name",
                                                       "%s", volname);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = dict_get_str (dict, "volume-id", &volid);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"id",
                                                       "%s", volid);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </volume> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </volCreate> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_generic_volume (char *op, dict_t *dict, int op_ret, int op_errno,
                               char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *volname = NULL;
        char                    *volid = NULL;

        GF_ASSERT (op);

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        if (dict) {
                /* <"op"> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)op);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* <volume> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"volume");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = dict_get_str (dict, "volname", &volname);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *) "name",
                                                       "%s", volname);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = dict_get_str (dict, "vol-id", &volid);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"id",
                                                       "%s", volid);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </volume> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </"op"> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
int
cli_xml_output_vol_gsync_status (dict_t *dict, xmlTextWriterPtr writer)
{
        char                    master_key[PATH_MAX] = "";
        char                    slave_key[PATH_MAX] = "";
        char                    status_key[PATH_MAX] = "";
        char                    node_key[PATH_MAX] = "";
        char                    *master = NULL;
        char                    *slave = NULL;
        char                    *status = NULL;
        char                    *node = NULL;
        int                     ret = -1;
        int                     gsync_count = 0;
        int                     i = 1;

        ret = dict_get_int32 (dict, "gsync-count", &gsync_count);
        if (ret)
                goto out;

        for (i=1; i <= gsync_count; i++) {
                snprintf (node_key, sizeof(node_key), "node%d", i);
                snprintf (master_key, sizeof(master_key), "master%d", i);
                snprintf (slave_key, sizeof(slave_key), "slave%d", i);
                snprintf (status_key, sizeof(status_key), "status%d", i);

                ret = dict_get_str (dict, node_key, &node);
                if (ret)
                        goto out;

                ret = dict_get_str (dict, master_key, &master);
                if (ret)
                        goto out;

                ret = dict_get_str (dict, slave_key, &slave);
                if (ret)
                        goto out;

                ret = dict_get_str (dict, status_key, &status);
                if (ret)
                        goto out;

                /* <pair> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"pair");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"node",
                                                       "%s", node);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"master",
                                                       "%s", master);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"slave",
                                                       "%s", slave);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"status",
                                                       "%s", status);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </pair> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

        }

out:
        gf_log ("cli",GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

int
cli_xml_output_vol_gsync (dict_t *dict, int op_ret, int op_errno,
                          char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *master = NULL;
        char                    *slave = NULL;
        int                     type = 0;

        GF_ASSERT (dict);

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        /* <geoRep> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"geoRep");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get type");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"type",
                                               "%d", type);
        XML_RET_CHECK_AND_GOTO (ret, out);

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
        case GF_GSYNC_OPTION_TYPE_STOP:
                if (dict_get_str (dict, "master", &master) != 0)
                        master = "???";
                if (dict_get_str (dict, "slave", &slave) != 0)
                        slave = "???";

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"master",
                                                       "%s", master);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"slave",
                                                       "%s", slave);
                XML_RET_CHECK_AND_GOTO (ret, out);

                break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
                break;
        case GF_GSYNC_OPTION_TYPE_STATUS:
                ret = cli_xml_output_vol_gsync_status(dict, writer);
                break;
        default:
                ret = 0;
                break;
        }

        /* </geoRep> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_end_xml_output (writer, doc);
out:
        gf_log ("cli",GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}
