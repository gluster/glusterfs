
#ifndef _XDR_CUSTOM_H
#define _XDR_CUSTOM_H

#include <rpc/xdr.h>
#include "glusterfs4-xdr.h"
#include "rpc-pragmas.h"

bool_t
xdr_gfx_dirlist_custom(XDR *xdrs, gfx_dirlist *objp);

bool_t
xdr_gfx_readdir_rsp_custom(XDR *xdrs, gfx_readdir_rsp *objp);

bool_t
xdr_gfx_dirplist_custom(XDR *xdrs, gfx_dirplist *objp);

bool_t
xdr_gfx_readdirp_rsp_custom(XDR *xdrs, gfx_readdirp_rsp *objp);

#endif
