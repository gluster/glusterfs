/* template-name fop */
int32_t
jbrc_@NAME@ (call_frame_t *frame, xlator_t *this,
             @LONG_ARGS@)
{
        jbrc_local_t    *local          = NULL;
        xlator_t        *target_xl      = ACTIVE_CHILD(this);

        local = mem_get(this->local_pool);
        if (!local) {
                goto err;
        }

        local->stub = fop_@NAME@_stub (frame, jbrc_@NAME@_continue,
                                       @SHORT_ARGS@);
        if (!local->stub) {
                goto err;
        }
        local->curr_xl = target_xl;
        local->scars = 0;

        frame->local = local;
        STACK_WIND_COOKIE (frame, jbrc_@NAME@_cbk, target_xl,
                    target_xl, target_xl->fops->@NAME@,
                    @SHORT_ARGS@);
        return 0;

err:
        if (local) {
                mem_put(local);
        }
        STACK_UNWIND_STRICT (@NAME@, frame, -1, ENOMEM,
                             @ERROR_ARGS@);
        return 0;
}

/* template-name cbk */
int32_t
jbrc_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 @LONG_ARGS@)
{
        jbrc_local_t    *local          = frame->local;
        xlator_t        *last_xl        = cookie;
        xlator_t        *next_xl;
        jbrc_private_t  *priv           = this->private;
        struct timespec spec;

        if (op_ret != (-1)) {
                if (local->scars) {
                        gf_msg (this->name, GF_LOG_INFO, 0, J_MSG_RETRY_MSG,
                                HILITE("retried %p OK"), frame->local);
                }
                priv->active = last_xl;
                goto unwind;
        }
        if ((op_errno != EREMOTE) && (op_errno != ENOTCONN)) {
                goto unwind;
        }

        /* TBD: get leader ID from xdata? */
        next_xl = next_xlator(this, last_xl);
        /*
         * We can't just give up after we've tried all bricks, because it's
         * quite likely that a new leader election just hasn't finished yet.
         * We also shouldn't retry endlessly, and especially not at a high
         * rate, but that's good enough while we work on other things.
         *
         * TBD: implement slow/finite retry via a worker thread
         */
        if (!next_xl || (local->scars >= SCAR_LIMIT)) {
                gf_msg (this->name, GF_LOG_DEBUG, 0, J_MSG_RETRY_MSG,
                        HILITE("ran out of retries for %p"), frame->local);
                goto unwind;
        }

        local->curr_xl = next_xl;
        local->scars += 1;
        spec.tv_sec = 1;
        spec.tv_nsec = 0;
        /*
         * WARNING
         *
         * Just calling gf_timer_call_after like this leaves open the
         * possibility that writes will get reordered, if a first write is
         * rescheduled and then a second comes along to find an updated
         * priv->active before the first actually executes.  We might need to
         * implement a stricter (and more complicated) queuing mechanism to
         * ensure absolute consistency in this case.
         */
        if (gf_timer_call_after(this->ctx, spec, jbrc_retry_cb, local)) {
                return 0;
        }

unwind:
        call_stub_destroy(local->stub);
        STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno,
                                     @SHORT_ARGS@);
        return 0;
}

/* template-name cont-func */
int32_t
jbrc_@NAME@_continue (call_frame_t *frame, xlator_t *this,
                      @LONG_ARGS@)
{
        jbrc_local_t    *local  = frame->local;

        STACK_WIND_COOKIE (frame, jbrc_@NAME@_cbk, local->curr_xl,
                           local->curr_xl, local->curr_xl->fops->@NAME@,
                           @SHORT_ARGS@);
        return 0;
}
