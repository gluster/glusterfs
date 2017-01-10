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
#include "upcall-utils.h"

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

        if (op_errstr)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"opErrstr",
                                                       "%s", op_errstr);
        else
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"opErrstr",
                                                       "%s", "");

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
        int             ret             = -1;
        char            *hostname       = NULL;
        char            *path           = NULL;
        char            *uuid           = NULL;
        int             port            = 0;
        int             rdma_port       = 0;
        int             status          = 0;
        int             pid             = 0;
        char            key[1024]       = {0,};

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

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.rdma_port", brick_index);
        ret = dict_get_int32 (dict, key, &rdma_port);

        /* If the process is either offline or doesn't provide a port (shd)
         * port = "N/A"
         * else print the port number of the process.
         */

        /*
         * Tag 'port' can be removed once console management is started
         * to support new tag ports.
         */

        if (*online == 1 && port != 0)
                 ret = xmlTextWriterWriteFormatElement (writer,
                                                    (xmlChar *)"port",
                                                        "%d", port);
         else
                 ret = xmlTextWriterWriteFormatElement (writer,
                                                        (xmlChar *)"port",
                                                        "%s", "N/A");

        ret = xmlTextWriterStartElement (writer, (xmlChar *)"ports");
        if (*online == 1 && (port != 0 || rdma_port != 0)) {

                if (port) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"tcp",
                                                       "%d", port);
                } else {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"tcp",
                                                       "%s", "N/A");
                }

                if (rdma_port) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"rdma",
                                                       "%d", rdma_port);
                } else {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"rdma",
                                                       "%s", "N/A");
                }

        } else {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"tcp",
                                                       "%s", "N/A");
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"rdma",
                                                       "%s", "N/A");
        }

        ret = xmlTextWriterEndElement (writer);
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
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"sizeTotal",
                                                       "%"PRIu64, size_total);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.free", brick_index);
        ret = dict_get_uint64 (dict, key, &size_free);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"sizeFree",
                                                       "%"PRIu64, size_free);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.device", brick_index);
        ret = dict_get_str (dict, key, &device);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"device",
                                                       "%s", device);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.block_size", brick_index);
        ret = dict_get_uint64 (dict, key, &block_size);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"blockSize",
                                                       "%"PRIu64, block_size);
        XML_RET_CHECK_AND_GOTO (ret, out);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.mnt_options", brick_index);
        ret = dict_get_str (dict, key, &mnt_options);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"mntOptions",
                                                       "%s", mnt_options);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.fs_name", brick_index);
        ret = dict_get_str (dict, key, &fs_name);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"fsName",
                                                       "%s", fs_name);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.inode_size", brick_index);
        ret = dict_get_str (dict, key, &inode_size);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"inodeSize",
                                                       "%s", fs_name);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.total_inodes", brick_index);
        ret = dict_get_uint64 (dict, key, &inodes_total);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"inodesTotal",
                                                       "%"PRIu64, inodes_total);

        memset (key, 0, sizeof (key));
        snprintf (key, sizeof (key), "brick%d.free_inodes", brick_index);
        ret = dict_get_uint64 (dict, key, &inodes_free);
        if (!ret)
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"inodesFree",
                                                       "%"PRIu64, inodes_free);
	else
		ret = 0;

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
        uint32_t        opversion = 0;
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

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "brick%d.client%d.opversion",
                          brick_index, i);
                ret = dict_get_uint32 (dict, key, &opversion);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"opVersion",
                                                       "%"PRIu32, opversion);
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

                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"statusStr",
                                                       "%s",
                                             cli_vol_task_status_str[status]);

                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "task%d", i);
                if (!strcmp (task_type, "Remove brick")) {
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
        int                     type            = -1;
        int                     hot_brick_count = -1;

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

        ret = dict_get_int32 (dict, "type", &type);
        if (ret)
                goto out;

        if (type == GF_CLUSTER_TYPE_TIER) {
                ret = dict_get_int32 (dict, "hot_brick_count",
                                      &hot_brick_count);
                if (ret)
                        goto out;

                ret = xmlTextWriterStartElement
                        (local->writer, (xmlChar *)"hotBricks");
                XML_RET_CHECK_AND_GOTO (ret, out);

        }
        for (i = 0; i <= index_max; i++) {

                if (type == GF_CLUSTER_TYPE_TIER && i == hot_brick_count) {

                        /* </hotBricks>*/
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                        ret = xmlTextWriterStartElement (local->writer,
                                        (xmlChar *)"coldBricks");
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }
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

                /* </coldBricks>*/
                if (type == GF_CLUSTER_TYPE_TIER && i == brick_index_max) {
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }
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

        for (i = 0; i < GF_UPCALL_FLAGS_MAXVALUE; i++) {
                hits = 0;
                avg_latency = 0.0;
                min_latency = 0.0;
                max_latency = 0.0;

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%d-%d-%d-upcall-hits", brick_index,
                          interval, i);
                ret = dict_get_uint64 (dict, key, &hits);
                if (ret)
                        continue;

                /* <fop> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"fop");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"name", "%s", gf_fop_list[i]);
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
        int                     arbiter_count = 0;
        int                     snap_count    = 0;
        int                     isArbiter = 0;
        int                     disperse_count = 0;
        int                     redundancy_count = 0;
        int                     transport = 0;
        char                    *brick = NULL;
        char                    key[1024] = {0,};
        int                     i = 0;
        int                     j = 1;
        char                    *caps __attribute__((unused)) = NULL;
        int                     k __attribute__((unused)) = 0;
        int                     index = 1;
        int                     tier_vol_type          = 0;
        /* hot dist count is always zero so need for it to be
         * included in the array.*/
        int                     hot_dist_count         = 0;
        values                  c                      = 0;
        char                    *keys[MAX]              = {
                [COLD_BRICK_COUNT]      = "volume%d.cold_brick_count",
                [COLD_TYPE]             = "volume%d.cold_type",
                [COLD_DIST_COUNT]       = "volume%d.cold_dist_count",
                [COLD_REPLICA_COUNT]    = "volume%d.cold_replica_count",
                [COLD_ARBITER_COUNT]    = "volume%d.cold_arbiter_count",
                [COLD_DISPERSE_COUNT]   = "volume%d.cold_disperse_count",
                [COLD_REDUNDANCY_COUNT] = "volume%d.cold_redundancy_count",
                [HOT_BRICK_COUNT]       = "volume%d.hot_brick_count",
                [HOT_TYPE]              = "volume%d.hot_type",
                [HOT_REPLICA_COUNT]     = "volume%d.hot_replica_count"};
        int                     value[MAX]             = {};


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
                snprintf (key, sizeof (key), "volume%d.snap_count", i);
                ret = dict_get_int32 (dict, key, &snap_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                     (xmlChar *)"snapshotCount",
                                                     "%d", snap_count);
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
                snprintf (key, sizeof (key), "volume%d.arbiter_count", i);
                ret = dict_get_int32 (dict, key, &arbiter_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                      (xmlChar *)"arbiterCount",
                                                      "%d", arbiter_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.disperse_count", i);
                ret = dict_get_int32 (dict, key, &disperse_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"disperseCount",
                                                       "%d", disperse_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.redundancy_count", i);
                ret = dict_get_int32 (dict, key, &redundancy_count);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"redundancyCount",
                                                       "%d", redundancy_count);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "volume%d.type", i);
                ret = dict_get_int32 (dict, key, &type);
                if (ret)
                        goto out;
                /* For Distributed-(stripe,replicate,stipe-replicate,disperse)
                   types
                 */
                type = get_vol_type (type, dist_count, brick_count);

                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"type",
                                                       "%d", type);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (local->writer,
                                                       (xmlChar *)"typeStr",
                                                       "%s",
                                                       vol_type_str[type]);
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
#endif
                j = 1;

                /* <bricks> */
                ret = xmlTextWriterStartElement (local->writer,
                                                 (xmlChar *)"bricks");
                XML_RET_CHECK_AND_GOTO (ret, out);

                if (type == GF_CLUSTER_TYPE_TIER) {
                        /*the values for hot stripe, disperse and redundancy
                         * should not be looped in here as they are zero
                         * always */
                        for (c = COLD_BRICK_COUNT; c < MAX; c++) {

                                memset (key, 0, sizeof (key));
                                snprintf (key, 256, keys[c], i);
                                ret = dict_get_int32 (dict, key, &value[c]);
                                if (ret)
                                        goto out;
                        }

                        hot_dist_count = (value[HOT_REPLICA_COUNT] ?
                                          value[HOT_REPLICA_COUNT] : 1);

                        tier_vol_type = get_vol_type (value[HOT_TYPE],
                                                      hot_dist_count,
                                                      value[HOT_BRICK_COUNT]);

                        if ((value[HOT_TYPE] != GF_CLUSTER_TYPE_TIER) &&
                            (value[HOT_TYPE] > 0) &&
                            (hot_dist_count < value[HOT_BRICK_COUNT]))
                                tier_vol_type = value[HOT_TYPE] +
                                        GF_CLUSTER_TYPE_MAX - 1;

                        ret = xmlTextWriterStartElement (local->writer,
                                                         (xmlChar *)
                                                         "hotBricks");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement
                                (local->writer, (xmlChar *)"hotBrickType",
                                 "%s", vol_type_str[tier_vol_type]);

                        ret = xmlTextWriterWriteFormatElement (local->writer,
                                                   (xmlChar *)"hotreplicaCount",
                                                    "%d",
                                                    value[HOT_REPLICA_COUNT]);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement (local->writer,
                                                   (xmlChar *)"hotbrickCount",
                                                    "%d",
                                                    value[HOT_BRICK_COUNT]);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        if (value[HOT_TYPE] == GF_CLUSTER_TYPE_NONE ||
                                        value[HOT_TYPE] ==
                                        GF_CLUSTER_TYPE_TIER) {
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer,
                                         (xmlChar *)"numberOfBricks",
                                         "%d", value[HOT_BRICK_COUNT]);
                                XML_RET_CHECK_AND_GOTO (ret, out);
                        } else {
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer,
                                         (xmlChar *)"numberOfBricks",
                                         "%d x %d = %d",
                                         (value[HOT_BRICK_COUNT] /
                                          hot_dist_count),
                                         hot_dist_count,
                                         value[HOT_BRICK_COUNT]);
                        }

                        while (index <= value[HOT_BRICK_COUNT]) {
                                snprintf (key, 1024, "volume%d.brick%d", i,
                                          index);
                                ret = dict_get_str (dict, key, &brick);
                                if (ret)
                                        goto out;

                                ret = xmlTextWriterStartElement
                                        (local->writer, (xmlChar *)"brick");
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.brick%d.uuid", i, j);
                                ret = dict_get_str (dict, key, &uuid);
                                if (ret)
                                        goto out;
                                ret = xmlTextWriterWriteFormatAttribute
                                        (local->writer, (xmlChar *)"uuid", "%s",
                                         uuid);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatString
                                        (local->writer, "%s", brick);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"name", "%s",
                                         brick);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"hostUuid",
                                         "%s", uuid);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterEndElement (local->writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                index++;
                        }
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        tier_vol_type = get_vol_type (value[COLD_TYPE],
                                                      value[COLD_DIST_COUNT],
                                                      value[COLD_BRICK_COUNT]);

                        ret = xmlTextWriterStartElement (local->writer,
                                                         (xmlChar *)
                                                         "coldBricks");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement
                                (local->writer, (xmlChar *)"coldBrickType",
                                 "%s", vol_type_str[tier_vol_type]);

                        ret = xmlTextWriterWriteFormatElement (local->writer,
                                        (xmlChar *)"coldreplicaCount",
                                        "%d", value[COLD_REPLICA_COUNT]);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement (local->writer,
                                        (xmlChar *)"coldarbiterCount",
                                        "%d", value[COLD_ARBITER_COUNT]);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement (local->writer,
                                                   (xmlChar *)"coldbrickCount",
                                                    "%d",
                                                    value[COLD_BRICK_COUNT]);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterWriteFormatElement (local->writer,
                                              (xmlChar *)"colddisperseCount",
                                              "%d", value[COLD_DISPERSE_COUNT]);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        if (value[COLD_TYPE] == GF_CLUSTER_TYPE_NONE ||
                                        value[COLD_TYPE] ==
                                        GF_CLUSTER_TYPE_TIER) {
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer,
                                         (xmlChar *)"numberOfBricks",
                                         "%d", value[COLD_BRICK_COUNT]);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                        } else if (value[COLD_TYPE] ==
                                        GF_CLUSTER_TYPE_DISPERSE) {
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer,
                                         (xmlChar *)"numberOfBricks",
                                         " %d x (%d + %d) = %d",
                                         (value[COLD_BRICK_COUNT] /
                                          value[COLD_DIST_COUNT]),
                                         value[COLD_DISPERSE_COUNT] -
                                         value[COLD_REDUNDANCY_COUNT],
                                         value[COLD_REDUNDANCY_COUNT],
                                         value[COLD_BRICK_COUNT]);
                        } else {
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer,
                                         (xmlChar *)"numberOfBricks",
                                         "%d x %d = %d",
                                         (value[COLD_BRICK_COUNT] /
                                          value[COLD_DIST_COUNT]),
                                         value[COLD_DIST_COUNT],
                                         value[COLD_BRICK_COUNT]);
                        }

                        index = value[HOT_BRICK_COUNT] + 1;

                        while (index <= brick_count) {
                                snprintf (key, 1024, "volume%d.brick%d", i,
                                          index);
                                ret = dict_get_str (dict, key, &brick);
                                if (ret)
                                        goto out;

                                ret = xmlTextWriterStartElement
                                        (local->writer, (xmlChar *)"brick");
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.brick%d.uuid", i, j);
                                ret = dict_get_str (dict, key, &uuid);
                                if (ret)
                                        goto out;
                                ret = xmlTextWriterWriteFormatAttribute
                                        (local->writer, (xmlChar *)"uuid", "%s",
                                         uuid);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatString
                                        (local->writer, "%s", brick);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"name", "%s",
                                         brick);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"hostUuid",
                                         "%s", uuid);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.brick%d.isArbiter", i,
                                          index);
                                if (dict_get (dict, key))
                                        isArbiter = 1;
                                else
                                        isArbiter = 0;
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"isArbiter",
                                         "%d", isArbiter);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterEndElement (local->writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                index++;
                        }
                        ret = xmlTextWriterEndElement (local->writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                } else {
                        while (j <= brick_count) {
                                ret = xmlTextWriterStartElement
                                        (local->writer, (xmlChar *)"brick");
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.brick%d.uuid", i, j);
                                ret = dict_get_str (dict, key, &uuid);
                                if (ret)
                                        goto out;
                                ret = xmlTextWriterWriteFormatAttribute
                                        (local->writer, (xmlChar *)"uuid", "%s",
                                         uuid);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.brick%d", i, j);
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
                                        (local->writer, (xmlChar *)"hostUuid",
                                         "%s", uuid);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                memset (key, 0, sizeof (key));
                                snprintf (key, sizeof (key),
                                          "volume%d.brick%d.isArbiter", i, j);
                                if (dict_get (dict, key))
                                        isArbiter = 1;
                                else
                                        isArbiter = 0;
                                ret = xmlTextWriterWriteFormatElement
                                        (local->writer, (xmlChar *)"isArbiter",
                                         "%d", isArbiter);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                /* </brick> */
                                ret = xmlTextWriterEndElement (local->writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                j++;
                        }
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
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_quota_limit_list_end (cli_local_t *local)
{
#if (HAVE_LIB_XML)
        int     ret = -1;

        ret = xmlTextWriterEndElement (local->writer);
        if (ret) {
                goto out;
        }

        ret = cli_end_xml_output (local->writer, local->doc);

out:
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_vol_quota_limit_list_begin (cli_local_t *local, int op_ret,
                                           int op_errno, char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;

        ret = cli_begin_xml_output (&(local->writer), &(local->doc));
        if (ret)
                goto out;

        ret = cli_xml_output_common (local->writer, op_ret, op_errno,
                                     op_errstr);
        if (ret)
                goto out;

        /* <volQuota> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"volQuota");
        XML_RET_CHECK_AND_GOTO (ret, out);


out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

#if (HAVE_LIB_XML)
static int
cli_xml_output_peer_hostnames (xmlTextWriterPtr writer, dict_t *dict,
                               const char *prefix, int count)
{
        int   ret       = -1;
        int   i         = 0;
        char *hostname  = NULL;
        char  key[1024] = {0,};

        /* <hostnames> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"hostnames");
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 0; i < count; i++) {
                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "%s.hostname%d", prefix, i);
                ret = dict_get_str (dict, key, &hostname);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement
                        (writer, (xmlChar *)"hostname", "%s", hostname);
                XML_RET_CHECK_AND_GOTO (ret, out);
                hostname = NULL;
        }

        /* </hostnames> */
        ret = xmlTextWriterEndElement (writer);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

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
        int                     hostname_count = 0;
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
                snprintf (key, sizeof (key), "friend%d.hostname_count", i);
                ret = dict_get_int32 (dict, key, &hostname_count);
                if ((ret == 0) && (hostname_count > 0)) {
                        memset (key, 0, sizeof (key));
                        snprintf (key, sizeof (key), "friend%d", i);
                        ret = cli_xml_output_peer_hostnames (writer, dict, key,
                                                             hostname_count);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }

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

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "skipped-%d", i);

                ret = dict_get_uint64 (dict, key, &skipped);
                if (ret)
                        goto out;

                if (task_type == GF_TASK_TYPE_REMOVE_BRICK) {
                        failures += skipped;
                        skipped = 0;
                }

                total_failures += failures;
                ret = xmlTextWriterWriteFormatElement (writer,
                                               (xmlChar *)"failures",
                                               "%"PRIu64, failures);
                XML_RET_CHECK_AND_GOTO (ret, out);

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
cli_xml_output_vol_tier_status (xmlTextWriterPtr writer, dict_t *dict,
                enum gf_task_types task_type)
{
#if (HAVE_LIB_XML)

        int                     ret = -1;
        int                     count = 0;
        char                    *node_name = NULL;
        char                    *status_str  = NULL;
        uint64_t                promoted     = 0;
        uint64_t                demoted      = 0;
        int                     i = 1;
        char                    key[1024] = {0,};
        gf_defrag_status_t      status_rcd   = GF_DEFRAG_STATUS_NOT_STARTED;


        GF_VALIDATE_OR_GOTO ("cli", dict, out);

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "count not set");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"nodeCount",
                                               "%d", count);
        XML_RET_CHECK_AND_GOTO (ret, out);

        while (i <= count) {
                promoted = 0;
                node_name = NULL;
                demoted = 0;

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
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "promoted-%d", i);
                ret = dict_get_uint64 (dict, key, &promoted);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"promoted"
                                                       "Files", "%"PRIu64,
                                                       promoted);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, sizeof (key));
                snprintf (key, sizeof (key), "demoted-%d", i);
                ret = dict_get_uint64 (dict, key, &demoted);
                if (ret)
                        goto out;
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"demoted"
                                                       "Files", "%"PRIu64,
                                                       demoted);
                XML_RET_CHECK_AND_GOTO (ret, out);

                memset (key, 0, 256);
                snprintf (key, 256, "status-%d", i);
                ret = dict_get_int32 (dict, key, (int32_t *)&status_rcd);

                status_str = cli_vol_task_status_str[status_rcd];

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"statusStr",
                                                       "%s", status_str);
                XML_RET_CHECK_AND_GOTO (ret, out);


                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                i++;
        }

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

#else
        return 0;

#endif
}




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

        if (GF_DEFRAG_CMD_STATUS_TIER == op) {
                ret = cli_xml_output_vol_tier_status (writer,
                                dict, GF_TASK_TYPE_REBALANCE);
                if (ret)
                        goto out;
                }
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
cli_xml_output_vol_remove_brick_detach_tier (gf_boolean_t status_op,
                                             dict_t *dict, int op_ret,
                                             int op_errno, char *op_errstr,
                                             const char *op)
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

        ret = xmlTextWriterStartElement (writer, (xmlChar *) op);
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
cli_xml_output_vol_replace_brick (dict_t *dict,
                                  int op_ret, int op_errno, char *op_errstr)
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
_output_gsync_config (FILE *fp, xmlTextWriterPtr writer, char *op_name)
{
        char  resbuf[256 + PATH_MAX] = {0,};
        char *ptr                    = NULL;
        char *v                      = NULL;
        int   blen                   = sizeof(resbuf);
        int   ret                    = 0;

        for (;;) {
                ptr = fgets (resbuf, blen, fp);
                if (!ptr)
                        break;

                v = resbuf + strlen (resbuf) - 1;
                while (isspace (*v)) {
                        /* strip trailing space */
                        *v-- = '\0';
                }
                if (v == resbuf) {
                        /* skip empty line */
                        continue;
                }

                if (op_name!= NULL){
                        ret = xmlTextWriterWriteFormatElement (writer,
                                                        (xmlChar *)op_name,
                                                        "%s", resbuf);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                        goto out;
                }

                v = strchr (resbuf, ':');
                if (!v) {
                        ret = -1;
                        goto out;
                }
                *v++ = '\0';
                while (isspace (*v))
                        v++;
                v = gf_strdup (v);
                if (!v) {
                        ret = -1;
                        goto out;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)resbuf,
                                                       "%s", v);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }
out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

#if (HAVE_LIB_XML)
int
get_gsync_config (runner_t *runner,
                  int (*op_conf)(FILE *fp,
                              xmlTextWriterPtr writer,
                              char *op_name),
                  xmlTextWriterPtr writer, char *op_name)
{
        int ret = 0;

        runner_redir (runner, STDOUT_FILENO, RUN_PIPE);
        if (runner_start (runner) != 0) {
                gf_log ("cli", GF_LOG_ERROR, "spawning child failed");
                return -1;
        }

        ret = op_conf (runner_chio (runner, STDOUT_FILENO), writer, op_name);

        ret |= runner_end (runner);
        if (ret)
                gf_log ("cli", GF_LOG_ERROR, "reading data from child failed");

        return ret ? -1 : 0;
}
#endif

#if (HAVE_LIB_XML)
int
cli_xml_generate_gsync_config (dict_t *dict, xmlTextWriterPtr writer)
{
        runner_t runner           = {0,};
        char *subop               = NULL;
        char *gwd                 = NULL;
        char *slave               = NULL;
        char *confpath            = NULL;
        char *master              = NULL;
        char *op_name             = NULL;
        int   ret                 = -1;
        char  conf_path[PATH_MAX] = "";

        if (dict_get_str (dict, "subop", &subop) != 0) {
                ret = -1;
                goto out;
        }

        if (strcmp (subop, "get") != 0 && strcmp (subop, "get-all") != 0) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                 (xmlChar *)"message",
                                 "%s",GEOREP" config updated successfully" );
                XML_RET_CHECK_AND_GOTO (ret, out);
                ret = 0;
                goto out;
        }

        if (dict_get_str (dict, "glusterd_workdir", &gwd) != 0 ||
            dict_get_str (dict, "slave", &slave) != 0) {
                ret = -1;
                goto out;
        }

        if (dict_get_str (dict, "master", &master) != 0)
                master = NULL;

        if (dict_get_str (dict, "op_name", &op_name) != 0)
                op_name = NULL;

        ret = dict_get_str (dict, "conf_path", &confpath);
        if (!confpath) {
                ret = snprintf (conf_path, sizeof (conf_path) - 1,
                                "%s/"GEOREP"/gsyncd_template.conf", gwd);
                conf_path[ret] = '\0';
                confpath = conf_path;
        }

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s", confpath);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);

        if (master)
                runner_argprintf (&runner, ":%s", master);

        runner_add_arg (&runner, slave);
        runner_argprintf (&runner, "--config-%s", subop);

        if (op_name)
                runner_add_arg (&runner, op_name);

        ret =  get_gsync_config (&runner, _output_gsync_config,
                                 writer, op_name);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
#endif

#if (HAVE_LIB_XML)
int
cli_xml_output_vol_gsync_status (dict_t *dict,
                                 xmlTextWriterPtr writer)
{
        int                  ret                         = -1;
        int                  i                           = 1;
        int                  j                           = 0;
        int                  count                       = 0;
        const int            number_of_fields            = 20;
        int                  closed                      = 1;
        int                  session_closed              = 1;
        gf_gsync_status_t  **status_values               = NULL;
        char                 status_value_name[PATH_MAX] = "";
        char                *tmp                         = NULL;
        char                *volume                      = NULL;
        char                *volume_next                 = NULL;
        char                *slave                       = NULL;
        char                *slave_next                  = NULL;
        char                *title_values[]              = {"master_node",
                                                            "",
                                                            "master_brick",
                                                            "slave_user",
                                                            "slave",
                                                            "slave_node",
                                                            "status",
                                                            "crawl_status",
                                                            /* last_synced */
                                                            "",
                                                            "entry",
                                                            "data",
                                                            "meta",
                                                            "failures",
                                                           /* checkpoint_time */
                                                            "",
                                                         "checkpoint_completed",
                                               /* checkpoint_completion_time */
                                                            "",
                                                            "master_node_uuid",
                                                           /* last_synced_utc */
                                                            "last_synced",
                                                       /* checkpoint_time_utc */
                                                            "checkpoint_time",
                                            /* checkpoint_completion_time_utc */
                                                 "checkpoint_completion_time"};

        GF_ASSERT (dict);

        ret = dict_get_int32 (dict, "gsync-count", &count);
        if (ret)
                goto out;

        status_values = GF_CALLOC (count, sizeof (gf_gsync_status_t *),
                              gf_common_mt_char);
        if (!status_values) {
                ret = -1;
                goto out;
        }

        for (i = 0; i < count; i++) {
                status_values[i] = GF_CALLOC (1, sizeof (gf_gsync_status_t),
                                         gf_common_mt_char);
                if (!status_values[i]) {
                        ret = -1;
                        goto out;
                }

                snprintf (status_value_name, sizeof (status_value_name),
                          "status_value%d", i);

                ret = dict_get_bin (dict, status_value_name,
                                    (void **)&(status_values[i]));
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "struct member empty.");
                        goto out;
                }
        }

        qsort(status_values, count, sizeof (gf_gsync_status_t *),
              gf_gsync_status_t_comparator);

        for (i = 0; i < count; i++) {
                if (closed) {
                        ret = xmlTextWriterStartElement (writer,
                                                         (xmlChar *)"volume");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        tmp = get_struct_variable (1, status_values[i]);
                        if (!tmp) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "struct member empty.");
                                ret = -1;
                                goto out;
                        }

                        ret = xmlTextWriterWriteFormatElement (writer,
                                                        (xmlChar *)"name",
                                                        "%s",tmp);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterStartElement (writer,
                                                    (xmlChar *)"sessions");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        closed = 0;
                }

                if (session_closed) {
                        ret = xmlTextWriterStartElement (writer,
                                                         (xmlChar *)"session");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        session_closed = 0;

                        tmp = get_struct_variable (21, status_values[i]);
                        if (!tmp) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "struct member empty.");
                                ret = -1;
                                goto out;
                        }

                        ret = xmlTextWriterWriteFormatElement
                                (writer, (xmlChar *)"session_slave", "%s", tmp);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }

                ret = xmlTextWriterStartElement (writer, (xmlChar *)"pair");
                XML_RET_CHECK_AND_GOTO (ret, out);

                for (j = 0; j < number_of_fields; j++) {
                        /* XML ignore fields */
                        if (strcmp(title_values[j], "") == 0)
                                continue;

                        tmp = get_struct_variable (j, status_values[i]);
                        if (!tmp) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "struct member empty.");
                                ret = -1;
                                goto out;
                        }

                        ret = xmlTextWriterWriteFormatElement (writer,
                                                  (xmlChar *)title_values[j],
                                                  "%s", tmp);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }

                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                if (i+1 < count) {
                        slave = get_struct_variable (20, status_values[i]);
                        slave_next = get_struct_variable (20,
                                                          status_values[i+1]);
                        volume = get_struct_variable (1, status_values[i]);
                        volume_next = get_struct_variable (1,
                                                           status_values[i+1]);
                        if (!slave || !slave_next || !volume || !volume_next) {
                                gf_log ("cli", GF_LOG_ERROR,
                                        "struct member empty.");
                                ret = -1;
                                goto out;
                        }

                        if (strcmp (volume, volume_next)!=0) {
                                closed = 1;
                                session_closed = 1;

                                ret = xmlTextWriterEndElement (writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterEndElement (writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);

                                ret = xmlTextWriterEndElement (writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);
                        } else if (strcmp (slave, slave_next)!=0) {

                                session_closed = 1;

                                ret = xmlTextWriterEndElement (writer);
                                XML_RET_CHECK_AND_GOTO (ret, out);
                        }
                } else {

                        ret = xmlTextWriterEndElement (writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterEndElement (writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = xmlTextWriterEndElement (writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }
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

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
        case GF_GSYNC_OPTION_TYPE_STOP:
        case GF_GSYNC_OPTION_TYPE_PAUSE:
        case GF_GSYNC_OPTION_TYPE_RESUME:
        case GF_GSYNC_OPTION_TYPE_CREATE:
        case GF_GSYNC_OPTION_TYPE_DELETE:
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
                if (op_ret == 0) {
                        ret = xmlTextWriterStartElement (writer, (xmlChar *)"config");
                        XML_RET_CHECK_AND_GOTO (ret, out);

                        ret = cli_xml_generate_gsync_config (dict, writer);
                        if (ret)
                                goto out;

                        ret = xmlTextWriterEndElement (writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }

                break;
        case GF_GSYNC_OPTION_TYPE_STATUS:
                ret = cli_xml_output_vol_gsync_status (dict, writer);
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

#if (HAVE_LIB_XML)
/* This function will generate snapshot create output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing create output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_create (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int     ret             = -1;
        char   *str_value       = NULL;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapCreate> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapCreate");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapshot> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshot");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapname", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapuuid", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapshot> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapCreate> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}

/* This function will generate snapshot clone output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing create output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_clone (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int     ret             = -1;
        char   *str_value       = NULL;

        GF_VALIDATE_OR_GOTO ("cli", writer, out);
        GF_VALIDATE_OR_GOTO ("cli", doc, out);
        GF_VALIDATE_OR_GOTO ("cli", dict, out);

        /* <CloneCreate> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"CloneCreate");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <volume> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volume");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "clonename", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get clone name");
                goto out;
        }
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);


        ret = dict_get_str (dict, "snapuuid", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get clone uuid");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </volume> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </CloneCreate> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}


/* This function will generate snapshot restore output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing restore output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_restore (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int     ret             = -1;
        char   *str_value       = NULL;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapRestore> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapRestore");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <volume> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volume");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "volname", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get vol name");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "volid", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get volume id");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </volume> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);


        /* <snapshot> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshot");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapname", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapuuid", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapshot> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapRestore> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:

        return ret;
}

/* This function will generate snapshot list output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing list output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_list (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int     ret             = -1;
        int     i               = 0;
        int     snapcount       = 0;
        char   *str_value       = NULL;
        char    key[PATH_MAX]   = "";

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapList> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapList");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "snapcount", &snapcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snapcount");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "count",
                                               "%d", snapcount);
        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i = 1; i <= snapcount; ++i) {
                ret = snprintf (key, sizeof (key), "snapname%d", i);
                if (ret < 0) {
                        goto out;
                }

                ret = dict_get_str (dict, key, &str_value);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not get %s ", key);
                        goto out;
                } else {
                        ret = xmlTextWriterWriteFormatElement (writer,
                                        (xmlChar *)"snapshot", "%s", str_value);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                }
        }

        /* </snapList> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:

        return ret;
}

/* This function will generate xml output for origin volume
 * of the given snapshot.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing info output
 * @param keyprefix     prefix for dictionary key
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_info_orig_vol (xmlTextWriterPtr writer, xmlDocPtr doc,
                                dict_t *dict, char *keyprefix)
{
        int     ret             = -1;
        int     value           = 0;
        char   *buffer          = NULL;
        char    key [PATH_MAX]  = "";

        GF_ASSERT (dict);
        GF_ASSERT (keyprefix);
        GF_ASSERT (writer);
        GF_ASSERT (doc);

        /* <originVolume> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"originVolume");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%sorigin-volname", keyprefix);

        ret = dict_get_str (dict, key, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_WARNING, "Failed to get %s", key);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%ssnapcount", keyprefix);

        ret = dict_get_int32 (dict, key, &value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get %s", key);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "snapCount",
                                               "%d", value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%ssnaps-available", keyprefix);

        ret = dict_get_int32 (dict, key, &value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get %s", key);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "snapRemaining", "%d", value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </originVolume> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}

/* This function will generate xml output of snapshot volume info.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing info output
 * @param keyprefix     key prefix for dictionary
 * @param snap_driven   boolean to check if output is based of volume
 *                      or snapshot
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_info_snap_vol (xmlTextWriterPtr writer, xmlDocPtr doc,
                                dict_t *dict, char *keyprefix,
                                gf_boolean_t snap_driven)
{
        char            key [PATH_MAX]          = "";
        char           *buffer                  = NULL;
        int             ret                     = -1;

        GF_ASSERT (dict);
        GF_ASSERT (keyprefix);
        GF_ASSERT (writer);
        GF_ASSERT (doc);

        /* <snapVolume> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapVolume");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.volname", keyprefix);

        ret = dict_get_str (dict, key, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get %s", key);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.vol-status", keyprefix);

        ret = dict_get_str (dict, key, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get %s", key);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "status",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* If the command is snap_driven then we need to show origin volume
         * info. Else this is shown in the start of info display.*/
        if (snap_driven) {
                snprintf (key, sizeof (key), "%s.", keyprefix);
                ret = cli_xml_snapshot_info_orig_vol (writer, doc, dict, key);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot's origin volume");
                        goto out;
                }
        }

        /* </snapVolume> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}

/* This function will generate snapshot info of individual snapshot
 * in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing info output
 * @param keyprefix     key prefix for dictionary
 * @param snap_driven   boolean to check if output is based of volume
 *                      or snapshot
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_info_per_snap (xmlTextWriterPtr writer, xmlDocPtr doc,
                                dict_t *dict, char *keyprefix,
                                gf_boolean_t snap_driven)
{
        char            key_buffer[PATH_MAX]    = "";
        char           *buffer                  = NULL;
        int             volcount                = 0;
        int             ret                     = -1;
        int             i                       = 0;

        GF_ASSERT (dict);
        GF_ASSERT (keyprefix);
        GF_ASSERT (writer);
        GF_ASSERT (doc);

        /* <snapshot> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshot");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key_buffer, sizeof (key_buffer), "%s.snapname",
                  keyprefix);

        ret = dict_get_str (dict, key_buffer, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to fetch snapname %s ",
                        key_buffer);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key_buffer, sizeof (key_buffer), "%s.snap-id", keyprefix);

        ret = dict_get_str (dict, key_buffer, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to fetch snap-id %s ",
                                key_buffer);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key_buffer, sizeof (key_buffer), "%s.snap-desc", keyprefix);

        ret = dict_get_str (dict, key_buffer, &buffer);
        if (!ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                (xmlChar *) "description",
                                                "%s", buffer);
        } else {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                (xmlChar *) "description",
                                                "%s", "");
        }
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key_buffer, sizeof (key_buffer), "%s.snap-time", keyprefix);

        ret = dict_get_str (dict, key_buffer, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to fetch snap-time %s ",
                                keyprefix);
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                                               (xmlChar *) "createTime",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key_buffer, sizeof (key_buffer), "%s.vol-count", keyprefix);
        ret = dict_get_int32 (dict, key_buffer, &volcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Fail to get snap vol count");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                                               (xmlChar *) "volCount",
                                               "%d", volcount);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, key_buffer, &volcount);
        /* Display info of each snapshot volume */
        for (i = 1 ; i <= volcount ; i++) {
                snprintf (key_buffer, sizeof (key_buffer), "%s.vol%d",
                          keyprefix, i);

                ret = cli_xml_snapshot_info_snap_vol (writer, doc, dict,
                                                      key_buffer, snap_driven);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not list "
                                        "details of volume in a snap");
                        goto out;
                }
        }

        /* </snapshot> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);
out:
        return ret;
}

/* This function will generate snapshot info output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing info output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_info (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int             ret             = -1;
        int             i               = 0;
        int             snapcount       = 0;
        char            key [PATH_MAX]  = "";
        gf_boolean_t    snap_driven     = _gf_false;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapInfo> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapInfo");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snap_driven = dict_get_str_boolean (dict, "snap-driven", _gf_false);

        /* If the approach is volume based then we should display orgin volume
         * information first followed by per snap info*/
        if (!snap_driven) {
                ret = cli_xml_snapshot_info_orig_vol (writer, doc, dict, "");
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot's origin volume");
                        goto out;
                }
        }

        ret = dict_get_int32 (dict, "snapcount", &snapcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snapcount");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "count",
                                               "%d", snapcount);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapshots> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshots");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* Get snapshot info of individual snapshots */
        for (i = 1; i <= snapcount; ++i) {
                snprintf (key, sizeof (key), "snap%d", i);

                ret = cli_xml_snapshot_info_per_snap (writer, doc, dict,
                                                      key, snap_driven);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not get %s ", key);
                        goto out;
                }
        }

        /* </snapshots> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapInfo> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:

        return ret;
}

/* This function will generate snapshot status of individual
 * snapshot volume in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing status output
 * @param keyprefix     key prefix for dictionary
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_volume_status (xmlTextWriterPtr writer, xmlDocPtr doc,
                                dict_t *dict, const char *keyprefix)
{
        int     ret             = -1;
        int     brickcount      = 0;
        int     i               = 0;
        int     pid             = 0;
        char   *buffer          = NULL;
        char    key[PATH_MAX]   = "";

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);
        GF_ASSERT (keyprefix);

        snprintf (key, sizeof (key), "%s.brickcount", keyprefix);

        ret = dict_get_int32 (dict, key, &brickcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to fetch brickcount");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "brickCount",
                                               "%d", brickcount);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* Get status of every brick belonging to the snapshot volume */
        for (i = 0 ; i < brickcount ; i++) {
                /* <snapInfo> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"brick");
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.brick%d.path", keyprefix, i);

                ret = dict_get_str (dict, key, &buffer);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Unable to get Brick Path");
                        /*
                         * If path itself is not present, then end *
                         * this brick's status and continue to the *
                         * brick                                   *
                         */
                        ret = xmlTextWriterEndElement (writer);
                        XML_RET_CHECK_AND_GOTO (ret, out);
                        continue;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                        (xmlChar *) "path", "%s", buffer);
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.brick%d.vgname",
                          keyprefix, i);

                ret = dict_get_str (dict, key, &buffer);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                "Unable to get Volume Group");
                        ret = xmlTextWriterWriteFormatElement (writer,
                                              (xmlChar *) "volumeGroup", "N/A");
                } else
                        ret = xmlTextWriterWriteFormatElement (writer,
                                       (xmlChar *) "volumeGroup", "%s", buffer);

                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.brick%d.status", keyprefix, i);

                ret = dict_get_str (dict, key, &buffer);
                if (ret) {
                        gf_log ("cli", GF_LOG_INFO,
                                "Unable to get Brick Running");
                        ret = xmlTextWriterWriteFormatElement (writer,
                                            (xmlChar *) "brick_running", "N/A");
                } else
                        ret = xmlTextWriterWriteFormatElement (writer,
                                     (xmlChar *) "brick_running", "%s", buffer);

                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.brick%d.pid", keyprefix, i);

                ret = dict_get_int32 (dict, key, &pid);
                if (ret) {
                        gf_log ("cli", GF_LOG_INFO, "Unable to get pid");
                        ret = xmlTextWriterWriteFormatElement (writer,
                                                      (xmlChar *) "pid", "N/A");
                } else
                        ret = xmlTextWriterWriteFormatElement (writer,
                                                  (xmlChar *) "pid", "%d", pid);

                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.brick%d.data", keyprefix, i);

                ret = dict_get_str (dict, key, &buffer);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                        "Unable to get Data Percent");
                        ret = xmlTextWriterWriteFormatElement (writer,
                                          (xmlChar *) "data_percentage", "N/A");
                } else
                        ret = xmlTextWriterWriteFormatElement (writer,
                                   (xmlChar *) "data_percentage", "%s", buffer);

                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.brick%d.lvsize",
                          keyprefix, i);
                ret = dict_get_str (dict, key, &buffer);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Unable to get LV Size");
                        ret = xmlTextWriterWriteFormatElement (writer,
                                                 (xmlChar *) "lvSize", "N/A");
                } else {
                        /* Truncate any newline character */
                        buffer = strtok (buffer, "\n");

                        ret = xmlTextWriterWriteFormatElement (writer,
                                          (xmlChar *) "lvSize", "%s", buffer);
                }

                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </brick> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

out:
        return ret;
}

/* This function will generate snapshot status of individual
 * snapshot in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing status output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_status_per_snap (xmlTextWriterPtr writer, xmlDocPtr doc,
                                  dict_t *dict, const char *keyprefix)
{
        int     ret             = -1;
        int     volcount        = 0;
        int     i               = 0;
        char   *buffer          = NULL;
        char    key [PATH_MAX]  = "";

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);
        GF_ASSERT (keyprefix);

        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshot");
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.snapname", keyprefix);

        ret = dict_get_str (dict, key, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to get snapname");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.uuid", keyprefix);

        ret = dict_get_str (dict, key, &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to get snap UUID");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        snprintf (key, sizeof (key), "%s.volcount", keyprefix);

        ret = dict_get_int32 (dict, key, &volcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Unable to get volume count");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "volCount",
                                               "%d", volcount);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* Get snapshot status of individual snapshot volume */
        for (i = 0 ; i < volcount ; i++) {
                /* <volume> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"volume");
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (key, sizeof (key), "%s.vol%d", keyprefix, i);

                ret = cli_xml_snapshot_volume_status (writer, doc,
                                                             dict, key);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR,
                                        "Could not get snap volume status");
                        goto out;
                }

                /* </volume> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </snapshot> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}

/* This function will generate snapshot status output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing status output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_status (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int     ret             = -1;
        int     snapcount       = 0;
        int     i               = 0;
        int     status_cmd      = 0;
        char    key [PATH_MAX]  = "";

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapStatus> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "sub-cmd", &status_cmd);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not fetch status type");
                goto out;
        }

        if ((GF_SNAP_STATUS_TYPE_SNAP == status_cmd) ||
            (GF_SNAP_STATUS_TYPE_ITER == status_cmd)) {
                snapcount = 1;
        } else {
                ret = dict_get_int32 (dict, "status.snapcount", &snapcount);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not get snapcount");
                        goto out;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *) "count",
                                                       "%d", snapcount);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        for (i = 0 ; i < snapcount; i++) {
                snprintf (key, sizeof (key), "status.snap%d", i);

                ret = cli_xml_snapshot_status_per_snap (writer, doc,
                                                               dict, key);
                if (ret < 0) {
                        gf_log ("cli", GF_LOG_ERROR, "failed to create xml "
                                "output for snapshot status");
                        goto out;
                }
        }

        /* </snapStatus> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:

        return ret;
}

/* This function will generate snapshot config show output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing status output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_config_show (xmlTextWriterPtr writer,
                              xmlDocPtr doc, dict_t *dict)
{
        int             ret             = -1;
        uint64_t        i               = 0;
        uint64_t        value           = 0;
        uint64_t        volcount        = 0;
        char            buf[PATH_MAX]   = "";
        char           *str_value       = NULL;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <systemConfig> */
        ret = xmlTextWriterStartElement (writer,
                        (xmlChar *)"systemConfig");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_uint64 (dict, "snap-max-hard-limit", &value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get "
                        "snap-max-hard-limit");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                        (xmlChar *) "hardLimit", "%"PRIu64, value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_uint64 (dict, "snap-max-soft-limit", &value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get "
                        "snap-max-soft-limit");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                        (xmlChar *) "softLimit",
                        "%"PRIu64"%%", value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "auto-delete", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not fetch auto-delete");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                        (xmlChar *) "autoDelete", "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snap-activate-on-create", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR,
                        "Could not fetch snap-activate-on-create-delete");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer,
                        (xmlChar *) "activateOnCreate", "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </systemConfig> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <volumeConfig> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volumeConfig");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_uint64 (dict, "voldisplaycount", &volcount);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not fetch volcount");
                goto out;
        }

        /* Get config of all the volumes */
        for (i = 0; i < volcount; i++) {
                /* <volume> */
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"volume");
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (buf, sizeof(buf), "volume%"PRIu64"-volname", i);
                ret = dict_get_str (dict, buf, &str_value);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not fetch %s", buf);
                        goto out;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "name", "%s", str_value);
                XML_RET_CHECK_AND_GOTO (ret, out);


                snprintf (buf, sizeof(buf),
                                "volume%"PRIu64"-snap-max-hard-limit", i);
                ret = dict_get_uint64 (dict, buf, &value);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not fetch %s", buf);
                        goto out;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "hardLimit", "%"PRIu64, value);
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (buf, sizeof(buf),
                                "volume%"PRIu64"-active-hard-limit", i);
                ret = dict_get_uint64 (dict, buf, &value);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not fetch"
                                " effective snap_max_hard_limit for "
                                "%s", str_value);
                        goto out;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "effectiveHardLimit",
                                "%"PRIu64, value);
                XML_RET_CHECK_AND_GOTO (ret, out);

                snprintf (buf, sizeof(buf),
                                "volume%"PRIu64"-snap-max-soft-limit", i);
                ret = dict_get_uint64 (dict, buf, &value);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Could not fetch %s", buf);
                        goto out;
                }

                ret = xmlTextWriterWriteFormatElement (writer,
                                        (xmlChar *) "softLimit",
                                        "%"PRIu64, value);
                XML_RET_CHECK_AND_GOTO (ret, out);

                /* </volume> */
                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </volume> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}

/* This function will generate snapshot config set output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing status output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_config_set (xmlTextWriterPtr writer, xmlDocPtr doc,
                             dict_t *dict)
{
        int             ret             = -1;
        uint64_t        hard_limit      = 0;
        uint64_t        soft_limit      = 0;
        char           *volname         = NULL;
        char           *auto_delete     = NULL;
        char           *snap_activate   = NULL;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* This is optional parameter therefore ignore the error */
        ret = dict_get_uint64 (dict, "snap-max-hard-limit", &hard_limit);
        /* This is optional parameter therefore ignore the error */
        ret = dict_get_uint64 (dict, "snap-max-soft-limit", &soft_limit);
        ret = dict_get_str (dict, "auto-delete", &auto_delete);
        ret = dict_get_str (dict, "snap-activate-on-create", &snap_activate);

        if (!hard_limit && !soft_limit && !auto_delete && !snap_activate) {
                ret = -1;
                gf_log ("cli", GF_LOG_ERROR, "At least one option from "
                        "snap-max-hard-limit, snap-max-soft-limit, auto-delete"
                        " and snap-activate-on-create should be set");
                goto out;
        }

        /* Ignore the error, as volname is optional */
        ret = dict_get_str (dict, "volname", &volname);

        if (NULL == volname) {
                /* <systemConfig> */
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"systemConfig");
        } else {
                /* <volumeConfig> */
                ret = xmlTextWriterStartElement (writer,
                                                 (xmlChar *)"volumeConfig");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "name", "%s", volname);
        }

        XML_RET_CHECK_AND_GOTO (ret, out);

        if (hard_limit) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                (xmlChar *) "newHardLimit",
                                                "%"PRIu64, hard_limit);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        if (soft_limit) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "newSoftLimit",
                                "%"PRIu64, soft_limit);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        if (auto_delete) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                (xmlChar *) "autoDelete", "%s", auto_delete);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        if (snap_activate) {
                ret = xmlTextWriterWriteFormatElement (writer,
                           (xmlChar *) "activateOnCreate", "%s", snap_activate);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }

        /* </volumeConfig> or </systemConfig> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:
        return ret;
}

/* This function will generate snapshot config output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing config output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_config (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict)
{
        int             ret             = -1;
        int             config_command  = 0;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapConfig> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapConfig");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "config-command", &config_command);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Could not fetch config type");
                goto out;
        }

        switch (config_command) {
        case GF_SNAP_CONFIG_TYPE_SET:
                ret = cli_xml_snapshot_config_set (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create xml "
                                "output for snapshot config set command");
                        goto out;
                }

                break;
        case GF_SNAP_CONFIG_DISPLAY:
                ret = cli_xml_snapshot_config_show (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create xml "
                                "output for snapshot config show command");
                        goto out;
                }
                break;
        default:
                gf_log ("cli", GF_LOG_ERROR, "Unknown config command :%d",
                        config_command);
                ret = -1;
                goto out;
        }

        /* </snapConfig> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:

        return ret;
}

/* This function will generate snapshot activate or
 * deactivate output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing activate or deactivate output
 *
 * @return 0 on success and -1 on failure
 */
static int
cli_xml_snapshot_activate_deactivate (xmlTextWriterPtr writer, xmlDocPtr doc,
                                      dict_t *dict, int cmd)
{
        int     ret     = -1;
        char   *buffer  = NULL;
        char   *tag     = NULL;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        if (GF_SNAP_OPTION_TYPE_ACTIVATE == cmd) {
                tag = "snapActivate";
        } else if (GF_SNAP_OPTION_TYPE_DEACTIVATE == cmd) {
                tag = "snapDeactivate";
        } else {
                gf_log ("cli", GF_LOG_ERROR, "invalid command %d", cmd);
                goto out;
        }

        /* <snapActivate> or <snapDeactivate> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)tag);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapshot> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshot");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapname", &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapuuid", &buffer);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto out;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", buffer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapshot> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);


        /* </snapActivate> or </snapDeactivate> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = 0;
out:

        return ret;
}
#endif /* HAVE_LIB_XML */

/* This function will generate snapshot delete output in xml format.
 *
 * @param writer        xmlTextWriterPtr
 * @param doc           xmlDocPtr
 * @param dict          dict containing delete output
 * @param rsp           cli response
 *
 * @return 0 on success and -1 on failure
 */
int
cli_xml_snapshot_delete (xmlTextWriterPtr writer, xmlDocPtr doc, dict_t *dict,
                         gf_cli_rsp *rsp)
{
        int     ret             = -1;
#ifdef HAVE_LIB_XML
        char   *str_value       = NULL;

        GF_ASSERT (writer);
        GF_ASSERT (doc);
        GF_ASSERT (dict);

        /* <snapshot> */
        ret = xmlTextWriterStartElement (writer, (xmlChar *)"snapshot");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_str (dict, "snapname", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap name");
                goto xmlend;
        }

        if (!rsp->op_ret) {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *) "status",
                                                       "Success");
                XML_RET_CHECK_AND_GOTO (ret, xmlend);
        } else {
                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *) "status",
                                                       "Failure");
                XML_RET_CHECK_AND_GOTO (ret, xmlend);

                ret = cli_xml_output_common (writer, rsp->op_ret,
                                             rsp->op_errno,
                                             rsp->op_errstr);
                XML_RET_CHECK_AND_GOTO (ret, xmlend);
        }


        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "name",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, xmlend);

        ret = dict_get_str (dict, "snapuuid", &str_value);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snap uuid");
                goto xmlend;
        }

        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *) "uuid",
                                               "%s", str_value);
        XML_RET_CHECK_AND_GOTO (ret, out);

xmlend:
        /* </snapshot> */
        ret = xmlTextWriterEndElement (writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

#endif /* HAVE_LIB_XML */
        ret = 0;
out:

        return ret;
}

int
cli_xml_output_snap_status_begin (cli_local_t *local, int op_ret, int op_errno,
                                  char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret = -1;

        GF_ASSERT (local);

        ret = cli_begin_xml_output (&(local->writer), &(local->doc));
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = cli_xml_output_common (local->writer, op_ret, op_errno,
                                     op_errstr);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapStatus> */
        ret = xmlTextWriterStartElement (local->writer,
                                         (xmlChar *) "snapStatus");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapshots> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"snapshots");
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_TRACE, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_snap_status_end (cli_local_t *local)
{
#if (HAVE_LIB_XML)
        int     ret = -1;

        GF_ASSERT (local);

        /* </snapshots> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapStatus> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO(ret, out);

        ret = cli_end_xml_output (local->writer, local->doc);
out:
        gf_log ("cli", GF_LOG_TRACE, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_snap_delete_begin (cli_local_t *local, int op_ret, int op_errno,
                                  char *op_errstr)
{
#if (HAVE_LIB_XML)
        int ret = -1;
        int delete_cmd = -1;

        GF_ASSERT (local);

        ret = cli_begin_xml_output (&(local->writer), &(local->doc));
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (local->dict, "sub-cmd", &delete_cmd);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get sub-cmd");
                goto out;
        }

        ret = cli_xml_output_common (local->writer, op_ret, op_errno,
                                             op_errstr);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapStatus> */
        ret = xmlTextWriterStartElement (local->writer,
                                         (xmlChar *) "snapDelete");
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* <snapshots> */
        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"snapshots");
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        gf_log ("cli", GF_LOG_TRACE, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}

int
cli_xml_output_snap_delete_end (cli_local_t *local)
{
#if (HAVE_LIB_XML)
        int     ret = -1;

        GF_ASSERT (local);

        /* </snapshots> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

        /* </snapDelete> */
        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO(ret, out);

        ret = cli_end_xml_output (local->writer, local->doc);
out:
        gf_log ("cli", GF_LOG_TRACE, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif
}
/* This function will generate xml output for all the snapshot commands
 *
 * @param cmd_type      command type
 * @param dict          dict containing snapshot command output
 * @param op_ret        return value of the snapshot command
 * @param op_errno      errno for the snapshot command
 * @param op_errstr     error string for the snapshot command
 *
 * @return 0 on success and -1 on failure
 */
int
cli_xml_output_snapshot (int cmd_type, dict_t *dict, int op_ret,
                         int op_errno, char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     ret     = -1;
        xmlTextWriterPtr        writer  = NULL;
        xmlDocPtr               doc     = NULL;

        GF_ASSERT (dict);

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to output "
                        "xml begin block");
                goto out;
        }

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to output "
                        "xml common block");
                goto out;
        }

        /* In case of command failure just printing the error message is good
         * enough */
        if (0 != op_ret) {
                goto end;
        }

        switch (cmd_type) {
        case GF_SNAP_OPTION_TYPE_CREATE:
                ret = cli_xml_snapshot_create (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot create command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_CLONE:
                ret = cli_xml_snapshot_clone (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot clone command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_RESTORE:
                ret = cli_xml_snapshot_restore (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot restore command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_LIST:
                ret = cli_xml_snapshot_list (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot list command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_STATUS:
                ret = cli_xml_snapshot_status (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create"
                                "xml output for snapshot status command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_INFO:
                ret = cli_xml_snapshot_info (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot info command");
                        goto out;
                }
                break;
        case GF_SNAP_OPTION_TYPE_ACTIVATE:
        case GF_SNAP_OPTION_TYPE_DEACTIVATE:
                ret = cli_xml_snapshot_activate_deactivate (writer, doc,
                                                            dict, cmd_type);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot config command");
                }
                break;
        case GF_SNAP_OPTION_TYPE_CONFIG:
                ret = cli_xml_snapshot_config (writer, doc, dict);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to create "
                                "xml output for snapshot config command");
                }
                break;
        default:
                gf_log ("cli", GF_LOG_ERROR,
                        "Unexpected snapshot command: %d", cmd_type);
                goto out;
        }

end:
        ret = cli_end_xml_output (writer, doc);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to output "
                        "xml end block");
                goto out;
        }

        ret = 0;
out:
        return ret;
#else
        return 0;
#endif /* HAVE_LIB_XML */
}

int
cli_xml_snapshot_begin_composite_op (cli_local_t *local)
{
        int ret         = -1;
#ifdef HAVE_LIB_XML
        int cmd         = -1;
        int type        =  -1;

        ret = dict_get_int32 (local->dict, "sub-cmd", &cmd);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get "
                        "sub-cmd");
                ret = 0;
                goto out;
        }

        if (cmd == GF_SNAP_STATUS_TYPE_ITER ||
            cmd == GF_SNAP_DELETE_TYPE_SNAP){
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (local->dict, "type", &type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snapshot "
                        "command type from dictionary");
                goto out;
        }

        if (GF_SNAP_OPTION_TYPE_STATUS == type)
                ret = cli_xml_output_snap_status_begin (local, 0, 0, NULL);
        else if (GF_SNAP_OPTION_TYPE_DELETE == type)
                ret = cli_xml_output_snap_delete_begin (local, 0, 0, NULL);

        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Error creating xml output");
                goto out;
        }

#endif /* HAVE_LIB_XML */
        ret = 0;
out:
        return ret;
}

int
cli_xml_snapshot_end_composite_op (cli_local_t *local)
{
        int ret         = -1;
#ifdef HAVE_LIB_XML
        int cmd         = -1;
        int type        = -1;

        ret = dict_get_int32 (local->dict, "sub-cmd", &cmd);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get "
                        "sub-cmd");
                ret = 0;
                goto out;
        }

        if (cmd == GF_SNAP_STATUS_TYPE_ITER ||
            cmd == GF_SNAP_DELETE_TYPE_SNAP){
                ret = 0;
                goto out;
        }

        ret = dict_get_int32 (local->dict, "type", &type);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to get snapshot "
                        "command type from dictionary");
                goto out;
        }

        if (GF_SNAP_OPTION_TYPE_STATUS == type)
                ret = cli_xml_output_snap_status_end (local);
        else if (GF_SNAP_OPTION_TYPE_DELETE == type)
                ret = cli_xml_output_snap_delete_end (local);

        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Error creating xml "
                                "output");
                goto out;
        }
#endif /* HAVE_LIB_XML */
        ret = 0;
out:
        return ret;
}

int
cli_xml_snapshot_status_single_snap (cli_local_t *local, dict_t *dict,
                                     char *key)
{
#if (HAVE_LIB_XML)
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("cli", (local != NULL), out);
        GF_VALIDATE_OR_GOTO ("cli", (dict != NULL), out);
        GF_VALIDATE_OR_GOTO ("cli", (key != NULL), out);

        ret = cli_xml_snapshot_status_per_snap (local->writer, local->doc, dict,
                                                key);
out:
        return ret;
#else
        return 0;
#endif /* HAVE_LIB_XML */
}

int
cli_xml_output_vol_getopts (dict_t *dict, int op_ret, int op_errno,
                            char *op_errstr)
{
#if (HAVE_LIB_XML)
        int                     i = 0;
        int                     ret = -1;
        int                     count = 0;
        xmlTextWriterPtr        writer = NULL;
        xmlDocPtr               doc = NULL;
        char                    *key = NULL;
        char                    *value = NULL;
        char                    dict_key[50] = {0,};

        ret = cli_begin_xml_output (&writer, &doc);
        if (ret)
                goto out;

        ret = cli_xml_output_common (writer, op_ret, op_errno, op_errstr);
        if (ret)
                goto out;

        ret = xmlTextWriterStartElement (writer, (xmlChar *)"volGetopts");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = dict_get_int32 (dict, "count", &count);
        if (ret) {
                gf_log ("cli", GF_LOG_ERROR, "Failed to retrieve count "
                        "from the dictionary");
                goto out;
        }
        if (count <= 0) {
                gf_log ("cli", GF_LOG_ERROR, "Value of count :%d is "
                        "invalid", count);
                ret = -1;
                goto out;
        }
        ret = xmlTextWriterWriteFormatElement (writer, (xmlChar *)"count",
                                               "%d", count);

        XML_RET_CHECK_AND_GOTO (ret, out);

        for (i=1; i<=count; i++) {
                sprintf (dict_key, "key%d", i);
                ret = dict_get_str (dict, dict_key, &key);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to"
                                " retrieve %s from the "
                                "dictionary", dict_key);
                        goto out;
                }
                sprintf (dict_key, "value%d", i);
                ret = dict_get_str (dict, dict_key, &value);
                if (ret) {
                        gf_log ("cli", GF_LOG_ERROR, "Failed to "
                                "retrieve key value for %s from"
                                "the dictionary", dict_key);
                        goto out;
                }
                ret = xmlTextWriterStartElement (writer, (xmlChar *)"Opt");
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"Option",
                                                       "%s", key);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterWriteFormatElement (writer,
                                                       (xmlChar *)"Value",
                                                       "%s", value);
                XML_RET_CHECK_AND_GOTO (ret, out);

                ret = xmlTextWriterEndElement (writer);
                XML_RET_CHECK_AND_GOTO (ret, out);
        }
        ret = cli_end_xml_output (writer, doc);

out:
        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
#else
        return 0;
#endif /* HAVE_LIB_XML */
}

int
cli_quota_list_xml_error (cli_local_t *local, char *path,
                          char *errstr)
{
#if (HAVE_LIB_XML)
        int     ret     =       -1;

        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"limit");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                              (xmlChar *)"path",
                                               "%s", path);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                              (xmlChar *)"errstr",
                                               "%s", errstr);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        return ret;
#else
        return 0;
#endif
}

int
cli_quota_xml_output (cli_local_t *local, char *path, int64_t hl_str,
                      char *sl_final, int64_t sl_num, int64_t used,
                      int64_t avail, char *sl, char *hl,
                      gf_boolean_t limit_set)
{
#if (HAVE_LIB_XML)
        int     ret             = -1;

        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"limit");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"path",
                                               "%s", path);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"hard_limit",
                                               !limit_set ? "N/A" :
                                               "%"PRId64, hl_str);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"soft_limit_percent",
                                               !limit_set ? "N/A" :
                                               "%s", sl_final);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"soft_limit_value",
                                               !limit_set ? "N/A" :
                                               "%"PRId64, sl_num);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"used_space",
                                               "%"PRId64, used);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"avail_space",
                                               !limit_set ? "N/A" :
                                               "%"PRId64, avail);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                              (xmlChar *)"sl_exceeded",
                                               !limit_set ? "N/A" :
                                               "%s", sl);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"hl_exceeded",
                                               !limit_set ? "N/A" :
                                               "%s", hl);
        XML_RET_CHECK_AND_GOTO (ret, out);


        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        return ret;
#else
        return 0;
#endif /* HAVE_LIB_XML */
}

int
cli_quota_object_xml_output (cli_local_t *local, char *path, char *sl_str,
                             int64_t sl_val, quota_limits_t *limits,
                             quota_meta_t *used_space, int64_t avail,
                             char *sl, char *hl, gf_boolean_t limit_set)
{
#if (HAVE_LIB_XML)
        int     ret             = -1;

        ret = xmlTextWriterStartElement (local->writer, (xmlChar *)"limit");
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"path",
                                              "%s", path);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"hard_limit",
                                               !limit_set ? "N/A" :
                                               "%"PRId64, limits->hl);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"soft_limit_percent",
                                               !limit_set ? "N/A" :
                                               "%s", sl_str);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"soft_limit_value",
                                               !limit_set ? "N/A" :
                                               "%"PRIu64, sl_val);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"file_count",
                                               "%"PRId64,
                                               used_space->file_count);

        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"dir_count",
                                               "%"PRIu64,
                                               used_space->dir_count);

        XML_RET_CHECK_AND_GOTO (ret, out);


        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"available",
                                               !limit_set ? "N/A" :
                                               "%"PRId64, avail);

        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"sl_exceeded",
                                               !limit_set ? "N/A" :
                                               "%s", sl);
        XML_RET_CHECK_AND_GOTO (ret, out);

        ret = xmlTextWriterWriteFormatElement (local->writer,
                                               (xmlChar *)"hl_exceeded",
                                               !limit_set ? "N/A" :
                                               "%s", hl);
        XML_RET_CHECK_AND_GOTO (ret, out);


        ret = xmlTextWriterEndElement (local->writer);
        XML_RET_CHECK_AND_GOTO (ret, out);

out:
        return ret;
#else
        return 0;
#endif /* HAVE_LIB_XML */
}
