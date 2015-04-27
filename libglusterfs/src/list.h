/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _LLIST_H
#define _LLIST_H


struct list_head {
	struct list_head *next;
	struct list_head *prev;
};


#define INIT_LIST_HEAD(head) do {			\
		(head)->next = (head)->prev = head;	\
	} while (0)


static inline void
list_add (struct list_head *new, struct list_head *head)
{
	new->prev = head;
	new->next = head->next;

	new->prev->next = new;
	new->next->prev = new;
}


static inline void
list_add_tail (struct list_head *new, struct list_head *head)
{
	new->next = head;
	new->prev = head->prev;

	new->prev->next = new;
	new->next->prev = new;
}


/* This function will insert the element to the list in a order.
   Order will be based on the compare function provided as a input.
   If element to be inserted in ascending order compare should return:
    0: if both the arguments are equal
   >0: if first argument is greater than second argument
   <0: if first argument is less than second argument */
static inline void
list_add_order (struct list_head *new, struct list_head *head,
                int (*compare)(struct list_head *, struct list_head *))
{
        struct list_head *pos = head->prev;

        while ( pos != head ) {
                if (compare(new, pos) >= 0)
                        break;

                /* Iterate the list in the reverse order. This will have
                   better efficiency if the elements are inserted in the
                   ascending order */
                pos = pos->prev;
        }

        list_add (new, pos);
}

static inline void
list_del (struct list_head *old)
{
	old->prev->next = old->next;
	old->next->prev = old->prev;

	old->next = (void *)0xbabebabe;
	old->prev = (void *)0xcafecafe;
}


static inline void
list_del_init (struct list_head *old)
{
	old->prev->next = old->next;
	old->next->prev = old->prev;

	old->next = old;
	old->prev = old;
}


static inline void
list_move (struct list_head *list, struct list_head *head)
{
	list_del (list);
	list_add (list, head);
}


static inline void
list_move_tail (struct list_head *list, struct list_head *head)
{
	list_del (list);
	list_add_tail (list, head);
}


static inline int
list_empty (struct list_head *head)
{
	return (head->next == head);
}


static inline void
__list_splice (struct list_head *list, struct list_head *head)
{
	(list->prev)->next = (head->next);
	(head->next)->prev = (list->prev);

	(head)->next = (list->next);
	(list->next)->prev = (head);
}


static inline void
list_splice (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_splice (list, head);
}


/* Splice moves @list to the head of the list at @head. */
static inline void
list_splice_init (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_splice (list, head);
	INIT_LIST_HEAD (list);
}


static inline void
__list_append (struct list_head *list, struct list_head *head)
{
	(head->prev)->next = (list->next);
        (list->next)->prev = (head->prev);
        (head->prev) = (list->prev);
        (list->prev)->next = head;
}


static inline void
list_append (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_append (list, head);
}


/* Append moves @list to the end of @head */
static inline void
list_append_init (struct list_head *list, struct list_head *head)
{
	if (list_empty (list))
		return;

	__list_append (list, head);
	INIT_LIST_HEAD (list);
}

static inline int
list_is_last (struct list_head *list, struct list_head *head)
{
        return (list->next == head);
}

static inline int
list_is_singular(struct list_head *head)
{
        return !list_empty(head) && (head->next == head->prev);
}

/**
 * list_replace - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * If @old was empty, it will be overwritten.
 */
static inline void list_replace(struct list_head *old,
				struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

static inline void list_replace_init(struct list_head *old,
                                     struct list_head *new)
{
	list_replace(old, new);
	INIT_LIST_HEAD(old);
}

/**
 * list_rotate_left - rotate the list to the left
 * @head: the head of the list
 */
static inline void list_rotate_left (struct list_head *head)
{
	struct list_head *first;

	if (!list_empty (head)) {
		first = head->next;
		list_move_tail (first, head);
	}
}

#define list_entry(ptr, type, member)					\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define list_first_entry(ptr, type, member)     \
        list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member)     \
        list_entry((ptr)->prev, type, member)

#define list_next_entry(pos, member) \
        list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_prev_entry(pos, member) \
        list_entry((pos)->member.prev, typeof(*(pos)), member)

#define list_for_each(pos, head)                                        \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.next, typeof(*pos), member))


#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
		n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

#define list_for_each_entry_reverse(pos, head, member)                  \
	for (pos = list_entry((head)->prev, typeof(*pos), member);      \
	     &pos->member != (head);                                    \
	     pos = list_entry(pos->member.prev, typeof(*pos), member))


#define list_for_each_entry_safe_reverse(pos, n, head, member)          \
	for (pos = list_entry((head)->prev, typeof(*pos), member),      \
	        n = list_entry(pos->member.prev, typeof(*pos), member); \
	     &pos->member != (head);                                    \
	     pos = n, n = list_entry(n->member.prev, typeof(*n), member))

/*
 * This list implementation has some advantages, but one disadvantage: you
 * can't use NULL to check whether you're at the head or tail.  Thus, the
 * address of the head has to be an argument for these macros.
 */

#define list_next(ptr, head, type, member)      \
        (((ptr)->member.next == head) ? NULL    \
                                 : list_entry((ptr)->member.next, type, member))

#define list_prev(ptr, head, type, member)      \
        (((ptr)->member.prev == head) ? NULL    \
                                 : list_entry((ptr)->member.prev, type, member))

#endif /* _LLIST_H */
