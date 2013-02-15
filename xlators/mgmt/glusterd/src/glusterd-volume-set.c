/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterd-volgen.h"

/* dispatch table for VOLUME SET
 * -----------------------------
 *
 * Format of entries:
 *
 * First field is the <key>, for the purpose of looking it up
 * in volume dictionary. Each <key> is of the format "<domain>.<specifier>".
 *
 * Second field is <voltype>.
 *
 * Third field is <option>, if its unset, it's assumed to be
 * the same as <specifier>.
 *
 * Fourth field is <value>. In this context they are used to specify
 * a default. That is, even the volume dict doesn't have a value,
 * we procced as if the default value were set for it.
 *
 * Fifth field is <doctype>, which decides if the option is public and available
 * in "set help" or not. "NODOC" entries are not part of the public interface
 * and are subject to change at any time. This also decides if an option is
 * global (apllies to all volumes) or normal (applies to only specified volume).
 *
 * Sixth field is <flags>.
 *
 * Seventh field is <op-version>.
 *
 * There are two type of entries: basic and special.
 *
 * - Basic entries are the ones where the <option> does _not_ start with
 *   the bang! character ('!').
 *
 *   In their case, <option> is understood as an option for an xlator of
 *   type <voltype>. Their effect is to copy over the volinfo->dict[<key>]
 *   value to all graph nodes of type <voltype> (if such a value is set).
 *
 *   You are free to add entries of this type, they will become functional
 *   just by being present in the table.
 *
 * - Special entries where the <option> starts with the bang!.
 *
 *   They are not applied to all graphs during generation, and you cannot
 *   extend them in a trivial way which could be just picked up. Better
 *   not touch them unless you know what you do.
 *
 *
 * Another kind of grouping for options, according to visibility:
 *
 * - Exported: one which is used in the code. These are characterized by
 *   being used a macro as <key> (of the format VKEY_..., defined in
 *   glusterd-volgen.h
 *
 * - Non-exported: the rest; these have string literal <keys>.
 *
 * Adhering to this policy, option name changes shall be one-liners.
 *
 */

struct volopt_map_entry glusterd_volopt_map[] = {
        /* DHT xlator options */
        {"cluster.lookup-unhashed",              "cluster/distribute", NULL, NULL, DOC, 0, 1},
        {"cluster.min-free-disk",                "cluster/distribute", NULL, NULL, DOC, 0, 1},
        {"cluster.min-free-inodes",              "cluster/distribute", NULL, NULL, DOC, 0, 1},
        {"cluster.rebalance-stats",              "cluster/distribute", NULL, NULL, DOC, 0, 2},
        {"cluster.subvols-per-directory",        "cluster/distribute", "directory-layout-spread", NULL, DOC, 0, 2},
        {"cluster.readdir-optimize",             "cluster/distribute", NULL, NULL, DOC, 0, 2},
        {"cluster.nufa",                         "cluster/distribute", "!nufa", NULL, NO_DOC, 0, 2},

        /* AFR xlator options */
        {"cluster.entry-change-log",             "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.read-subvolume",               "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.read-subvolume-index",         "cluster/replicate",  NULL, NULL, DOC, 0, 2},
        {"cluster.read-hash-mode",               "cluster/replicate",  NULL, NULL, DOC, 0, 2},
        {"cluster.background-self-heal-count",   "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.metadata-self-heal",           "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.data-self-heal",               "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.entry-self-heal",              "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.self-heal-daemon",             "cluster/replicate",  "!self-heal-daemon" , NULL, DOC, 0, 1},
        {"cluster.heal-timeout",                 "cluster/replicate",  "!heal-timeout" , NULL, DOC, 0, 2},
        {"cluster.strict-readdir",               "cluster/replicate",  NULL, NULL, NO_DOC, 0, 1},
        {"cluster.self-heal-window-size",        "cluster/replicate",  "data-self-heal-window-size", NULL, DOC, 0, 1},
        {"cluster.data-change-log",              "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.metadata-change-log",          "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.data-self-heal-algorithm",     "cluster/replicate",  "data-self-heal-algorithm", NULL, DOC, 0, 1},
        {"cluster.eager-lock",                   "cluster/replicate",  NULL, NULL, DOC, 0, 1},
        {"cluster.quorum-type",                  "cluster/replicate",  "quorum-type", NULL, DOC, 0, 1},
        {"cluster.quorum-count",                 "cluster/replicate",  "quorum-count", NULL, DOC, 0, 1},
        {"cluster.choose-local",                 "cluster/replicate",  NULL, NULL, DOC, 0, 2},
        {"cluster.self-heal-readdir-size",       "cluster/replicate",  NULL, NULL, DOC, 0, 2},
        {"cluster.post-op-delay-secs",           "cluster/replicate",  NULL, NULL, NO_DOC, 0, 2},
        {"cluster.readdir-failover",             "cluster/replicate",  NULL, NULL, DOC, 0, 2},

        /* Stripe xlator options */
        {"cluster.stripe-block-size",            "cluster/stripe",     "block-size", NULL, DOC, 0, 1},
	{"cluster.stripe-coalesce",		 "cluster/stripe",     "coalesce", NULL, DOC, 0, 2},

        /* IO-stats xlator options */
        {VKEY_DIAG_LAT_MEASUREMENT,              "debug/io-stats",     "latency-measurement", "off", DOC, 0, 1},
        {"diagnostics.dump-fd-stats",            "debug/io-stats",     NULL, NULL, DOC, 0, 1},
        {VKEY_DIAG_CNT_FOP_HITS,                 "debug/io-stats",     "count-fop-hits", "off", NO_DOC, 0, 1},
        {"diagnostics.brick-log-level",          "debug/io-stats",     "!brick-log-level", NULL, DOC, 0, 1},
        {"diagnostics.client-log-level",         "debug/io-stats",     "!client-log-level", NULL, DOC, 0, 1},
        {"diagnostics.brick-sys-log-level",      "debug/io-stats",     "!sys-log-level", NULL, DOC, 0, 1},
        {"diagnostics.client-sys-log-level",     "debug/io-stats",     "!sys-log-level", NULL, DOC, 0, 1},

        /* IO-cache xlator options */
        {"performance.cache-max-file-size",      "performance/io-cache",      "max-file-size", NULL, DOC, 0, 1},
        {"performance.cache-min-file-size",      "performance/io-cache",      "min-file-size", NULL, DOC, 0, 1},
        {"performance.cache-refresh-timeout",    "performance/io-cache",      "cache-timeout", NULL, DOC, 0, 1},
        {"performance.cache-priority",           "performance/io-cache",      "priority", NULL, DOC, 0, 1},
        {"performance.cache-size",               "performance/io-cache",      NULL, NULL, DOC, 0, 1},

        /* IO-threads xlator options */
        {"performance.io-thread-count",          "performance/io-threads",    "thread-count", NULL, DOC, 0, 1},
        {"performance.high-prio-threads",        "performance/io-threads",    NULL, NULL, DOC, 0, 1},
        {"performance.normal-prio-threads",      "performance/io-threads",    NULL, NULL, DOC, 0, 1},
        {"performance.low-prio-threads",         "performance/io-threads",    NULL, NULL, DOC, 0, 1},
        {"performance.least-prio-threads",       "performance/io-threads",    NULL, NULL, DOC, 0, 1},
        {"performance.enable-least-priority",    "performance/io-threads",    NULL, NULL, DOC, 0, 2},
	{"performance.least-rate-limit",	 "performance/io-threads",    NULL, NULL, DOC, 0, 1},

        /* Other perf xlators' options */
        {"performance.cache-size",               "performance/quick-read",    NULL, NULL, DOC, 0, 1},

        {"performance.flush-behind",             "performance/write-behind",  "flush-behind", NULL, DOC, 0, 1},
        {"performance.write-behind-window-size", "performance/write-behind",  "cache-size", NULL, DOC, 1},
        {"performance.strict-o-direct",          "performance/write-behind",  "strict-O_DIRECT", NULL, DOC, 2},
        {"performance.strict-write-ordering",    "performance/write-behind",  "strict-write-ordering", NULL, DOC, 2},

        {"performance.read-ahead-page-count",    "performance/read-ahead",    "page-count", NULL, DOC, 1},
        {"performance.md-cache-timeout",         "performance/md-cache",      "md-cache-timeout", NULL, DOC, 0, 2},

        /* Client xlator options */
        {"network.frame-timeout",                "protocol/client",           NULL, NULL, DOC, 0, 1},
        {"network.ping-timeout",                 "protocol/client",           NULL, NULL, DOC, 0, 1},
        {"network.tcp-window-size",              "protocol/client",           NULL, NULL, DOC, 0, 1},
        {"features.lock-heal",                   "protocol/client",           "lk-heal", NULL, DOC, 0, 1},
        {"features.grace-timeout",               "protocol/client",           "grace-timeout", NULL, DOC, 0, 1},
        {"client.ssl",                           "protocol/client",           "transport.socket.ssl-enabled", NULL, NO_DOC, 0, 2},
        {"network.remote-dio",                   "protocol/client",           "filter-O_DIRECT", NULL, DOC, 0, 1},

        /* Server xlator options */
        {"network.tcp-window-size",              "protocol/server",           NULL, NULL, DOC, 0, 1},
        {"network.inode-lru-limit",              "protocol/server",           NULL, NULL, DOC, 0, 1},
        {AUTH_ALLOW_MAP_KEY,                     "protocol/server",           "!server-auth", "*", DOC, 0, 1},
        {AUTH_REJECT_MAP_KEY,                    "protocol/server",           "!server-auth", NULL, DOC, 0},

        {"transport.keepalive",                  "protocol/server",           "transport.socket.keepalive", NULL, NO_DOC, 0, 1},
        {"server.allow-insecure",                "protocol/server",           "rpc-auth-allow-insecure", NULL, NO_DOC, 0, 1},
        {"server.statedump-path",                "protocol/server",           "statedump-path", NULL, DOC, 0, 1},
        {"features.lock-heal",                   "protocol/server",           "lk-heal", NULL, NO_DOC, 0, 1},
        {"features.grace-timeout",               "protocol/server",           "grace-timeout", NULL, NO_DOC, 0, 1},
        {"server.ssl",                           "protocol/server",           "transport.socket.ssl-enabled", NULL, NO_DOC, 0, 2},

        /* Performance xlators enable/disbable options */
        {"performance.write-behind",             "performance/write-behind",  "!perf", "on", NO_DOC, 0, 1},
        {"performance.read-ahead",               "performance/read-ahead",    "!perf", "on", NO_DOC, 0, 1},
        {"performance.io-cache",                 "performance/io-cache",      "!perf", "on", NO_DOC, 0, 1},
        {"performance.quick-read",               "performance/quick-read",    "!perf", "on", NO_DOC, 0, 1},
        {"performance.open-behind",              "performance/open-behind",   "!perf", "on", NO_DOC, 0, 2},
        {"performance.stat-prefetch",            "performance/md-cache",      "!perf", "on", NO_DOC, 0, 1},
        {"performance.client-io-threads",        "performance/io-threads",    "!perf", "off", NO_DOC, 0, 1},

        {"performance.nfs.write-behind",         "performance/write-behind",  "!nfsperf", "off", NO_DOC, 0, 1},
        {"performance.nfs.read-ahead",           "performance/read-ahead",    "!nfsperf", "off", NO_DOC, 0, 1},
        {"performance.nfs.io-cache",             "performance/io-cache",      "!nfsperf", "off", NO_DOC, 0, 1},
        {"performance.nfs.quick-read",           "performance/quick-read",    "!nfsperf", "off", NO_DOC, 0, 1},
        {"performance.nfs.stat-prefetch",        "performance/md-cache",      "!nfsperf", "off", NO_DOC, 0, 1},
        {"performance.nfs.io-threads",           "performance/io-threads",    "!nfsperf", "off", NO_DOC, 0, 1},
	{"performance.force-readdirp",		 "performance/md-cache",      "force-readdirp", NULL, DOC, 0, 2},

        /* Quota xlator options */
        {VKEY_FEATURES_LIMIT_USAGE,              "features/quota",            "limit-set", NULL, NO_DOC, 0, 1},
        {"features.quota-timeout",               "features/quota",            "timeout", "0", DOC, 0, 1},

        /* Marker xlator options */
        {VKEY_MARKER_XTIME,                      "features/marker",           "xtime", "off", NO_DOC, OPT_FLAG_FORCE, 1},
        {VKEY_MARKER_XTIME,                      "features/marker",           "!xtime", "off", NO_DOC, OPT_FLAG_FORCE, 1},
        {VKEY_FEATURES_QUOTA,                    "features/marker",           "quota", "off", NO_DOC, OPT_FLAG_FORCE, 1},

        /* Debug xlators options */
        {"debug.trace",                          "debug/trace",               "!debug","off", NO_DOC, 0, 1},
        {"debug.log-history",                    "debug/trace",               "log-history", NULL, NO_DOC, 0, 2},
        {"debug.log-file",                       "debug/trace",               "log-file", NULL, NO_DOC, 0, 2},
        {"debug.exclude-ops",                    "debug/trace",               "exclude-ops", NULL, NO_DOC, 0, 2},
        {"debug.include-ops",                    "debug/trace",               "include-ops", NULL, NO_DOC, 0, 2},
        {"debug.error-gen",                      "debug/error-gen",           "!debug","off", NO_DOC, 0, 1},

        /* NFS xlator options */
        {"nfs.enable-ino32",                     "nfs/server",                "nfs.enable-ino32", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.mem-factor",                       "nfs/server",                "nfs.mem-factor", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.export-dirs",                      "nfs/server",                "nfs3.export-dirs", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.export-volumes",                   "nfs/server",                "nfs3.export-volumes", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.addr-namelookup",                  "nfs/server",                "rpc-auth.addr.namelookup", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.dynamic-volumes",                  "nfs/server",                "nfs.dynamic-volumes", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.register-with-portmap",            "nfs/server",                "rpc.register-with-portmap", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.port",                             "nfs/server",                "nfs.port", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.rpc-auth-unix",                    "nfs/server",                "!rpc-auth.auth-unix.*", NULL, DOC, 0, 1},
        {"nfs.rpc-auth-null",                    "nfs/server",                "!rpc-auth.auth-null.*", NULL, DOC, 0, 1},
        {"nfs.rpc-auth-allow",                   "nfs/server",                "!rpc-auth.addr.*.allow", NULL, DOC, 0, 1},
        {"nfs.rpc-auth-reject",                  "nfs/server",                "!rpc-auth.addr.*.reject", NULL, DOC, 0, 1},
        {"nfs.ports-insecure",                   "nfs/server",                "!rpc-auth.ports.*.insecure", NULL, DOC, 0, 1},
        {"nfs.transport-type",                   "nfs/server",                "!nfs.transport-type", NULL, DOC, 0, 1},
        {"nfs.trusted-sync",                     "nfs/server",                "!nfs3.*.trusted-sync", NULL, DOC, 0, 1},
        {"nfs.trusted-write",                    "nfs/server",                "!nfs3.*.trusted-write", NULL, DOC, 0, 1},
        {"nfs.volume-access",                    "nfs/server",                "!nfs3.*.volume-access", NULL, DOC, 0, 1},
        {"nfs.export-dir",                       "nfs/server",                "!nfs3.*.export-dir", NULL, DOC, 0, 1},
        {NFS_DISABLE_MAP_KEY,                    "nfs/server",                "!nfs-disable", NULL, DOC, 0, 1},
        {"nfs.nlm",                              "nfs/server",                "nfs.nlm", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.mount-udp",                        "nfs/server",                "nfs.mount-udp", NULL, GLOBAL_DOC, 0, 1},
        {"nfs.server-aux-gids",                  "nfs/server",                "nfs.server-aux-gids", NULL, NO_DOC, 0, 2},

        /* Other options which don't fit any place above */
        {"features.read-only",                   "features/read-only",        "!read-only", "off", DOC, 0, 2},
        {"features.worm",                        "features/worm",             "!worm", "off", DOC, 0, 2},

        {"storage.linux-aio",                    "storage/posix",             NULL, NULL, DOC, 0, 2},
        {"storage.owner-uid",                    "storage/posix",             "brick-uid", NULL, DOC, 0, 2},
        {"storage.owner-gid",                    "storage/posix",             "brick-gid", NULL, DOC, 0, 2},
        {"config.memory-accounting",             "configuration",             "!config", NULL, DOC, 0, 2},
        {"config.transport",                     "configuration",             "!config", NULL, DOC, 0, 2},
        {GLUSTERD_QUORUM_TYPE_KEY,               "mgmt/glusterd",             NULL, "off", DOC, 0, 2},
        {GLUSTERD_QUORUM_RATIO_KEY,              "mgmt/glusterd",             NULL, "0", DOC, 0, 2},
        {NULL,                                                                }
};
