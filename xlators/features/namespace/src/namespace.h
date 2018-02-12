#ifndef __NAMESPACE_H__
#define __NAMESPACE_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "call-stub.h"

#define GF_NAMESPACE "namespace"

typedef struct {
        gf_boolean_t tag_namespaces;
} ns_private_t;

typedef struct {
        loc_t loc;         /* We store a "fake" loc_t for the getxattr wind. */
        call_stub_t *stub; /* A stub back to the function we're resuming. */
} ns_local_t;

#endif /* __NAMESPACE_H__ */
