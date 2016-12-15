#include "aha-helpers.h"
#include "aha-retry.h"
#include "aha-fops.h"
#include "aha.h"

#include "syncop.h"


int
retry_failed_fops_cbk (int ret, call_frame_t *frame, void *arg)
{
        /* Nothing to do here ... */
        return 0;
}

int
retry_failed_fops (void *arg)
{
        xlator_t *this = NULL;

        struct aha_conf *conf = NULL;

        this = arg;
        conf = this->private;

        aha_retry_failed_fops (conf);

        return 0;
}

void
dispatch_fop_queue_drain (xlator_t *this)
{
        struct syncenv *env = NULL;
        int ret = 0;

        env = this->ctx->env;

        ret = synctask_new (env, retry_failed_fops,
                                retry_failed_fops_cbk, NULL, this);
        if (ret != 0) {
                gf_log (GF_AHA, GF_LOG_CRITICAL,
                        "Failed to dispatch synctask "
                        "to drain fop queue!");
        }
}

inline void
__aha_set_timer_status (struct aha_conf *conf, gf_boolean_t expired)
{
        conf->timer_expired = expired;
}

inline gf_boolean_t
__aha_is_timer_expired (struct aha_conf *conf)
{
        return conf->timer_expired;
}

gf_boolean_t
aha_is_timer_expired (struct aha_conf *conf)
{
        gf_boolean_t expired = _gf_false;

        LOCK (&conf->lock);
        {
                expired = __aha_is_timer_expired (conf);
        }
        UNLOCK (&conf->lock);

        return expired;
}

void
aha_child_down_timer_expired (void *data)
{
        struct aha_conf *conf = NULL;

        conf = data;

        gf_log (GF_AHA, GF_LOG_INFO, "Timer expired!");

        LOCK (&conf->lock);
        {
                __aha_set_timer_status (conf, _gf_true);
        }
        UNLOCK (&conf->lock);

        aha_force_unwind_fops ((struct aha_conf *)data);
}

void
__aha_start_timer (struct aha_conf *conf)
{
        struct timespec child_down_timeout = {
                .tv_sec = conf->server_wait_timeout,
                .tv_nsec = 0
        };

        __aha_set_timer_status (conf, _gf_false);

        conf->timer = gf_timer_call_after (conf->this->ctx, child_down_timeout,
                                           aha_child_down_timer_expired, conf);
        if (!conf->timer) {
                gf_log (GF_AHA, GF_LOG_CRITICAL, "Failed to start the timer!");
        }

        gf_log (GF_AHA, GF_LOG_INFO,
                "Registered timer for %lu seconds.",
                conf->server_wait_timeout);
}

void
__aha_cancel_timer (struct aha_conf *conf)
{
        if (!conf->timer)
                goto out;

        gf_timer_call_cancel (conf->this->ctx, conf->timer);
        conf->timer = NULL;
        gf_log (GF_AHA, GF_LOG_INFO, "Timer cancelled!");
out:
        return;
}

void
__aha_update_child_status (struct aha_conf *conf, int status)
{
        conf->child_up = status;
}

void
aha_handle_child_up (xlator_t *this)
{
        struct aha_conf *conf = this->private;

        LOCK (&conf->lock);
        {
                __aha_update_child_status (
                    conf, AHA_CHILD_STATUS_UP);  /* Mark the child as up */
                __aha_set_timer_status (
                    conf, _gf_false);       /* Timer is no longer expired */
                __aha_cancel_timer (conf);      /* Cancel the timer */
        }
        UNLOCK (&conf->lock);
}

void
aha_handle_child_down (xlator_t *this)
{
        struct aha_conf *conf = this->private;

        LOCK (&conf->lock);
        {
                __aha_update_child_status (conf, AHA_CHILD_STATUS_DOWN);
                __aha_set_timer_status (conf, _gf_true);
                __aha_start_timer (conf);
        }
        UNLOCK (&conf->lock);
}

int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        switch (event) {
        case GF_EVENT_CHILD_DOWN:
                gf_log (this->name, GF_LOG_WARNING, "Got child-down event!");
                aha_handle_child_down (this);
                break;
        case GF_EVENT_CHILD_UP:
                gf_log (this->name, GF_LOG_WARNING, "Got child-up event!");
                aha_handle_child_up (this);
                dispatch_fop_queue_drain (this);
                break;
        default:
                break;
        }

        default_notify (this, event, data);

        return 0;
}

int32_t
aha_priv_dump (xlator_t *this)
{
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_aha_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Memory accounting init failed!");
                return ret;
        }

        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        struct aha_conf *conf = NULL;

        conf = this->private;

        GF_OPTION_RECONF ("server-wait-timeout-seconds",
                                conf->server_wait_timeout,
                                options, size_uint64, err);

        return 0;
err:
        return -1;
}

int
aha_init_options (xlator_t *this)
{
        struct aha_conf *conf = NULL;

        conf = this->private;

        GF_OPTION_INIT ("server-wait-timeout-seconds",
                        conf->server_wait_timeout,
                        size_uint64, err);

        return 0;
err:
        return -1;
}


int
init (xlator_t *this)
{
        int ret = 0;
        struct aha_conf *conf = NULL;

        conf = aha_conf_new ();
        if (!conf) {
                ret = -(ENOMEM);
                goto err;
        }

        conf->this = this;
        this->private = conf;

        aha_init_options (this);

        /* init() completed successfully */
        goto done;
err:
        gf_log (GF_AHA, GF_LOG_ERROR,
                "init() failed, please see "
                "logs for details.");

        /* Free all allocated memory */
        aha_conf_destroy (conf);
done:
        return ret;
}

void
fini (xlator_t *this)
{
        struct aha_conf *conf = this->private;

        aha_conf_destroy (conf);

        this->private = NULL;
}

struct xlator_dumpops dumpops = {
        .priv = aha_priv_dump,
};

struct xlator_fops cbks;

struct xlator_fops fops = {
    .lookup      = aha_lookup,
    .stat        = aha_stat,
    .readlink    = aha_readlink,
    .mknod       = aha_mknod,
    .mkdir       = aha_mkdir,
    .unlink      = aha_unlink,
    .rmdir       = aha_rmdir,
    .symlink     = aha_symlink,
    .rename      = aha_rename,
    .link        = aha_link,
    .truncate    = aha_truncate,
    .create      = aha_create,
    .open        = aha_open,
    .readv       = aha_readv,
    .writev      = aha_writev,
    .statfs      = aha_statfs,
    .flush       = aha_flush,
    .fsync       = aha_fsync,
    .setxattr    = aha_setxattr,
    .getxattr    = aha_getxattr,
    .removexattr = aha_removexattr,
    .fsetxattr    = aha_fsetxattr,
    .fgetxattr    = aha_fgetxattr,
    .fremovexattr = aha_fremovexattr,
    .opendir     = aha_opendir,
    .readdir     = aha_readdir,
    .readdirp    = aha_readdirp,
    .fsyncdir    = aha_fsyncdir,
    .access      = aha_access,
    .ftruncate   = aha_ftruncate,
    .fstat       = aha_fstat,
    .lk          = aha_lk,
    .lookup_cbk  = aha_lookup_cbk,
    .xattrop     = aha_xattrop,
    .fxattrop    = aha_fxattrop,
    .inodelk     = aha_inodelk,
    .finodelk    = aha_finodelk,
    .entrylk     = aha_entrylk,
    .fentrylk    = aha_fentrylk,
    .setattr     = aha_setattr,
    .fsetattr    = aha_fsetattr,
};

struct volume_options options[] = {
        { .key = {"server-wait-timeout-seconds"},
          .type = GF_OPTION_TYPE_SIZET,
          .min = 10,
          .max = 20 * 60,
          .default_value = TOSTRING (120),
          .description = "Specifies the number of seconds the "
                         "AHA translator will wait "
                         "for a CHILD_UP event before "
                         "force-unwinding the frames it has "
                         "currently stored for retry."
        },
        { .key  = {NULL} }
};
