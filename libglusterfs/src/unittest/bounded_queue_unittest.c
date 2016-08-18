#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include "bounded-queue.h"

struct int_entry {
        struct queue_entry link;
        int val;
};

struct test_state {
        struct bounded_queue *q;
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        int successful_pushes;
        int failed_pushes;
        int successful_pops;
        int failed_pops;
};

static void *push_thread (void *arg) {
        struct test_state *state = arg;
        struct int_entry *e = malloc (sizeof (*e));
        int rc;

        e->val = 42;

        rc = bounded_queue_push (state->q, &e->link);

        pthread_mutex_lock (&state->mutex);
        if (!rc) {
                state->successful_pushes++;
        } else {
                free (e);
                state->failed_pushes++;
        }
        pthread_mutex_unlock (&state->mutex);
        pthread_cond_signal (&state->cond);

        return NULL;
}

static void *pop_thread (void *arg) {
        struct test_state *state = arg;
        struct int_entry *e;

        e = (struct int_entry *)bounded_queue_pop (state->q);

        pthread_mutex_lock (&state->mutex);
        if (e) {
                assert (e->val == 42);
                state->successful_pops++;
                free (e);
        } else {
                state->failed_pops++;
        }
        pthread_mutex_unlock (&state->mutex);
        pthread_cond_signal (&state->cond);

        return NULL;
}

int main (void) {
        struct bounded_queue q;
        struct int_entry *e;
        struct test_state state;
        int i, rc;

        rc = bounded_queue_init (&q, 10);

        for (i = 0; i < 10; ++i) {
                e = malloc (sizeof (*e));
                e->val = i;
                rc = bounded_queue_push (&q, &e->link);
                assert (rc == 0);
        }

        for (i = 0; i < 10; ++i) {
                e = (struct int_entry *)bounded_queue_pop (&q);
                assert (e);
                assert (e->val == i);
                free (e);
        }

        puts ("Single-threaded test passed.");

        memset (&state, 0, sizeof (state));
        pthread_mutex_init (&state.mutex, NULL);
        pthread_cond_init (&state.cond, NULL);
        state.q = &q;

        puts ("Starting 20 pusher threads.");

        for (i = 0; i < 20; ++i) {
                pthread_t t;
                pthread_create (&t, NULL, push_thread, (void *)&state);
                pthread_detach (t);
        }

        // Since queue limit is 10, 10 and only 10 pushers should succeed.
        puts ("Waiting for winners...");

        pthread_mutex_lock (&state.mutex);
        while (state.successful_pushes < 10) {
                pthread_cond_wait (&state.cond, &state.mutex);
        }
        pthread_mutex_unlock (&state.mutex);

        sleep (1);

        pthread_mutex_lock (&state.mutex);
        assert (state.successful_pushes == 10);
        assert (state.failed_pushes == 0);
        pthread_mutex_unlock (&state.mutex);

        // Kill the queue: all 10 blocked pushers should fail.
        bounded_queue_kill (&q);

        puts ("Waiting for losers...");

        pthread_mutex_lock (&state.mutex);
        while (state.failed_pushes != 10) {
                pthread_cond_wait (&state.cond, &state.mutex);
        }
        pthread_mutex_unlock (&state.mutex);

        pthread_mutex_lock (&state.mutex);
        assert (state.successful_pushes == 10);
        assert (state.failed_pushes == 10);
        pthread_mutex_unlock (&state.mutex);

        q.killed = 0;

        puts ("Starting 20 popper threads...");
        for (i = 0; i < 20; ++i) {
                pthread_t t;
                pthread_create (&t, NULL, pop_thread, (void *)&state);
                pthread_detach (t);
        }

        // We have 10 entries on the queue, so 10 pops should succeed.
        puts ("Waiting for winners...");

        pthread_mutex_lock (&state.mutex);
        while (state.successful_pops != 10) {
                pthread_cond_wait (&state.cond, &state.mutex);
        }
        pthread_mutex_unlock (&state.mutex);

        sleep (1);

        pthread_mutex_lock (&state.mutex);
        assert (state.successful_pops == 10);
        assert (state.failed_pops == 0);
        pthread_mutex_unlock (&state.mutex);

        bounded_queue_kill (&q);

        puts ("Waiting for losers...");

        pthread_mutex_lock (&state.mutex);
        while (state.failed_pops != 10) {
                pthread_cond_wait (&state.cond, &state.mutex);
        }
        pthread_mutex_unlock (&state.mutex);

        sleep (1);

        pthread_mutex_lock (&state.mutex);
        assert (state.successful_pops == 10);
        assert (state.failed_pops == 10);
        pthread_mutex_unlock (&state.mutex);

        puts ("Multi-threaded tests passed.");

        pthread_cond_destroy (&state.cond);
        pthread_mutex_destroy (&state.mutex);

        return 0;
}

