#include "bounded-queue.h"

#include <assert.h>

/// @brief Initialize a bounded queue structure.
///
/// @returns zero on success.
int bounded_queue_init (struct bounded_queue *q, uint32_t limit) {
        int rc;

        q->head = NULL;
        q->tail = NULL;
        q->count = 0;
        q->limit = limit;
        q->killed = 0;
        rc = pthread_mutex_init (&q->mutex, NULL);
        rc |= pthread_cond_init (&q->cond, NULL);

        return rc;
}

/// @brief Push an entry onto the bounded queue tail.
///
/// If the queue is full, will block until space is available
/// or someone calls bounded_queue_kill().
///
/// @returns zero on success.
int bounded_queue_push (struct bounded_queue *q, struct queue_entry *e) {
        int rc;

        if ((rc = pthread_mutex_lock (&q->mutex)) != 0) {
                return rc;
        }
        while (q->count >= q->limit && !q->killed) {
                if (pthread_cond_wait (&q->cond, &q->mutex) || q->killed) {
                        pthread_mutex_unlock (&q->mutex);
                        return -1;
                }
        }

        e->next = NULL;
        if (!q->head) {
                assert (q->count == 0);
                q->head = e;
        }
        if (q->tail) {
                q->tail->next = e;
        }
        q->tail = e;

        q->count++;
        rc = pthread_mutex_unlock (&q->mutex);
        rc |= pthread_cond_signal (&q->cond);
        return rc;
}

/// @brief Pop an entry from the bounded queue head.
///
/// If the queue is empty, blocks until an entry is pushed or
/// someone calls bounded_queue_kill().
///
/// @returns valid pointer on success, NULL on any error.
struct queue_entry *bounded_queue_pop (struct bounded_queue *q) {
        struct queue_entry *e;

        if (pthread_mutex_lock (&q->mutex)) {
                return NULL;
        }
        while (q->count == 0 && !q->killed) {
                if (pthread_cond_wait (&q->cond, &q->mutex) || q->killed) {
                        pthread_mutex_unlock (&q->mutex);
                        return NULL;
                }
        }

        e = q->head;
        q->head = e->next;
        if (q->tail == e) {
                assert (q->count == 1);
                q->tail = NULL;
        }

        q->count--;
        pthread_mutex_unlock (&q->mutex);
        pthread_cond_signal (&q->cond);

        return e;
}

/// @brief Make all current and future calls to push/pop fail immediately.
///
/// @returns zero on success.
int bounded_queue_kill (struct bounded_queue *q) {
        int rc;

        rc = pthread_mutex_lock (&q->mutex);
        q->killed = 1;
        rc |= pthread_cond_broadcast (&q->cond);
        rc |= pthread_mutex_unlock (&q->mutex);

        return rc;
}
