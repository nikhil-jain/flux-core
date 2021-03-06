/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "waitqueue.h"
#include "msg_cb_handler.h"

#define WAIT_MAGIC 0xafad7777
struct wait_struct {
    int magic;
    int usecount;
    wait_cb_f cb;
    void *cb_arg;
    msg_cb_handler_t *mcb;      /* optional special case */
};

#define WAITQUEUE_MAGIC 0xafad7778
struct waitqueue_struct {
    int magic;
    zlist_t *q;
};

int wait_get_usecount (wait_t *w)
{
    return w->usecount;
}

wait_t *wait_create (wait_cb_f cb, void *arg)
{
    wait_t *w = calloc (1, sizeof (*w));
    if (!w) {
        errno = ENOMEM;
        return NULL;
    }
    w->magic = WAIT_MAGIC;
    w->cb = cb;
    w->cb_arg = arg;
    return w;
}

wait_t *wait_create_msg_handler (flux_t *h, flux_msg_handler_t *mh,
                                 const flux_msg_t *msg, void *arg,
                                 flux_msg_handler_f cb)
{
    wait_t *w = wait_create (NULL, NULL);
    if (w) {
        if (!(w->mcb = msg_cb_handler_create (h, mh, msg, arg, cb))) {
            int saved_errno = errno;
            wait_destroy (w);
            errno = saved_errno;
            return NULL;
        }
    }
    return w;
}

void wait_destroy (wait_t *w)
{
    if (w) {
        assert (w->magic == WAIT_MAGIC);
        assert (w->usecount == 0);
        msg_cb_handler_destroy (w->mcb);
        w->magic = ~WAIT_MAGIC;
        free (w);
    }
}

waitqueue_t *wait_queue_create (void)
{
    waitqueue_t *q = calloc (1, sizeof (*q));
    if (!q) {
        errno = ENOMEM;
        return NULL;
    }
    if (!(q->q = zlist_new ())) {
        free (q);
        errno = ENOMEM;
        return NULL;
    }
    q->magic = WAITQUEUE_MAGIC;
    return q;
}

void wait_queue_destroy (waitqueue_t *q)
{
    if (q) {
        wait_t *w;
        assert (q->magic == WAITQUEUE_MAGIC);
        while ((w = zlist_pop (q->q))) {
            if (--w->usecount == 0)
                wait_destroy (w);
        }
        zlist_destroy (&q->q);
        q->magic = ~WAITQUEUE_MAGIC;
        free (q);
    }
}

int wait_queue_length (waitqueue_t *q)
{
    assert (q->magic == WAITQUEUE_MAGIC);
    return zlist_size (q->q);
}

int wait_addqueue (waitqueue_t *q, wait_t *w)
{
    assert (q->magic == WAITQUEUE_MAGIC);
    assert (w->magic == WAIT_MAGIC);
    if (zlist_append (q->q, w) < 0) {
        errno = ENOMEM;
        return -1;
    }
    w->usecount++;
    return 0;
}

static void wait_runone (wait_t *w)
{
    if (--w->usecount == 0) {
        if (w->cb)
            w->cb (w->cb_arg);
        else if (w->mcb)
            msg_cb_handler_call (w->mcb);
        wait_destroy (w);
    }
}

int wait_runqueue (waitqueue_t *q)
{
    assert (q->magic == WAITQUEUE_MAGIC);
    /* N.B. for safety on errors, we must copy all elements off of
     * q->q or none, otherwise it's not clear what's to be done
     * otherwise. e.g. if code was
     * while ((w = zlist_pop (q->q))) {
     *    if (zlist_append (cpy, w) < 0) {
     *        what to do on error here?
     *        pop off all of q?
     *        call wait_runone() on cpy but not on rest of q->q?
     *    }
     * }
     */
    if (zlist_size (q->q) > 0) {
        zlist_t *cpy = NULL;
        wait_t *w;
        if (!(cpy = zlist_dup (q->q))) {
            errno = ENOMEM;
            return -1;
        }
        zlist_purge (q->q);
        while ((w = zlist_pop (cpy)))
            wait_runone (w);
        zlist_destroy (&cpy);
    }
    return 0;
}

int wait_destroy_msg (waitqueue_t *q, wait_test_msg_f cb, void *arg)
{
    zlist_t *tmp = NULL;
    wait_t *w;
    int rc = -1;
    int count = 0;
    int saved_errno;

    assert (q->magic == WAITQUEUE_MAGIC);

    w = zlist_first (q->q);
    while (w) {
        const flux_msg_t *msgcpy = NULL;

        if (w->mcb)
            msgcpy = msg_cb_handler_get_msgcopy (w->mcb);
        if (msgcpy && cb != NULL && cb (msgcpy, arg)) {
            if (!tmp && !(tmp = zlist_new ())) {
                saved_errno = ENOMEM;
                goto error;
            }
            if (zlist_append (tmp, w) < 0) {
                saved_errno = ENOMEM;
                goto error;
            }
            /* prevent wait_runone from restarting handler by clearing
             * callback function */
            msg_cb_handler_set_cb (w->mcb, NULL);
            count++;
        }
        w = zlist_next (q->q);
    }
    rc = 0;
    if (tmp) {
        while ((w = zlist_pop (tmp))) {
            zlist_remove (q->q, w);
            if (--w->usecount == 0)
                wait_destroy (w);
        }
    }
    rc = count;
error:
    /* if an error occurs above in zlist_new() or zlist_append(),
     * simply destroy the tmp list.  Nothing has been removed off of
     * the original queue yet.  Allow user to handle error as they see
     * fit.
     */
    zlist_destroy (&tmp);
    if (rc < 0)
        errno = saved_errno;
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

