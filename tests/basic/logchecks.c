/*
 * Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#include <stdio.h>
#include <unistd.h>

#include "glusterfs.h"
#include "globals.h"
#include "logging.h"

#include "logchecks-messages.h"
#include "../../libglusterfs/src/logging.h"

glusterfs_ctx_t   *ctx = NULL;

#define TEST_FILENAME           "/tmp/logchecks.log"
#define GF_LOG_CONTROL_FILE     "/etc/glusterfs/logger.conf"

int
go_log_vargs(gf_loglevel_t level, const char *fmt, ...)
{
        va_list  ap;

        va_start (ap, fmt);
        gf_msg_vplain (level, fmt, ap);
        va_end (ap);

        return 0;
}

int
go_log (void)
{
        /*** gf_msg ***/
        gf_msg ("logchecks", GF_LOG_INFO, 0, logchecks_msg_1);
        gf_msg ("logchecks", GF_LOG_INFO, 22, logchecks_msg_2, 42, "Forty-Two",
                42);
        /* change criticality */
        gf_msg ("logchecks", GF_LOG_CRITICAL, 0, logchecks_msg_3);
        gf_msg ("logchecks", GF_LOG_CRITICAL, 22, logchecks_msg_4, 42,
                "Forty-Two", 42);

        /*** msg_nomem ***/
        gf_msg_nomem ("logchecks", GF_LOG_ALERT, 555);
        gf_msg_nomem ("logchecks", GF_LOG_INFO, 555);

        /*** msg_plain ***/
        gf_msg_plain (GF_LOG_INFO, "Informational: gf_msg_plain with"
                        " args %d:%s:%x", 42, "Forty-Two", 42);
        gf_msg_plain (GF_LOG_ALERT, "Alert: gf_msg_plain with"
                        " args %d:%s:%x", 42, "Forty-Two", 42);

        /*** msg_vplain ***/
        go_log_vargs (GF_LOG_INFO, "Informational: gf_msg_vplain: No args!!!");
        go_log_vargs (GF_LOG_INFO, "Informational: gf_msg_vplain: Some"
                        " args %d:%s:%x", 42, "Forty-Two", 42);
        go_log_vargs (GF_LOG_INFO, "Critical: gf_msg_vplain: No args!!!");
        go_log_vargs (GF_LOG_INFO, "Critical: gf_msg_vplain: Some"
                        " args %d:%s:%x", 42, "Forty-Two", 42);

        /*** msg_plain_nomem ***/
        gf_msg_plain_nomem (GF_LOG_INFO, "Informational: gf_msg_plain_nomem");
        gf_msg_plain_nomem (GF_LOG_ALERT, "Alert: gf_msg_plain_nomem");

        /*** msg_backtrace_nomem ***/
        // TODO: Need to create a stack depth and then call
        gf_msg_backtrace_nomem (GF_LOG_INFO, 5);
        gf_msg_backtrace_nomem (GF_LOG_ALERT, 5);

        /*** gf_msg_callingfn ***/
        // TODO: Need to create a stack depth and then call
        gf_msg_callingfn ("logchecks", GF_LOG_INFO, 0, logchecks_msg_7);
        gf_msg_callingfn ("logchecks", GF_LOG_INFO, 0, logchecks_msg_8, 42,
                          "Forty-Two", 42);
        gf_msg_callingfn ("logchecks", GF_LOG_CRITICAL, 0, logchecks_msg_9);
        gf_msg_callingfn ("logchecks", GF_LOG_CRITICAL, 0, logchecks_msg_10, 42,
                          "Forty-Two", 42);

        /*** gf_msg_debug ***/
        gf_msg_debug ("logchecks", 0, "Debug: Hello World!!!");
        gf_msg_debug ("logchecks", 22, "Debug: With args %d:%s:%x", 42,
                      "Forty-Two", 42);

        /*** gf_msg_trace ***/
        gf_msg_trace ("logchecks", 0, "Trace: Hello World!!!");
        gf_msg_trace ("logchecks", 22, "Trace: With args %d:%s:%x", 42,
                      "Forty-Two", 42);

        /*** gf_msg_backtrace ***/
        // TODO: Test with lower callstr values to check truncation

        return 0;
}

int
main (int argc, char *argv[])
{
        int                ret = -1;

        unlink (GF_LOG_CONTROL_FILE);
        creat (GF_LOG_CONTROL_FILE, O_RDONLY);
        ctx = glusterfs_ctx_new ();
        if (!ctx)
                return -1;

        ret = glusterfs_globals_init (ctx);
        if (ret) {
                printf ("Error from glusterfs_globals_init [%s]\n",
                        strerror (errno));
                return ret;
        }

        /* Pre init test, message should not be printed */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_19);

        THIS->ctx = ctx;

        /* TEST 1: messages before initializing the log, goes to stderr
         * and syslog based on criticality */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_12);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 2: messages post initialization, goes to glusterlog and
         * syslog based on severity */
        ret = gf_log_init(ctx, TEST_FILENAME, "logchecks");
        if (ret != 0) {
                printf ("Error from gf_log_init [%s]\n", strerror (errno));
                return -1;
        }
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_13);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 3: Test rotation */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_logrotate (0);
        gf_msg ("logchecks", GF_LOG_CRITICAL, 0, logchecks_msg_5);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 4: Check flush, nothing noticeable should occur :) */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_flush ();
        gf_msg ("logchecks", GF_LOG_CRITICAL, 0, logchecks_msg_6);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 5: Change format */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_set_logformat (gf_logformat_traditional);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_14);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 6: Change level */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_set_loglevel (GF_LOG_CRITICAL);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_15);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* Reset to run with syslog */
        gf_log_set_logformat (gf_logformat_withmsgid);
        gf_log_set_loglevel (GF_LOG_INFO);

        /* Run tests with logger changed to syslog */
        /* TEST 7: No more gluster logs */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_set_logger (gf_logger_syslog);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_16);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 8: Change format */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_set_logformat (gf_logformat_traditional);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_14);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        /* TEST 9: Change level */
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);
        gf_log_set_loglevel (GF_LOG_CRITICAL);
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_15);
        go_log ();
        gf_msg ("logchecks", GF_LOG_ALERT, 0, logchecks_msg_11);

        // TODO: signal crash prints, but not yet feasible here
        // TODO: Graph printing
        // TODO: Multi threaded logging

        /* Close out the logging */
        gf_log_fini (ctx);
        gf_log_globals_fini ();

        unlink (GF_LOG_CONTROL_FILE);
        unlink (TEST_FILENAME);

        return 0;
}
