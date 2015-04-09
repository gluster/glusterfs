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

struct gf_brick_spec;

/**
 * Max bit shiter for event selection
 */
#define CHANGELOG_EV_SELECTION_RANGE  5

#define CHANGELOG_OP_TYPE_JOURNAL    (1<<0)
#define CHANGELOG_OP_TYPE_OPEN       (1<<1)
#define CHANGELOG_OP_TYPE_CREATE     (1<<2)
#define CHANGELOG_OP_TYPE_RELEASE    (1<<3)
#define CHANGELOG_OP_TYPE_BR_RELEASE (1<<4)  /* logical release (last close()),
                                                sent by bitrot stub */
#define CHANGELOG_OP_TYPE_MAX        (1<<CHANGELOG_EV_SELECTION_RANGE)


struct ev_open {
        unsigned char gfid[16];
        int32_t flags;
};

struct ev_creat {
        unsigned char gfid[16];
        int32_t flags;
};

struct ev_release {
        unsigned char gfid[16];
};

struct ev_release_br {
        unsigned long version;
        unsigned char gfid[16];
        int32_t sign_info;
};

struct ev_changelog {
        char path[PATH_MAX];
};

typedef struct changelog_event {
        unsigned int ev_type;

        union {
                struct ev_open open;
                struct ev_creat create;
                struct ev_release release;
                struct ev_changelog journal;
                struct ev_release_br releasebr;
        } u;
} changelog_event_t;

#define CHANGELOG_EV_SIZE  (sizeof (changelog_event_t))

/**
 * event callback, connected & disconnection defs
 */
typedef void (CALLBACK) (void *, char *,
                        void *, changelog_event_t *);
typedef void *(INIT) (void *, struct gf_brick_spec *);
typedef void (FINI) (void *, char *, void *);
typedef void (CONNECT) (void *, char *, void *);
typedef void (DISCONNECT) (void *, char *, void *);

struct gf_brick_spec {
        char         *brick_path;
        unsigned int  filter;

        INIT       *init;
        FINI       *fini;
        CALLBACK   *callback;
        CONNECT    *connected;
        DISCONNECT *disconnected;

        void *ptr;
};

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

/* newer flexible API */
int
gf_changelog_init (void *xl);

int
gf_changelog_register_generic (struct gf_brick_spec *bricks, int count,
                               int ordered, char *logfile, int lvl, void *xl);

#endif
