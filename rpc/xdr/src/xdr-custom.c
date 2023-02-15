
#include "glusterfs4-xdr.h"
#include "rpc-pragmas.h"
#include <glusterfs/glusterfs-fops.h>

/* These functions are necessary to avoid huge stack utilization when the
 * list of entries is large because rpcgen creates a simple recursive
 * implementation to encode the entire list.
 *
 * It's possible to modify the XDR structures to represent the entries as
 * a variable length array instead of a linked list of items to avoid this
 * problem. However this won't be backward compatible. We can't introduce
 * such a change in a minor release bur we need to fix this in all maintained
 * versions.
 *
 * With this hack we can immediately fix the issue in all existing versions
 * without breaking anythings, and we can wait for a major release to
 * introduce the backward incompatible changes.
 *
 * The code generated in glusterfs-xdr.c can be used as a reference to
 * implement this non-recursive version. */

/* TODO: Remove this once the XDR changes can be implemented. */

bool_t
xdr_gfx_dirlist_custom(XDR *xdrs, gfx_dirlist *objp)
{
    if (xdr_u_quad_t(xdrs, &objp->d_ino) && xdr_u_quad_t(xdrs, &objp->d_off) &&
        xdr_u_int(xdrs, &objp->d_len) && xdr_u_int(xdrs, &objp->d_type) &&
        xdr_string(xdrs, &objp->name, ~0)) {
        return TRUE;
    }

    return FALSE;
}

bool_t
xdr_gfx_readdir_rsp_custom(XDR *xdrs, gfx_readdir_rsp *objp)
{
    struct gfx_dirlist **pentry;

    if (!xdr_int(xdrs, &objp->op_ret) || !xdr_int(xdrs, &objp->op_errno) ||
        !xdr_gfx_dict(xdrs, &objp->xdata)) {
        return FALSE;
    }

    pentry = &objp->reply;
    while (xdr_pointer(xdrs, (char **)pentry, sizeof(gfx_dirlist),
                       (xdrproc_t)xdr_gfx_dirlist_custom)) {
        if (*pentry == NULL) {
            return TRUE;
        }
        pentry = &(*pentry)->nextentry;
    }

    return FALSE;
}

bool_t
xdr_gfx_dirplist_custom(XDR *xdrs, gfx_dirplist *objp)
{
    if (xdr_u_quad_t(xdrs, &objp->d_ino) && xdr_u_quad_t(xdrs, &objp->d_off) &&
        xdr_u_int(xdrs, &objp->d_len) && xdr_u_int(xdrs, &objp->d_type) &&
        xdr_string(xdrs, &objp->name, ~0) && xdr_gfx_iattx(xdrs, &objp->stat) &&
        xdr_gfx_dict(xdrs, &objp->dict)) {
        return TRUE;
    }

    return FALSE;
}

bool_t
xdr_gfx_readdirp_rsp_custom(XDR *xdrs, gfx_readdirp_rsp *objp)
{
    struct gfx_dirplist **pentry;

    if (!xdr_int(xdrs, &objp->op_ret) || !xdr_int(xdrs, &objp->op_errno) ||
        !xdr_gfx_dict(xdrs, &objp->xdata)) {
        return FALSE;
    }

    pentry = &objp->reply;
    while (xdr_pointer(xdrs, (char **)pentry, sizeof(gfx_dirplist),
                       (xdrproc_t)xdr_gfx_dirplist_custom)) {
        if (*pentry == NULL) {
            return TRUE;
        }
        pentry = &(*pentry)->nextentry;
    }

    return FALSE;
}
