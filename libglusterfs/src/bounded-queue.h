#pragma once

#include <stdint.h>
#include <pthread.h>

struct queue_entry {
        struct queue_entry *next;
};

/// @brief a FIFO queue that holds a limited number of elements.
///
/// The queue is blocking, i.e. any attempt to push to a full
/// queue or read from an empty one will block indefinitely.
///
/// When tearing down the queue, blocked processes may be
/// unblocked by calling bounded_queue_kill(), which will
/// cause all blocked processes to return with error status.
struct bounded_queue {
        struct queue_entry *head;
        struct queue_entry *tail;
        uint32_t count;
        uint32_t limit;
        int killed;
        pthread_mutex_t mutex;
        pthread_cond_t cond;
};

int bounded_queue_init (struct bounded_queue *q, uint32_t limit);
int bounded_queue_push (struct bounded_queue *q, struct queue_entry *e);
struct queue_entry *bounded_queue_pop (struct bounded_queue *q);
int bounded_queue_kill (struct bounded_queue *q);

