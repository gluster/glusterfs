#ifndef _AHA_HELPERS_H
#define _AHA_HELPERS_H

#include "aha.h"

#define GF_AHA "aha"

struct aha_conf *aha_conf_new ();

void aha_conf_destroy (struct aha_conf *conf);

struct aha_fop *aha_fop_new ();

void aha_fop_destroy (struct aha_fop *fop);

#define AHA_DESTROY_LOCAL(frame)                        \
        do {                                            \
                struct aha_fop *fop = frame->local;          \
                aha_fop_destroy (fop);                  \
                frame->local = NULL;                    \
        } while (0)                                     \

#endif /* _AHA_HELPERS_H */
