#include "aha.h"
#include "aha-helpers.h"
#include "aha-retry.h"
#include "aha-fops.h"

/*
 * AHA_RETRY_FOP:
 *
 * - We STACK_WIND the fop using the arguments in the call_stub.
 *   We use STACK_WIND because we need a *new* frame, since we already
 *   exhausted the existing frame with the original STACK_WIND.
 *
 * - After STACK_WIND completes, we can destroy this frame's local (which
 *   should be struct aha_fop *). The frame itself will get destroyed higher in
 *   the xlator graph, since its still part of the call stack.
 */
#define AHA_RETRY_FOP(fop, type, args ...)                              \
        do {                                                            \
                call_stub_t *stub = fop->stub;                          \
                call_frame_t *frame = fop->frame;                       \
                xlator_t *this = frame->this;                           \
                STACK_WIND (frame, aha_##type##_cbk, this,              \
                            this->fops->type, args);                    \
                AHA_DESTROY_LOCAL (frame);                              \
        } while (0)                                                     \

#define AHA_UNWIND_FOP(fop, type)                                       \
        do {                                                            \
                call_frame_t *frame = fop->frame;                       \
                AHA_DESTROY_LOCAL (frame);                              \
                default_##type##_failure_cbk (frame, ETIMEDOUT);        \
        } while (0)                                                     \

void
__aha_retry_force_unwind_fops (struct aha_conf *conf)
{
        struct aha_fop *fop = NULL;
        struct aha_fop *tmp = NULL;
        size_t  ndrained = 0;

        /*
         * Drain the queue. After we finish the loop, the list
         * must be empty.
         */
        list_for_each_entry_safe (fop, tmp, &conf->failed, list) {
                list_del (&fop->list);
                aha_force_unwind_fop (fop);
                ndrained++;
        }

        gf_log (GF_AHA, GF_LOG_WARNING,
                "Force-unwound %"GF_PRI_SIZET" fops!", ndrained);

        assert (list_empty (&conf->failed));
}

void
aha_force_unwind_fops (struct aha_conf *conf)
{
        LOCK (&conf->lock);
        {
                __aha_retry_force_unwind_fops (conf);
        }
        UNLOCK (&conf->lock);
}

void
__aha_retry_failed_fops (struct aha_conf *conf)
{
        struct aha_fop *fop = NULL;
        struct aha_fop *tmp = NULL;
        size_t ndrained = 0;

        /*
         * Skip if the child is not up
         */
        if (!conf->child_up) {
                gf_log (GF_AHA, GF_LOG_WARNING,
                        "Waiting for child to come up before retrying.");
                return;
        }

        /*
         * Skip if the the queue is empty.
         */
        if (list_empty (&conf->failed)) {
                gf_log (GF_AHA, GF_LOG_WARNING, "No FOPs to retry.");
        }

        /*
         * Drain the queue. After we finish the loop, the list
         * must be empty.
         */
        list_for_each_entry_safe (fop, tmp, &conf->failed, list) {
                list_del (&fop->list);
                aha_retry_fop (fop);
                ndrained++;
        }

        gf_log (GF_AHA, GF_LOG_WARNING,
                "Drained %"GF_PRI_SIZET" fops!", ndrained);

        assert (list_empty (&conf->failed));
}


void
aha_retry_failed_fops (struct aha_conf *conf)
{
        LOCK (&conf->lock);
        {
                __aha_retry_failed_fops (conf);
        }
        UNLOCK (&conf->lock);
}

void aha_retry_fop (struct aha_fop *fop)
{
        call_stub_t *stub = fop->stub;

        switch (stub->fop) {
        case GF_FOP_OPEN:
                AHA_RETRY_FOP (fop, open, &stub->args.loc, stub->args.flags,
                                stub->args.fd, stub->args.xdata);
                break;

        case GF_FOP_CREATE:
                AHA_RETRY_FOP (fop, create, &stub->args.loc, stub->args.flags,
                                stub->args.mode, stub->args.umask,
                                stub->args.fd,
                                stub->args.xdata);
                break;

        case GF_FOP_STAT:
                AHA_RETRY_FOP (fop, stat, &stub->args.loc, stub->args.xdata);
                break;

        case GF_FOP_READLINK:
                AHA_RETRY_FOP (fop, readlink, &stub->args.loc,
                                stub->args.size, stub->args.xdata);
                break;

        case GF_FOP_MKNOD:
                AHA_RETRY_FOP (fop, mknod, &stub->args.loc, stub->args.mode,
                                stub->args.rdev, stub->args.umask,
                                stub->args.xdata);
	        break;

        case GF_FOP_MKDIR:
                AHA_RETRY_FOP (fop, mkdir, &stub->args.loc, stub->args.mode,
                                stub->args.umask, stub->args.xdata);
                break;

        case GF_FOP_UNLINK:
                AHA_RETRY_FOP (fop, unlink, &stub->args.loc, stub->args.xflag,
                                stub->args.xdata);
                break;

        case GF_FOP_RMDIR:
                AHA_RETRY_FOP (fop, rmdir, &stub->args.loc,
                                stub->args.flags, stub->args.xdata);
                break;

        case GF_FOP_SYMLINK:
                AHA_RETRY_FOP (fop, symlink, stub->args.linkname,
                                &stub->args.loc, stub->args.umask,
                                stub->args.xdata);
                break;

        case GF_FOP_RENAME:
                AHA_RETRY_FOP (fop, rename, &stub->args.loc,
                                &stub->args.loc2, stub->args.xdata);
                break;

        case GF_FOP_LINK:
                AHA_RETRY_FOP (fop, link, &stub->args.loc,
                                &stub->args.loc2, stub->args.xdata);
                break;

        case GF_FOP_TRUNCATE:
                AHA_RETRY_FOP (fop, truncate, &stub->args.loc,
                                stub->args.offset, stub->args.xdata);
                break;

        case GF_FOP_READ:
                AHA_RETRY_FOP (fop, readv, stub->args.fd, stub->args.size,
                                stub->args.offset, stub->args.flags,
                                stub->args.xdata);
                break;

        case GF_FOP_WRITE:
                AHA_RETRY_FOP (fop, writev, stub->args.fd, stub->args.vector,
                                stub->args.count, stub->args.offset,
                                stub->args.flags, stub->args.iobref,
                                stub->args.xdata);
                break;

        case GF_FOP_STATFS:
                AHA_RETRY_FOP (fop, statfs, &stub->args.loc, stub->args.xdata);
                break;

        case GF_FOP_FLUSH:
                AHA_RETRY_FOP (fop, flush, stub->args.fd, stub->args.xdata);
                break;

        case GF_FOP_FSYNC:
                AHA_RETRY_FOP (fop, fsync, stub->args.fd, stub->args.datasync,
                                stub->args.xdata);
                break;

        case GF_FOP_SETXATTR:
                AHA_RETRY_FOP (fop, setxattr, &stub->args.loc, stub->args.xattr,
		                stub->args.flags, stub->args.xdata);
                break;

        case GF_FOP_GETXATTR:
                AHA_RETRY_FOP (fop, getxattr, &stub->args.loc,
                                stub->args.name, stub->args.xdata);
                break;

        case GF_FOP_FSETXATTR:
                AHA_RETRY_FOP (fop, fsetxattr, stub->args.fd,
                                stub->args.xattr, stub->args.flags,
                                stub->args.xdata);
                break;

        case GF_FOP_FGETXATTR:
                AHA_RETRY_FOP (fop, fgetxattr, stub->args.fd,
                                stub->args.name, stub->args.xdata);
                break;

        case GF_FOP_REMOVEXATTR:
                AHA_RETRY_FOP (fop, removexattr, &stub->args.loc,
                                stub->args.name, stub->args.xdata);
                break;

        case GF_FOP_FREMOVEXATTR:
                AHA_RETRY_FOP (fop, fremovexattr, stub->args.fd,
                                stub->args.name, stub->args.xdata);
                break;

        case GF_FOP_OPENDIR:
                AHA_RETRY_FOP (fop, opendir, &stub->args.loc,
                                stub->args.fd, stub->args.xdata);
                break;

        case GF_FOP_FSYNCDIR:
                AHA_RETRY_FOP (fop, fsyncdir, stub->args.fd,
                                stub->args.datasync, stub->args.xdata);
                break;

        case GF_FOP_ACCESS:
                AHA_RETRY_FOP (fop, access, &stub->args.loc,
                                stub->args.mask, stub->args.xdata);
                break;

        case GF_FOP_FTRUNCATE:
                AHA_RETRY_FOP (fop, ftruncate, stub->args.fd,
                                stub->args.offset, stub->args.xdata);
                break;

        case GF_FOP_FSTAT:
                AHA_RETRY_FOP (fop, fstat, stub->args.fd, stub->args.xdata);
                break;

        case GF_FOP_LK:
                AHA_RETRY_FOP (fop, lk, stub->args.fd, stub->args.cmd,
                                &stub->args.lock, stub->args.xdata);
                break;

        case GF_FOP_INODELK:
                AHA_RETRY_FOP (fop, inodelk, stub->args.volume,
                                &stub->args.loc, stub->args.cmd,
                                &stub->args.lock, stub->args.xdata);
                break;

        case GF_FOP_FINODELK:
                AHA_RETRY_FOP (fop, finodelk, stub->args.volume,
                                stub->args.fd, stub->args.cmd,
                                &stub->args.lock, stub->args.xdata);
                break;

        case GF_FOP_ENTRYLK:
                AHA_RETRY_FOP (fop, entrylk, stub->args.volume, &stub->args.loc,
	                        stub->args.name, stub->args.entrylkcmd,
		                stub->args.entrylktype, stub->args.xdata);
                break;

        case GF_FOP_FENTRYLK:
                AHA_RETRY_FOP (fop, fentrylk, stub->args.volume, stub->args.fd,
                                stub->args.name, stub->args.entrylkcmd,
                                stub->args.entrylktype, stub->args.xdata);
                break;

        case GF_FOP_LOOKUP:
                AHA_RETRY_FOP (fop, lookup, &stub->args.loc, stub->args.xdata);
                break;

        case GF_FOP_READDIR:
                AHA_RETRY_FOP (fop, readdir, stub->args.fd, stub->args.size,
                                stub->args.offset, stub->args.xdata);
                break;

        case GF_FOP_READDIRP:
                AHA_RETRY_FOP (fop, readdirp, stub->args.fd, stub->args.size,
                                stub->args.offset, stub->args.xdata);
                break;

        case GF_FOP_XATTROP:
                AHA_RETRY_FOP (fop, xattrop, &stub->args.loc, stub->args.optype,
                                stub->args.xattr, stub->args.xdata);
                break;

        case GF_FOP_FXATTROP:
                AHA_RETRY_FOP (fop, fxattrop, stub->args.fd, stub->args.optype,
                                stub->args.xattr, stub->args.xdata);
                break;

        case GF_FOP_SETATTR:
                AHA_RETRY_FOP (fop, setattr, &stub->args.loc, &stub->args.stat,
                                stub->args.valid, stub->args.xdata);
                break;

        case GF_FOP_FSETATTR:
                AHA_RETRY_FOP (fop, fsetattr, stub->args.fd, &stub->args.stat,
                                stub->args.valid, stub->args.xdata);
                break;

        default:
                /* Some fops are not implemented yet:
                 *
                 * GF_FOP_NULL
                 * GF_FOP_RCHECKSUM
                 * GF_FOP_FORGET
                 * GF_FOP_RELEASE
                 * GF_FOP_RELEASEDIR
                 * GF_FOP_GETSPEC
                 * GF_FOP_FALLOCATE
                 * GF_FOP_DISCARD
                 * GF_FOP_ZEROFILL
                 * GF_FOP_MAXVALUE
                 *
                 */
                gf_log (GF_AHA, GF_LOG_CRITICAL, "Got unexpected FOP %s",
                        gf_fop_list[stub->fop]);
                assert (0);
                break;
        }
}

void
aha_force_unwind_fop (struct aha_fop *fop)
{
        call_stub_t *stub = fop->stub;

        switch (stub->fop) {
        case GF_FOP_OPEN:
                AHA_UNWIND_FOP (fop, open);
                break;

        case GF_FOP_CREATE:
                AHA_UNWIND_FOP (fop, create);
                break;

        case GF_FOP_STAT:
                AHA_UNWIND_FOP (fop, stat);
                break;

        case GF_FOP_READLINK:
                AHA_UNWIND_FOP (fop, readlink);
                break;

        case GF_FOP_MKNOD:
                AHA_UNWIND_FOP (fop, mknod);
	        break;

        case GF_FOP_MKDIR:
                AHA_UNWIND_FOP (fop, mkdir);
                break;

        case GF_FOP_UNLINK:
                AHA_UNWIND_FOP (fop, unlink);
                break;

        case GF_FOP_RMDIR:
                AHA_UNWIND_FOP (fop, rmdir);
                break;

        case GF_FOP_SYMLINK:
                AHA_UNWIND_FOP (fop, symlink);
                break;

        case GF_FOP_RENAME:
                AHA_UNWIND_FOP (fop, rename);
                break;

        case GF_FOP_LINK:
                AHA_UNWIND_FOP (fop, link);
                break;

        case GF_FOP_TRUNCATE:
                AHA_UNWIND_FOP (fop, truncate);
                break;

        case GF_FOP_READ:
                AHA_UNWIND_FOP (fop, readv);
                break;

        case GF_FOP_WRITE:
                AHA_UNWIND_FOP (fop, writev);
                break;

        case GF_FOP_STATFS:
                AHA_UNWIND_FOP (fop, statfs);
                break;

        case GF_FOP_FLUSH:
                AHA_UNWIND_FOP (fop, flush);
                break;

        case GF_FOP_FSYNC:
                AHA_UNWIND_FOP (fop, fsync);
                break;

        case GF_FOP_SETXATTR:
                AHA_UNWIND_FOP (fop, setxattr);
                break;

        case GF_FOP_GETXATTR:
                AHA_UNWIND_FOP (fop, getxattr);
                break;

        case GF_FOP_FSETXATTR:
                AHA_UNWIND_FOP (fop, fsetxattr);
                break;

        case GF_FOP_FGETXATTR:
                AHA_UNWIND_FOP (fop, fgetxattr);
                break;

        case GF_FOP_REMOVEXATTR:
                AHA_UNWIND_FOP (fop, removexattr);
                break;

        case GF_FOP_FREMOVEXATTR:
                AHA_UNWIND_FOP (fop, fremovexattr);
                break;

        case GF_FOP_OPENDIR:
                AHA_UNWIND_FOP (fop, opendir);
                break;

        case GF_FOP_FSYNCDIR:
                AHA_UNWIND_FOP (fop, fsyncdir);
                break;

        case GF_FOP_ACCESS:
                AHA_UNWIND_FOP (fop, access);
                break;

        case GF_FOP_FTRUNCATE:
                AHA_UNWIND_FOP (fop, ftruncate);
                break;

        case GF_FOP_FSTAT:
                AHA_UNWIND_FOP (fop, fstat);
                break;

        case GF_FOP_LK:
                AHA_UNWIND_FOP (fop, lk);
                break;

        case GF_FOP_INODELK:
                AHA_UNWIND_FOP (fop, inodelk);
                break;

        case GF_FOP_FINODELK:
                AHA_UNWIND_FOP (fop, finodelk);
                break;

        case GF_FOP_ENTRYLK:
                AHA_UNWIND_FOP (fop, entrylk);
                break;

        case GF_FOP_FENTRYLK:
                AHA_UNWIND_FOP (fop, fentrylk);
                break;

        case GF_FOP_LOOKUP:
                AHA_UNWIND_FOP (fop, lookup);
                break;

        case GF_FOP_READDIR:
                AHA_UNWIND_FOP (fop, readdir);
                break;

        case GF_FOP_READDIRP:
                AHA_UNWIND_FOP (fop, readdirp);
                break;

        case GF_FOP_XATTROP:
                AHA_UNWIND_FOP (fop, xattrop);
                break;

        case GF_FOP_FXATTROP:
                AHA_UNWIND_FOP (fop, fxattrop);
                break;

        case GF_FOP_SETATTR:
                AHA_UNWIND_FOP (fop, setattr);
                break;

        case GF_FOP_FSETATTR:
                AHA_UNWIND_FOP (fop, fsetattr);
                break;

        default:
                /* Some fops are not implemented yet,
                 * and this would never happen cause we wouldn't
                 * queue them (see the assert statement in aha_retry_fop())
                 */
                break;
        }
}
