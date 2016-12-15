#include "aha-helpers.h"

struct aha_conf *aha_conf_new ()
{
        struct aha_conf *conf = NULL;

        conf = GF_CALLOC (1, sizeof (*conf), gf_aha_mt_conf);
        if (!conf)
                goto err;

        INIT_LIST_HEAD (&conf->failed);

        LOCK_INIT (&conf->lock);
err:
        return conf;
}

void aha_conf_destroy (struct aha_conf *conf)
{
        LOCK_DESTROY (&conf->lock);
        GF_FREE (conf);
}

struct aha_fop *aha_fop_new ()
{
        struct aha_fop *fop = NULL;

        fop = GF_CALLOC (1, sizeof (*fop), gf_aha_mt_fop);
        if (!fop)
                goto err;

        INIT_LIST_HEAD (&fop->list);

err:
        return fop;
}

void aha_fop_destroy (struct aha_fop *fop)
{
        if (!fop)
                return;

        call_stub_destroy (fop->stub);
        fop->stub = NULL;
        GF_FREE (fop);
}
