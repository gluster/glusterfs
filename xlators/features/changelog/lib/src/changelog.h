/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GF_CHANGELOG_H
#define _GF_CHANGELOG_H

/* API set */

int
gf_changelog_register (char *brick_path, char *scratch_dir,
                       char *log_file, int log_levl, int max_reconnects);
ssize_t
gf_changelog_scan ();

int
gf_changelog_start_fresh ();

ssize_t
gf_changelog_next_change (char *bufptr, size_t maxlen);

int
gf_changelog_done (char *file);

#endif
