/*
 * You can put anything here - it doesn't even have to be a comment - and it
 * will be ignored until we reach the first template-name comment.
 */


/* template-name read-fop */
int32_t
jbr_@NAME@ (call_frame_t *frame, xlator_t *this,
            @LONG_ARGS@)
{
        jbr_private_t   *priv   = this->private;
        gf_boolean_t in_recon = _gf_false;
        int32_t recon_term, recon_index;

        /* allow reads during reconciliation       *
         * TBD: allow "dirty" reads on non-leaders *
         */
        if (xdata &&
            (dict_get_int32(xdata, RECON_TERM_XATTR, &recon_term) == 0) &&
            (dict_get_int32(xdata, RECON_INDEX_XATTR, &recon_index) == 0)) {
                in_recon = _gf_true;
        }

        if ((!priv->leader) && (in_recon == _gf_false)) {
                goto err;
        }

        STACK_WIND (frame, default_@NAME@_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                    @SHORT_ARGS@);
        return 0;

err:
        STACK_UNWIND_STRICT (@NAME@, frame, -1, EREMOTE,
                             @ERROR_ARGS@);
        return 0;
}

/* template-name read-dispatch */
/* No "dispatch" function needed for @NAME@ */

/* template-name read-fan-in */
/* No "fan-in" function needed for @NAME@ */

/* template-name read-continue */
/* No "continue" function needed for @NAME@ */

/* template-name read-complete */
/* No "complete" function needed for @NAME@ */

/* template-name write-fop */
int32_t
jbr_@NAME@ (call_frame_t *frame, xlator_t *this,
            @LONG_ARGS@)
{
        jbr_local_t     *local          = NULL;
        jbr_private_t   *priv           = this->private;
        gf_boolean_t     result         = _gf_false;
        int             op_errno        = ENOMEM;
        int             from_leader;
        int             from_recon;
        uint32_t        ti = 0;

        /*
         * Our first goal here is to avoid "split brain surprise" for users who
         * specify exactly 50% with two- or three-way replication.  That means
         * either a more-than check against half the total replicas or an
         * at-least check against half of our peers (one less).  Of the two,
         * only an at-least check supports the intuitive use of 100% to mean
         * all replicas must be present, because "more than 100%" will never
         * succeed regardless of which count we use.  This leaves us with a
         * slightly non-traditional definition of quorum ("at least X% of peers
         * not including ourselves") but one that's useful enough to be worth
         * it.
         *
         * Note that n_children and up_children *do* include the local
         * subvolume, so we need to subtract one in each case.
         */
        if (priv->leader) {
                result = fop_quorum_check (this, (double)(priv->n_children - 1),
                                   (double)(priv->up_children - 1));

                if (result == _gf_false) {
                        /* Emulate the AFR client-side-quorum behavior. */
                        gf_msg (this->name, GF_LOG_ERROR, EROFS,
                                J_MSG_QUORUM_NOT_MET, "Sufficient number of "
                                "subvolumes are not up to meet quorum.");
                        op_errno = EROFS;
                        goto err;
                }
        } else {
                if (xdata) {
                        from_leader = !!dict_get(xdata, JBR_TERM_XATTR);
                        from_recon = !!dict_get(xdata, RECON_TERM_XATTR)
                                  && !!dict_get(xdata, RECON_INDEX_XATTR);
                } else {
                        from_leader = from_recon = _gf_false;
                }

                /* follower/recon path        *
                 * just send it to local node *
                 */
                if (!from_leader && !from_recon) {
                        op_errno = EREMOTE;
                        goto err;
                }
        }

        local = mem_get0(this->local_pool);
        if (!local) {
                goto err;
        }
#if defined(JBR_CG_NEED_FD)
        local->fd = fd_ref(fd);
#else
        local->fd = NULL;
#endif
        INIT_LIST_HEAD(&local->qlinks);
        frame->local = local;

        /*
         * If we let it through despite not being the leader, then we just want
         * to pass it on down without all of the additional xattrs, queuing, and
         * so on.  However, jbr_*_complete does depend on the initialization
         * immediately above this.
         */
        if (!priv->leader) {
                STACK_WIND (frame, jbr_@NAME@_complete,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                            @SHORT_ARGS@);
                return 0;
        }

        if (!xdata) {
                xdata = dict_new();
                if (!xdata) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                J_MSG_MEM_ERR, "failed to allocate xdata");
                        goto err;
                }
        }

        if (dict_set_int32(xdata, JBR_TERM_XATTR, priv->current_term) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR, "failed to set jbr-term");
                goto err;
        }

        LOCK(&priv->index_lock);
        ti = ++(priv->index);
        UNLOCK(&priv->index_lock);
        if (dict_set_int32(xdata, JBR_INDEX_XATTR, ti) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR, "failed to set index");
                goto err;
        }

        local->stub = fop_@NAME@_stub (frame, jbr_@NAME@_continue,
                                       @SHORT_ARGS@);
        if (!local->stub) {
                goto err;
        }


#if defined(JBR_CG_QUEUE)
        jbr_inode_ctx_t         *ictx   = jbr_get_inode_ctx(this, fd->inode);

        if (!ictx) {
                op_errno = EIO;
                goto err;
        }
        LOCK(&ictx->lock);
                if (ictx->active) {
                        gf_msg_debug (this->name, 0,
                                      "queuing request due to conflict");
                        /*
                         * TBD: enqueue only for real conflict
                         *
                         * Currently we just act like all writes are in
                         * conflict with one another.  What we should really do
                         * is check the active/pending queues and defer only if
                         * there's a conflict there.
                         *
                         * It's important to check the pending queue because we
                         * might have an active request X which conflicts with
                         * a pending request Y, and this request Z might
                         * conflict with Y but not X.  If we checked only the
                         * active queue then Z could jump ahead of Y, which
                         * would be incorrect.
                         */
                        local->qstub = fop_@NAME@_stub (frame,
                                                        jbr_@NAME@_dispatch,
                                                        @SHORT_ARGS@);
                        if (!local->qstub) {
                                UNLOCK(&ictx->lock);
                                goto err;
                        }
                        list_add_tail(&local->qlinks, &ictx->pqueue);
                        ++(ictx->pending);
                        UNLOCK(&ictx->lock);
                        return 0;
                } else {
                        list_add_tail(&local->qlinks, &ictx->aqueue);
                        ++(ictx->active);
                }
        UNLOCK(&ictx->lock);
#endif

        return jbr_@NAME@_dispatch (frame, this, @SHORT_ARGS@);

err:
        if (local) {
                if (local->stub) {
                        call_stub_destroy(local->stub);
                }
                if (local->qstub) {
                        call_stub_destroy(local->qstub);
                }
                if (local->fd) {
                        fd_unref(local->fd);
                }
                mem_put(local);
        }
        STACK_UNWIND_STRICT (@NAME@, frame, -1, op_errno,
                             @ERROR_ARGS@);
        return 0;
}

/* template-name write-dispatch */
int32_t
jbr_@NAME@_dispatch (call_frame_t *frame, xlator_t *this,
                     @LONG_ARGS@)
{
        jbr_local_t     *local  = frame->local;
        jbr_private_t   *priv   = this->private;
        xlator_list_t   *trav;

        /*
         * TBD: unblock pending request(s) if we fail after this point but
         * before we get to jbr_@NAME@_complete (where that code currently
         * resides).
         */

        local->call_count = priv->n_children - 1;
        local->successful_acks = 0;
        for (trav = this->children->next; trav; trav = trav->next) {
                STACK_WIND (frame, jbr_@NAME@_fan_in,
                            trav->xlator, trav->xlator->fops->@NAME@,
                            @SHORT_ARGS@);
        }

        /* TBD: variable Issue count */
        return 0;
}

/* template-name write-fan-in */
int32_t
jbr_@NAME@_fan_in (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   @LONG_ARGS@)
{
        jbr_local_t     *local  = frame->local;
        uint8_t         call_count;

        gf_msg_trace (this->name, 0, "op_ret = %d, op_errno = %d\n",
                      op_ret, op_errno);

        LOCK(&frame->lock);
        call_count = --(local->call_count);
        if (op_ret != -1) {
                /* Increment the number of successful acks *
                 * received for the operation.             *
                 */
                (local->successful_acks)++;
                local->successful_op_ret = op_ret;
        }
        gf_msg_debug (this->name, 0, "succ_acks = %d, op_ret = %d, op_errno = %d\n",
                      op_ret, op_errno, local->successful_acks);
        UNLOCK(&frame->lock);

        /* TBD: variable Completion count */
        if (call_count == 0) {
                call_resume(local->stub);
        }

        return 0;
}

/* template-name write-continue */
int32_t
jbr_@NAME@_continue (call_frame_t *frame, xlator_t *this,
                     @LONG_ARGS@)
{
        int32_t          ret = -1;
        gf_boolean_t     result   = _gf_false;
        jbr_local_t     *local    = NULL;
        jbr_private_t   *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        priv = this->private;
        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        /* Perform quorum check to see if the leader needs     *
         * to perform the operation. If the operation will not *
         * meet quorum irrespective of the leader's result     *
         * there is no point in the leader performing the fop  *
         */
        result = fop_quorum_check (this, (double)priv->n_children,
                                   (double)local->successful_acks + 1);
        if (result == _gf_false) {
                gf_msg (this->name, GF_LOG_ERROR, EROFS,
                        J_MSG_QUORUM_NOT_MET, "Didn't receive enough acks "
                        "to meet quorum. Failing the operation without trying "
                        "it on the leader.");
                STACK_UNWIND_STRICT (@NAME@, frame, -1, EROFS,
                                     @ERROR_ARGS@);
        } else {
                STACK_WIND (frame, jbr_@NAME@_complete,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                            @SHORT_ARGS@);
        }

        ret = 0;
out:
        return ret;
}

/* template-name write-complete */
int32_t
jbr_@NAME@_complete (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     @LONG_ARGS@)
{
        gf_boolean_t     result         = _gf_false;
        jbr_private_t   *priv           = this->private;

        jbr_local_t *local = frame->local;

        /* If the fop failed on the leader, then reduce one succesful ack
         * before calculating the fop quorum
         */
        LOCK(&frame->lock);
        if (op_ret == -1)
                (local->successful_acks)--;
        UNLOCK(&frame->lock);

#if defined(JBR_CG_QUEUE)
        jbr_inode_ctx_t *ictx;
        jbr_local_t     *next;

        if (local->qlinks.next != &local->qlinks) {
                list_del(&local->qlinks);
                ictx = jbr_get_inode_ctx(this, local->fd->inode);
                if (ictx) {
                        LOCK(&ictx->lock);
                                if (ictx->pending) {
                                        /*
                                         * TBD: dequeue *all* non-conflicting
                                         * reqs
                                         *
                                         * With the stub implementation there
                                         * can only be one request active at a
                                         * time (zero here) so it's not an
                                         * issue.  In a real implementation
                                         * there might still be other active
                                         * requests to check against, and
                                         * multiple pending requests that could
                                         * continue.
                                         */
                                        gf_msg_debug (this->name, 0,
                                                     "unblocking next request");
                                        --(ictx->pending);
                                        next = list_entry (ictx->pqueue.next,
                                                           jbr_local_t, qlinks);
                                        list_del(&next->qlinks);
                                        list_add_tail(&next->qlinks,
                                                      &ictx->aqueue);
                                        call_resume(next->qstub);
                                } else {
                                        --(ictx->active);
                                }
                        UNLOCK(&ictx->lock);
                }
        }
#endif

#if defined(JBR_CG_FSYNC)
        jbr_mark_fd_dirty(this, local);
#endif

#if defined(JBR_CG_NEED_FD)
        fd_unref(local->fd);
#endif

        /* After the leader completes the fop, a quorum check is      *
         * performed, taking into account the outcome of the fop      *
         * on the leader. Irrespective of the fop being successful    *
         * or failing on the leader, the result of the quorum will    *
         * determine if the overall fop is successful or not. For     *
         * example, a fop might have succeeded on every node except   *
         * the leader, in which case as quorum is being met, the fop  *
         * will be treated as a successful fop, even though it failed *
         * on the leader. On follower nodes, no quorum check should   *
         * be done, and the result is returned to the leader as is.   *
         */
        if (priv->leader) {
                result = fop_quorum_check (this, (double)priv->n_children,
                                           (double)local->successful_acks + 1);
                if (result == _gf_false) {
                        op_ret = -1;
                        op_errno = EROFS;
                        gf_msg (this->name, GF_LOG_ERROR, EROFS,
                                J_MSG_QUORUM_NOT_MET, "Quorum is not met. "
                                "The operation has failed.");
                } else {
#if defined(JBR_CG_NEED_FD)
                        op_ret = local->successful_op_ret;
#else
                        op_ret = 0;
#endif
                        op_errno = 0;
                        gf_msg_debug (this->name, 0,
                                      "Quorum has met. The operation has succeeded.");
                }
        }

        STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno,
                             @SHORT_ARGS@);


        return 0;

}
