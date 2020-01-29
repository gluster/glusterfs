#include <glusterfs/call-stub.h>

#include "metadisp.h"
#include "metadisp-fops.h"

int32_t
init(xlator_t *this)
{
    if (!this->children) {
        gf_log(this->name, GF_LOG_ERROR,
               "not configured with children. exiting");
        return -1;
    }

    if (!this->parents) {
        gf_log(this->name, GF_LOG_WARNING, "dangling volume. check volfile ");
    }

    return 0;
}

void
fini(xlator_t *this)
{
    return;
}

/* defined in fops.c */
struct xlator_fops fops;

struct xlator_cbks cbks = {};

struct volume_options options[] = {
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .op_version = {1},
    .identifier = "metadisp",
    .category = GF_EXPERIMENTAL,
};
