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
 * in "set help" or not. "NO_DOC" entries are not part of the public interface
 * and are subject to change at any time. This also decides if an option is
 * global (apllies to all volumes) or normal (applies to only specified volume).
 *
 * Sixth field is <flags>.
 *
 * Seventh field is <op-version>.
 *
 * Eight field is description of option: If NULL, tried to fetch from
 * translator code's xlator_options table.
 *
 * Nineth field is validation function: If NULL, xlator's option specific
 * validation will be tried, otherwise tried at glusterd code itself.
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
        { .key         = "cluster.lookup-unhashed",
          .voltype     = "cluster/distribute",
          .op_version  = 1
        },
        { .key         = "cluster.min-free-disk",
          .voltype     = "cluster/distribute",
          .op_version  = 1
        },
        { .key         = "cluster.min-free-inodes",
          .voltype     = "cluster/distribute",
          .op_version  = 1
        },
        { .key         = "cluster.rebalance-stats",
          .voltype     = "cluster/distribute",
          .op_version  = 2
        },
        { .key         = "cluster.subvols-per-directory",
          .voltype     = "cluster/distribute",
          .option      = "directory-layout-spread",
          .op_version  = 2
        },
        { .key         = "cluster.readdir-optimize",
          .voltype     = "cluster/distribute",
          .op_version  = 2
        },
        { .key         = "cluster.nufa",
          .voltype     = "cluster/distribute",
          .option      = "!nufa",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "cluster.rsync-hash-regex",
          .voltype     = "cluster/distribute",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "cluster.extra-hash-regex",
          .voltype     = "cluster/distribute",
          .type        = NO_DOC,
          .op_version  = 2
        },

        /* AFR xlator options */
        { .key         = "cluster.entry-change-log",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.read-subvolume",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.read-subvolume-index",
          .voltype     = "cluster/replicate",
          .op_version  = 2
        },
        { .key         = "cluster.read-hash-mode",
          .voltype     = "cluster/replicate",
          .op_version  = 2
        },
        { .key         = "cluster.background-self-heal-count",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.metadata-self-heal",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.data-self-heal",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.entry-self-heal",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.self-heal-daemon",
          .voltype     = "cluster/replicate",
          .option      = "!self-heal-daemon",
          .op_version  = 1
        },
        { .key         = "cluster.heal-timeout",
          .voltype     = "cluster/replicate",
          .option      = "!heal-timeout",
          .op_version  = 2
        },
        { .key         = "cluster.strict-readdir",
          .voltype     = "cluster/replicate",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "cluster.self-heal-window-size",
          .voltype     = "cluster/replicate",
          .option      = "data-self-heal-window-size",
          .op_version  = 1
        },
        { .key         = "cluster.data-change-log",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.metadata-change-log",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.data-self-heal-algorithm",
          .voltype     = "cluster/replicate",
          .option      = "data-self-heal-algorithm",
          .op_version  = 1
        },
        { .key         = "cluster.eager-lock",
          .voltype     = "cluster/replicate",
          .op_version  = 1
        },
        { .key         = "cluster.quorum-type",
          .voltype     = "cluster/replicate",
          .option      = "quorum-type",
          .op_version  = 1
        },
        { .key         = "cluster.quorum-count",
          .voltype     = "cluster/replicate",
          .option      = "quorum-count",
          .op_version  = 1
        },
        { .key         = "cluster.choose-local",
          .voltype     = "cluster/replicate",
          .op_version  = 2
        },
        { .key         = "cluster.self-heal-readdir-size",
          .voltype     = "cluster/replicate",
          .op_version  = 2
        },
        { .key         = "cluster.post-op-delay-secs",
          .voltype     = "cluster/replicate",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "cluster.readdir-failover",
          .voltype     = "cluster/replicate",
          .op_version  = 2
        },

        /* Stripe xlator options */
        { .key         = "cluster.stripe-block-size",
          .voltype     = "cluster/stripe",
          .option      = "block-size",
          .op_version  = 1
        },
        { .key         = "cluster.stripe-coalesce",
          .voltype     = "cluster/stripe",
          .option      = "coalesce",
          .op_version  = 2
        },

        /* IO-stats xlator options */
        { .key         = VKEY_DIAG_LAT_MEASUREMENT,
          .voltype     = "debug/io-stats",
          .option      = "latency-measurement",
          .value       = "off",
          .op_version  = 1
        },
        { .key         = "diagnostics.dump-fd-stats",
          .voltype     = "debug/io-stats",
          .op_version  = 1
        },
        { .key         = VKEY_DIAG_CNT_FOP_HITS,
          .voltype     = "debug/io-stats",
          .option      = "count-fop-hits",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "diagnostics.brick-log-level",
          .voltype     = "debug/io-stats",
          .option      = "!brick-log-level",
          .op_version  = 1
        },
        { .key         = "diagnostics.client-log-level",
          .voltype     = "debug/io-stats",
          .option      = "!client-log-level",
          .op_version  = 1
        },
        { .key         = "diagnostics.brick-sys-log-level",
          .voltype     = "debug/io-stats",
          .option      = "!sys-log-level",
          .op_version  = 1
        },
        { .key         = "diagnostics.client-sys-log-level",
          .voltype     = "debug/io-stats",
          .option      = "!sys-log-level",
          .op_version  = 1
        },

        /* IO-cache xlator options */
        { .key         = "performance.cache-max-file-size",
          .voltype     = "performance/io-cache",
          .option      = "max-file-size",
          .op_version  = 1
        },
        { .key         = "performance.cache-min-file-size",
          .voltype     = "performance/io-cache",
          .option      = "min-file-size",
          .op_version  = 1
        },
        { .key         = "performance.cache-refresh-timeout",
          .voltype     = "performance/io-cache",
          .option      = "cache-timeout",
          .op_version  = 1
        },
        { .key         = "performance.cache-priority",
          .voltype     = "performance/io-cache",
          .option      = "priority",
          .op_version  = 1
        },
        { .key         = "performance.cache-size",
          .voltype     = "performance/io-cache",
          .op_version  = 1
        },

        /* IO-threads xlator options */
        { .key         = "performance.io-thread-count",
          .voltype     = "performance/io-threads",
          .option      = "thread-count",
          .op_version  = 1
        },
        { .key         = "performance.high-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.normal-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.low-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.least-prio-threads",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },
        { .key         = "performance.enable-least-priority",
          .voltype     = "performance/io-threads",
          .op_version  = 2
        },
        { .key         = "performance.least-rate-limit",
          .voltype     = "performance/io-threads",
          .op_version  = 1
        },

        /* Other perf xlators' options */
        { .key         = "performance.cache-size",
          .voltype     = "performance/quick-read",
          .op_version  = 1
        },
        { .key         = "performance.flush-behind",
          .voltype     = "performance/write-behind",
          .option      = "flush-behind",
          .op_version  = 1
        },
        { .key         = "performance.write-behind-window-size",
          .voltype     = "performance/write-behind",
          .option      = "cache-size",
          .flags       = 1,
          .op_version  = 1
        },
        { .key         = "performance.strict-o-direct",
          .voltype     = "performance/write-behind",
          .option      = "strict-O_DIRECT",
          .flags       = 2,
          .op_version  = 1
        },
        { .key         = "performance.strict-write-ordering",
          .voltype     = "performance/write-behind",
          .option      = "strict-write-ordering",
          .flags       = 2,
          .op_version  = 1
        },
        { .key         = "performance.read-ahead-page-count",
          .voltype     = "performance/read-ahead",
          .option      = "page-count",
          .flags       = 1,
          .op_version  = 1
        },
        { .key         = "performance.md-cache-timeout",
          .voltype     = "performance/md-cache",
          .option      = "md-cache-timeout",
          .op_version  = 2
        },

        /* Client xlator options */
        { .key         = "network.frame-timeout",
          .voltype     = "protocol/client",
          .op_version  = 1
        },
        { .key         = "network.ping-timeout",
          .voltype     = "protocol/client",
          .op_version  = 1
        },
        { .key         = "network.tcp-window-size",
          .voltype     = "protocol/client",
          .op_version  = 1
        },
        { .key         = "features.lock-heal",
          .voltype     = "protocol/client",
          .option      = "lk-heal",
          .op_version  = 1
        },
        { .key         = "features.grace-timeout",
          .voltype     = "protocol/client",
          .option      = "grace-timeout",
          .op_version  = 1
        },
        { .key         = "client.ssl",
          .voltype     = "protocol/client",
          .option      = "transport.socket.ssl-enabled",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "network.remote-dio",
          .voltype     = "protocol/client",
          .option      = "filter-O_DIRECT",
          .op_version  = 1
        },

        /* Server xlator options */
        { .key         = "network.tcp-window-size",
          .voltype     = "protocol/server",
          .op_version  = 1
        },
        { .key         = "network.inode-lru-limit",
          .voltype     = "protocol/server",
          .op_version  = 1
        },
        { .key         = AUTH_ALLOW_MAP_KEY,
          .voltype     = "protocol/server",
          .option      = "!server-auth",
          .value       = "*",
          .op_version  = 1
        },
        { .key         = AUTH_REJECT_MAP_KEY,
          .voltype     = "protocol/server",
          .option      = "!server-auth",
          .op_version  = 1
        },
        { .key         = "transport.keepalive",
          .voltype     = "protocol/server",
          .option      = "transport.socket.keepalive",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "server.allow-insecure",
          .voltype     = "protocol/server",
          .option      = "rpc-auth-allow-insecure",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "server.root-squash",
          .voltype     = "protocol/server",
          .option      = "root-squash",
          .op_version  = 2
        },
        { .key         = "server.statedump-path",
          .voltype     = "protocol/server",
          .option      = "statedump-path",
          .op_version  = 1
        },
        { .key         = "features.lock-heal",
          .voltype     = "protocol/server",
          .option      = "lk-heal",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "features.grace-timeout",
          .voltype     = "protocol/server",
          .option      = "grace-timeout",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "server.ssl",
          .voltype     = "protocol/server",
          .option      = "transport.socket.ssl-enabled",
          .type        = NO_DOC,
          .op_version  = 2
        },

        /* Performance xlators enable/disbable options */
        { .key         = "performance.write-behind",
          .voltype     = "performance/write-behind",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable write-behind translator in the volume."
        },
        { .key         = "performance.read-ahead",
          .voltype     = "performance/read-ahead",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable read-ahead translator in the volume."
        },
        { .key         = "performance.io-cache",
          .voltype     = "performance/io-cache",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable io-cache translator in the volume."
        },
        { .key         = "performance.quick-read",
          .voltype     = "performance/quick-read",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable quick-read translator in the volume."
        },
        { .key         = "performance.open-behind",
          .voltype     = "performance/open-behind",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 2,
          .description = "enable/disable open-behind translator in the volume."
        },
        { .key         = "performance.stat-prefetch",
          .voltype     = "performance/md-cache",
          .option      = "!perf",
          .value       = "on",
          .op_version  = 1,
          .description = "enable/disable meta-data caching translator in the "
                         "volume."
        },
        { .key         = "performance.client-io-threads",
          .voltype     = "performance/io-threads",
          .option      = "!perf",
          .value       = "off",
          .op_version  = 1,
          .description = "enable/disable io-threads translator in the client "
                         "graph of volume."
        },
        { .key         = "performance.nfs.write-behind",
          .voltype     = "performance/write-behind",
          .option      = "!nfsperf",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "performance.nfs.read-ahead",
          .voltype     = "performance/read-ahead",
          .option      = "!nfsperf",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "performance.nfs.io-cache",
          .voltype     = "performance/io-cache",
          .option      = "!nfsperf",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "performance.nfs.quick-read",
          .voltype     = "performance/quick-read",
          .option      = "!nfsperf",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "performance.nfs.stat-prefetch",
          .voltype     = "performance/md-cache",
          .option      = "!nfsperf",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "performance.nfs.io-threads",
          .voltype     = "performance/io-threads",
          .option      = "!nfsperf",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "performance.force-readdirp",
          .voltype     = "performance/md-cache",
          .option      = "force-readdirp",
          .op_version  = 2
        },

        /* Quota xlator options */
        { .key         = VKEY_FEATURES_LIMIT_USAGE,
          .voltype     = "features/quota",
          .option      = "limit-set",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "features.quota-timeout",
          .voltype     = "features/quota",
          .option      = "timeout",
          .value       = "0",
          .op_version  = 1
        },

        /* Marker xlator options */
        { .key         = VKEY_MARKER_XTIME,
          .voltype     = "features/marker",
          .option      = "xtime",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 1
        },
        { .key         = VKEY_MARKER_XTIME,
          .voltype     = "features/marker",
          .option      = "!xtime",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 1
        },
        { .key         = VKEY_FEATURES_QUOTA,
          .voltype     = "features/marker",
          .option      = "quota",
          .value       = "off",
          .type        = NO_DOC,
          .flags       = OPT_FLAG_FORCE,
          .op_version  = 1
        },

        /* Debug xlators options */
        { .key         = "debug.trace",
          .voltype     = "debug/trace",
          .option      = "!debug",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },
        { .key         = "debug.log-history",
          .voltype     = "debug/trace",
          .option      = "log-history",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.log-file",
          .voltype     = "debug/trace",
          .option      = "log-file",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.exclude-ops",
          .voltype     = "debug/trace",
          .option      = "exclude-ops",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.include-ops",
          .voltype     = "debug/trace",
          .option      = "include-ops",
          .type        = NO_DOC,
          .op_version  = 2
        },
        { .key         = "debug.error-gen",
          .voltype     = "debug/error-gen",
          .option      = "!debug",
          .value       = "off",
          .type        = NO_DOC,
          .op_version  = 1
        },


        /* NFS xlator options */
        { .key         = "nfs.enable-ino32",
          .voltype     = "nfs/server",
          .option      = "nfs.enable-ino32",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.mem-factor",
          .voltype     = "nfs/server",
          .option      = "nfs.mem-factor",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.export-dirs",
          .voltype     = "nfs/server",
          .option      = "nfs3.export-dirs",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.export-volumes",
          .voltype     = "nfs/server",
          .option      = "nfs3.export-volumes",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.addr-namelookup",
          .voltype     = "nfs/server",
          .option      = "rpc-auth.addr.namelookup",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.dynamic-volumes",
          .voltype     = "nfs/server",
          .option      = "nfs.dynamic-volumes",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.register-with-portmap",
          .voltype     = "nfs/server",
          .option      = "rpc.register-with-portmap",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.port",
          .voltype     = "nfs/server",
          .option      = "nfs.port",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-unix",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.auth-unix.*",
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-null",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.auth-null.*",
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-allow",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.addr.*.allow",
          .op_version  = 1
        },
        { .key         = "nfs.rpc-auth-reject",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.addr.*.reject",
          .op_version  = 1
        },
        { .key         = "nfs.ports-insecure",
          .voltype     = "nfs/server",
          .option      = "!rpc-auth.ports.*.insecure",
          .op_version  = 1
        },
        { .key         = "nfs.transport-type",
          .voltype     = "nfs/server",
          .option      = "!nfs.transport-type",
          .op_version  = 1
        },
        { .key         = "nfs.trusted-sync",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.trusted-sync",
          .op_version  = 1
        },
        { .key         = "nfs.trusted-write",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.trusted-write",
          .op_version  = 1
        },
        { .key         = "nfs.volume-access",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.volume-access",
          .op_version  = 1
        },
        { .key         = "nfs.export-dir",
          .voltype     = "nfs/server",
          .option      = "!nfs3.*.export-dir",
          .op_version  = 1
        },
        { .key         = NFS_DISABLE_MAP_KEY,
          .voltype     = "nfs/server",
          .option      = "!nfs-disable",
          .op_version  = 1
        },
        { .key         = "nfs.nlm",
          .voltype     = "nfs/server",
          .option      = "nfs.nlm",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.mount-udp",
          .voltype     = "nfs/server",
          .option      = "nfs.mount-udp",
          .type        = GLOBAL_DOC,
          .op_version  = 1
        },
        { .key         = "nfs.server-aux-gids",
          .voltype     = "nfs/server",
          .option      = "nfs.server-aux-gids",
          .type        = NO_DOC,
          .op_version  = 2
        },

        /* Other options which don't fit any place above */
        { .key         = "features.read-only",
          .voltype     = "features/read-only",
          .option      = "!read-only",
          .value       = "off",
          .op_version  = 2
        },
        { .key         = "features.worm",
          .voltype     = "features/worm",
          .option      = "!worm",
          .value       = "off",
          .op_version  = 2
        },
        { .key         = "storage.linux-aio",
          .voltype     = "storage/posix",
          .op_version  = 2
        },
        { .key         = "storage.owner-uid",
          .voltype     = "storage/posix",
          .option      = "brick-uid",
          .op_version  = 2
        },
        { .key         = "storage.owner-gid",
          .voltype     = "storage/posix",
          .option      = "brick-gid",
          .op_version  = 2
        },
        { .key         = "config.memory-accounting",
          .voltype     = "configuration",
          .option      = "!config",
          .op_version  = 2
        },
        { .key         = "config.transport",
          .voltype     = "configuration",
          .option      = "!config",
          .op_version  = 2
        },
        { .key         = GLUSTERD_QUORUM_TYPE_KEY,
          .voltype     = "mgmt/glusterd",
          .value       = "off",
          .op_version  = 2
        },
        { .key         = GLUSTERD_QUORUM_RATIO_KEY,
          .voltype     = "mgmt/glusterd",
          .value       = "0",
          .op_version  = 2
        },
        { .key         = NULL
        }
};
