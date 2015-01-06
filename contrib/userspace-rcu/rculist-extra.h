/*
 * Copyright (C) 2002 Free Software Foundation, Inc.
 * (originally part of the GNU C Library)
 * Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.
 *
 * Copyright (C) 2009 Pierre-Marc Fournier
 * Conversion to RCU list.
 * Copyright (C) 2010 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef URCU_RCULIST_EXTRA_H
#define URCU_RCULIST_EXTRA_H
/* Copying this definition from liburcu-0.8 as liburcu-0.7 does not have this
 * particular list api
 */
/* Add new element at the tail of the list. */

static inline
void cds_list_add_tail_rcu(struct cds_list_head *newp,
                struct cds_list_head *head)
{
        newp->next = head;
        newp->prev = head->prev;
        rcu_assign_pointer(head->prev->next, newp);
        head->prev = newp;
}

#endif
