#include <stdlib.h>
#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include "libcvlt.h"
#include "cloudsync-common.h"
#include "cvlt-messages.h"

#define LIBARCHIVE_SO "libopenarchive.so"
#define ALIGN_SIZE 4096
#define CVLT_TRAILER "cvltv1"

store_methods_t store_ops = {
    .fop_download = cvlt_download,
    .fop_init = cvlt_init,
    .fop_reconfigure = cvlt_reconfigure,
    .fop_fini = cvlt_fini,
    .fop_remote_read = cvlt_read,
};

static const int32_t num_req = 32;
static const int32_t num_iatt = 32;
static char *plugin = "cvlt_cloudSync";

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_libcvlt_mt_end + 1);

    if (ret != 0) {
        return ret;
    }

    return ret;
}

static void
cvlt_free_resources(archive_t *arch)
{
    /*
     * We will release all the resources that were allocated by the xlator.
     * Check whether there are any buffers which have not been released
     * back to a mempool.
     */

    if (arch->handle) {
        dlclose(arch->handle);
    }

    if (arch->iobuf_pool) {
        iobuf_pool_destroy(arch->iobuf_pool);
    }

    if (arch->req_pool) {
        mem_pool_destroy(arch->req_pool);
        arch->req_pool = NULL;
    }

    return;
}

static int32_t
cvlt_extract_store_fops(xlator_t *this, archive_t *arch)
{
    int32_t op_ret = -1;
    get_archstore_methods_t get_archstore_methods;

    /*
     * libopenarchive.so defines methods for performing data management
     * operations. We will extract the methods from library and these
     * methods will be invoked for moving data between glusterfs volume
     * and the data management product.
     */

    VALIDATE_OR_GOTO(arch, err);

    arch->handle = dlopen(LIBARCHIVE_SO, RTLD_NOW);
    if (!arch->handle) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_DLOPEN_FAILED,
               " failed to open %s ", LIBARCHIVE_SO);
        return op_ret;
    }

    dlerror(); /* Clear any existing error */

    get_archstore_methods = dlsym(arch->handle, "get_archstore_methods");
    if (!get_archstore_methods) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " Error extracting get_archstore_methods()");
        dlclose(arch->handle);
        arch->handle = NULL;
        return op_ret;
    }

    op_ret = get_archstore_methods(&(arch->fops));
    if (op_ret) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " Failed to extract methods in get_archstore_methods");
        dlclose(arch->handle);
        arch->handle = NULL;
        return op_ret;
    }

err:
    return op_ret;
}

static int32_t
cvlt_alloc_resources(xlator_t *this, archive_t *arch, int num_req, int num_iatt)
{
    /*
     * Initialize information about all the memory pools that will be
     * used by this xlator.
     */
    arch->nreqs = 0;

    arch->req_pool = NULL;

    arch->handle = NULL;
    arch->xl = this;

    arch->req_pool = mem_pool_new(cvlt_request_t, num_req);
    if (!arch->req_pool) {
        goto err;
    }

    arch->iobuf_pool = iobuf_pool_new();
    if (!arch->iobuf_pool) {
        goto err;
    }

    if (cvlt_extract_store_fops(this, arch)) {
        goto err;
    }

    return 0;

err:

    return -1;
}

static void
cvlt_req_init(cvlt_request_t *req)
{
    sem_init(&(req->sem), 0, 0);

    return;
}

static void
cvlt_req_destroy(cvlt_request_t *req)
{
    if (req->iobuf) {
        iobuf_unref(req->iobuf);
    }

    if (req->iobref) {
        iobref_unref(req->iobref);
    }

    sem_destroy(&(req->sem));

    return;
}

static cvlt_request_t *
cvlt_alloc_req(archive_t *arch)
{
    cvlt_request_t *reqptr = NULL;

    if (!arch) {
        goto err;
    }

    if (arch->req_pool) {
        reqptr = mem_get0(arch->req_pool);
        if (reqptr) {
            cvlt_req_init(reqptr);
        }
    }

    if (reqptr) {
        LOCK(&(arch->lock));
        arch->nreqs++;
        UNLOCK(&(arch->lock));
    }

err:
    return reqptr;
}

static int32_t
cvlt_free_req(archive_t *arch, cvlt_request_t *reqptr)
{
    if (!reqptr) {
        goto err;
    }

    if (!arch) {
        goto err;
    }

    if (arch->req_pool) {
        /*
         * Free the request resources if they exist.
         */

        cvlt_req_destroy(reqptr);
        mem_put(reqptr);

        LOCK(&(arch->lock));
        arch->nreqs--;
        UNLOCK(&(arch->lock));
    }

    return 0;

err:
    return -1;
}

static int32_t
cvlt_init_xlator(xlator_t *this, archive_t *arch, int num_req, int num_iatt)
{
    int32_t ret = -1;
    int32_t errnum = -1;
    int32_t locked = 0;

    /*
     * Perform all the initializations needed for brining up the xlator.
     */
    if (!arch) {
        goto err;
    }

    LOCK_INIT(&(arch->lock));
    LOCK(&(arch->lock));

    locked = 1;

    ret = cvlt_alloc_resources(this, arch, num_req, num_iatt);

    if (ret) {
        goto err;
    }

    /*
     * Now that the fops have been extracted initialize the store
     */
    ret = arch->fops.init(&(arch->descinfo), &errnum, plugin);
    if (ret) {
        goto err;
    }

    UNLOCK(&(arch->lock));
    locked = 0;
    ret = 0;

    return ret;

err:
    if (arch) {
        cvlt_free_resources(arch);

        if (locked) {
            UNLOCK(&(arch->lock));
        }
    }

    return ret;
}

static int32_t
cvlt_term_xlator(archive_t *arch)
{
    int32_t errnum = -1;

    if (!arch) {
        goto err;
    }

    LOCK(&(arch->lock));

    /*
     * Release the resources that have been allocated inside store
     */
    arch->fops.fini(&(arch->descinfo), &errnum);

    cvlt_free_resources(arch);

    UNLOCK(&(arch->lock));

    GF_FREE(arch);

    return 0;

err:
    return -1;
}

static int32_t
cvlt_init_store_info(archive_t *priv, archstore_info_t *store_info)
{
    if (!store_info) {
        return -1;
    }

    store_info->prod = priv->product_id;
    store_info->prodlen = strlen(priv->product_id);

    store_info->id = priv->store_id;
    store_info->idlen = strlen(priv->store_id);

    return 0;
}

static int32_t
cvlt_init_file_info(cs_loc_xattr_t *xattr, archstore_fileinfo_t *file_info)
{
    if (!xattr || !file_info) {
        return -1;
    }

    gf_uuid_copy(file_info->uuid, xattr->uuid);
    file_info->path = xattr->file_path;
    file_info->pathlength = strlen(xattr->file_path);

    return 0;
}

static int32_t
cvlt_init_gluster_store_info(cs_loc_xattr_t *xattr,
                             archstore_info_t *store_info)
{
    static char *product = "glusterfs";

    if (!xattr || !store_info) {
        return -1;
    }

    store_info->prod = product;
    store_info->prodlen = strlen(product);

    store_info->id = xattr->volname;
    store_info->idlen = strlen(xattr->volname);

    return 0;
}

static int32_t
cvlt_init_gluster_file_info(cs_loc_xattr_t *xattr,
                            archstore_fileinfo_t *file_info)
{
    if (!xattr || !file_info) {
        return -1;
    }

    gf_uuid_copy(file_info->uuid, xattr->gfid);
    file_info->path = xattr->file_path;
    file_info->pathlength = strlen(xattr->file_path);

    return 0;
}

static void
cvlt_copy_stat_info(struct iatt *buf, cs_size_xattr_t *xattrs)
{
    /*
     * If the file was archived then the reported size will not be a
     * correct one. We need to fix this.
     */
    if (buf && xattrs) {
        buf->ia_size = xattrs->size;
        buf->ia_blksize = xattrs->blksize;
        buf->ia_blocks = xattrs->blocks;
    }

    return;
}

static void
cvlt_readv_complete(archstore_desc_t *desc, app_callback_info_t *cbkinfo,
                    void *cookie, int64_t op_ret, int32_t op_errno)
{
    struct iovec iov;
    xlator_t *this = NULL;
    struct iatt postbuf = {
        0,
    };
    call_frame_t *frame = NULL;
    cvlt_request_t *req = (cvlt_request_t *)cookie;
    cs_local_t *local = NULL;
    cs_private_t *cspriv = NULL;
    archive_t *priv = NULL;

    frame = req->frame;
    this = frame->this;
    local = frame->local;

    cspriv = this->private;
    priv = (archive_t *)cspriv->stores->config;

    if (strcmp(priv->trailer, CVLT_TRAILER)) {
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    gf_msg_debug(plugin, 0,
                 " Read callback invoked offset:%" PRIu64 "bytes: %" PRIu64
                 " op : %d ret : %" PRId64 " errno : %d",
                 req->offset, req->bytes, req->op_type, op_ret, op_errno);

    if (op_ret < 0) {
        goto out;
    }

    req->iobref = iobref_new();
    if (!req->iobref) {
        op_ret = -1;
        op_errno = ENOMEM;
        goto out;
    }

    iobref_add(req->iobref, req->iobuf);
    iov.iov_base = iobuf_ptr(req->iobuf);
    iov.iov_len = op_ret;

    cvlt_copy_stat_info(&postbuf, &(req->szxattr));

    /*
     * Hack to notify higher layers of EOF.
     */
    if (!postbuf.ia_size || (req->offset + iov.iov_len >= postbuf.ia_size)) {
        gf_msg_debug(plugin, 0, " signalling end-of-file for uuid=%s",
                     uuid_utoa(req->file_info.uuid));
        op_errno = ENOENT;
    }

out:

    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, &iov, 1, &postbuf,
                        req->iobref, local->xattr_rsp);

    cvlt_free_req(priv, req);

    return;
}

static void
cvlt_download_complete(archstore_desc_t *store, app_callback_info_t *cbk_info,
                       void *cookie, int64_t ret, int errcode)
{
    cvlt_request_t *req = (cvlt_request_t *)cookie;

    gf_msg_debug(plugin, 0,
                 " Download callback invoked  ret : %" PRId64 " errno : %d",
                 ret, errcode);

    req->op_ret = ret;
    req->op_errno = errcode;
    sem_post(&(req->sem));

    return;
}

void *
cvlt_init(xlator_t *this)
{
    int ret = 0;
    archive_t *priv = NULL;

    if (!this->children || this->children->next) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0,
               "should have exactly one child");
        ret = -1;
        goto out;
    }

    if (!this->parents) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0,
               "dangling volume. check volfile");
        ret = -1;
        goto out;
    }

    priv = GF_CALLOC(1, sizeof(archive_t), gf_libcvlt_mt_cvlt_private_t);
    if (!priv) {
        ret = -1;
        goto out;
    }

    priv->trailer = CVLT_TRAILER;
    if (cvlt_init_xlator(this, priv, num_req, num_iatt)) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0, "xlator init failed");
        ret = -1;
        goto out;
    }

    GF_OPTION_INIT("cloudsync-store-id", priv->store_id, str, out);
    GF_OPTION_INIT("cloudsync-product-id", priv->product_id, str, out);

    gf_msg(plugin, GF_LOG_INFO, 0, 0,
           "store id is : %s "
           "product id is : %s.",
           priv->store_id, priv->product_id);
out:
    if (ret == -1) {
        cvlt_term_xlator(priv);
        return (NULL);
    }
    return priv;
}

int
cvlt_reconfigure(xlator_t *this, dict_t *options)
{
    cs_private_t *cspriv = NULL;
    archive_t *priv = NULL;

    cspriv = this->private;
    priv = (archive_t *)cspriv->stores->config;

    if (strcmp(priv->trailer, CVLT_TRAILER))
        goto out;

    GF_OPTION_RECONF("cloudsync-store-id", priv->store_id, options, str, out);

    GF_OPTION_RECONF("cloudsync-product-id", priv->product_id, options, str,
                     out);
    gf_msg_debug(plugin, 0,
                 "store id is : %s "
                 "product id is : %s.",
                 priv->store_id, priv->product_id);
    return 0;
out:
    return -1;
}

void
cvlt_fini(void *config)
{
    archive_t *priv = NULL;

    priv = (archive_t *)config;

    if (strcmp(priv->trailer, CVLT_TRAILER))
        return;

    cvlt_term_xlator(priv);
    gf_msg(plugin, GF_LOG_INFO, 0, CVLT_FREE, " released xlator resources");
    return;
}

int
cvlt_download(call_frame_t *frame, void *config)
{
    archive_t *parch = NULL;
    cs_local_t *local = frame->local;
    cs_loc_xattr_t *locxattr = local->xattrinfo.lxattr;
    cvlt_request_t *req = NULL;
    archstore_info_t dest_storeinfo;
    archstore_fileinfo_t dest_fileinfo;
    int32_t op_ret, op_errno;

    parch = (archive_t *)config;

    if (strcmp(parch->trailer, CVLT_TRAILER)) {
        op_ret = -1;
        op_errno = EINVAL;
        goto err;
    }

    gf_msg_debug(plugin, 0, " download invoked for uuid = %s  gfid=%s ",
                 locxattr->uuid, uuid_utoa(locxattr->gfid));

    if (!(parch->fops.restore)) {
        op_errno = ELIBBAD;
        goto err;
    }

    /*
     * Download needs to be processed. Allocate a request.
     */
    req = cvlt_alloc_req(parch);

    if (!req) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, CVLT_RESOURCE_ALLOCATION_FAILED,
               " failed to allocated request for gfid=%s",
               uuid_utoa(locxattr->gfid));
        op_errno = ENOMEM;
        goto err;
    }

    /*
     * Initialize the request object.
     */
    req->op_type = CVLT_RESTORE_OP;
    req->frame = frame;

    /*
     * The file is currently residing inside a data management store.
     * To restore the file contents we need to provide the information
     * about data management store.
     */
    op_ret = cvlt_init_store_info(parch, &(req->store_info));
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " failed to extract store info for gfid=%s",
               uuid_utoa(locxattr->gfid));
        goto err;
    }

    op_ret = cvlt_init_file_info(locxattr, &(req->file_info));
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " failed to extract file info for gfid=%s",
               uuid_utoa(locxattr->gfid));
        goto err;
    }

    /*
     * We need to perform in-place restore of the file from data management
     * store to gusterfs volume.
     */
    op_ret = cvlt_init_gluster_store_info(locxattr, &dest_storeinfo);
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " failed to extract destination store info for gfid=%s",
               uuid_utoa(locxattr->gfid));
        goto err;
    }

    op_ret = cvlt_init_gluster_file_info(locxattr, &dest_fileinfo);
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " failed to extract file info for gfid=%s",
               uuid_utoa(locxattr->gfid));
        goto err;
    }

    /*
     * Submit the restore request.
     */
    op_ret = parch->fops.restore(&(parch->descinfo), &(req->store_info),
                                 &(req->file_info), &dest_storeinfo,
                                 &dest_fileinfo, &op_errno,
                                 cvlt_download_complete, req);
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_RESTORE_FAILED,
               " failed to restore file gfid=%s from data management store",
               uuid_utoa(locxattr->gfid));
        goto err;
    }

    /*
     * Wait for the restore to complete.
     */
    sem_wait(&(req->sem));

    if (req->op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_RESTORE_FAILED,
               " restored failed for gfid=%s", uuid_utoa(locxattr->gfid));
        goto err;
    }

    if (req) {
        cvlt_free_req(parch, req);
    }

    return 0;

err:

    if (req) {
        cvlt_free_req(parch, req);
    }

    return -1;
}

int
cvlt_read(call_frame_t *frame, void *config)
{
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    archive_t *parch = NULL;
    cvlt_request_t *req = NULL;
    struct iovec iov = {
        0,
    };
    struct iobref *iobref;
    size_t size = 0;
    off_t off = 0;

    cs_local_t *local = frame->local;
    cs_loc_xattr_t *locxattr = local->xattrinfo.lxattr;

    size = local->xattrinfo.size;
    off = local->xattrinfo.offset;

    parch = (archive_t *)config;

    if (strcmp(parch->trailer, CVLT_TRAILER)) {
        op_ret = -1;
        op_errno = EINVAL;
        goto err;
    }

    gf_msg_debug(plugin, 0,
                 " read invoked for gfid = %s offset = %" PRIu64
                 " file_size = %" PRIu64,
                 uuid_utoa(locxattr->gfid), off, local->stbuf.ia_size);

    if (off >= local->stbuf.ia_size) {
        /*
         * Hack to notify higher layers of EOF.
         */

        op_errno = ENOENT;
        op_ret = 0;

        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_READ_FAILED,
               " reporting end-of-file for gfid=%s", uuid_utoa(locxattr->gfid));

        goto err;
    }

    if (!size) {
        op_errno = EINVAL;

        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_READ_FAILED,
               " zero size read attempted on gfid=%s",
               uuid_utoa(locxattr->gfid));
        goto err;
    }

    if (!(parch->fops.read)) {
        op_errno = ELIBBAD;
        goto err;
    }

    /*
     * The read request need to be processed. Allocate a request.
     */
    req = cvlt_alloc_req(parch);

    if (!req) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, CVLT_NO_MEMORY,
               " failed to allocated request for gfid=%s",
               uuid_utoa(locxattr->gfid));
        op_errno = ENOMEM;
        goto err;
    }

    req->iobuf = iobuf_get_page_aligned(parch->iobuf_pool, size, ALIGN_SIZE);
    if (!req->iobuf) {
        op_errno = ENOMEM;
        goto err;
    }

    /*
     * Initialize the request object.
     */
    req->op_type = CVLT_READ_OP;
    req->offset = off;
    req->bytes = size;
    req->frame = frame;
    req->szxattr.size = local->stbuf.ia_size;
    req->szxattr.blocks = local->stbuf.ia_blocks;
    req->szxattr.blksize = local->stbuf.ia_blksize;

    /*
     * The file is currently residing inside a data management store.
     * To read the file contents we need to provide the information
     * about data management store.
     */
    op_ret = cvlt_init_store_info(parch, &(req->store_info));
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " failed to extract store info for gfid=%s"
               " offset=%" PRIu64 " size=%" GF_PRI_SIZET
               ", "
               " buf=%p",
               uuid_utoa(locxattr->gfid), off, size, req->iobuf->ptr);
        goto err;
    }

    op_ret = cvlt_init_file_info(locxattr, &(req->file_info));
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " failed to extract file info for gfid=%s"
               " offset=%" PRIu64 " size=%" GF_PRI_SIZET
               ", "
               " buf=%p",
               uuid_utoa(locxattr->gfid), off, size, req->iobuf->ptr);
        goto err;
    }

    /*
     * Submit the read request.
     */
    op_ret = parch->fops.read(&(parch->descinfo), &(req->store_info),
                              &(req->file_info), off, req->iobuf->ptr, size,
                              &op_errno, cvlt_readv_complete, req);

    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, CVLT_EXTRACTION_FAILED,
               " read failed on gfid=%s"
               " offset=%" PRIu64 " size=%" GF_PRI_SIZET
               ", "
               " buf=%p",
               uuid_utoa(locxattr->gfid), off, size, req->iobuf->ptr);
        goto err;
    }

    return 0;

err:

    iobref = iobref_new();
    gf_msg_debug(plugin, 0, " read unwinding stack op_ret = %d, op_errno = %d",
                 op_ret, op_errno);

    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, &iov, 1,
                        &(local->stbuf), iobref, local->xattr_rsp);

    if (iobref) {
        iobref_unref(iobref);
    }

    if (req) {
        cvlt_free_req(parch, req);
    }

    return 0;
}
