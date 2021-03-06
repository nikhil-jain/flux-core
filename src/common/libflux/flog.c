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
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>
#include <zmq.h>

#include "flog.h"
#include "info.h"
#include "message.h"
#include "rpc.h"

#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"

typedef struct {
    char appname[STDLOG_MAX_APPNAME + 1];
    char procid[STDLOG_MAX_PROCID + 1];
    char buf[FLUX_MAX_LOGBUF + 1];
    flux_log_f cb;
    void *cb_arg;
} logctx_t;

static void freectx (void *arg)
{
    logctx_t *ctx = arg;
    free (ctx);
}

static logctx_t *logctx_new (flux_t *h)
{
    logctx_t *ctx;
    extern char *__progname;
    // or glib-ism: program_invocation_short_name

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    snprintf (ctx->procid, sizeof (ctx->procid), "%d", getpid ());
    snprintf (ctx->appname, sizeof (ctx->appname), "%s", __progname);
    flux_aux_set (h, "flux::log", ctx, freectx);
    return ctx;
}

static logctx_t *getctx (flux_t *h)
{
    logctx_t *ctx = (logctx_t *)flux_aux_get (h, "flux::log");
    if (!ctx)
        ctx = logctx_new (h);
    return ctx;
}

void flux_log_set_appname (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    if (ctx)
        snprintf (ctx->appname, sizeof (ctx->appname), "%s", s);
}

void flux_log_set_procid (flux_t *h, const char *s)
{
    logctx_t *ctx = getctx (h);
    if (ctx)
        snprintf (ctx->procid, sizeof (ctx->procid), "%s", s);
}


void flux_log_set_redirect (flux_t *h, flux_log_f fun, void *arg)
{
    logctx_t *ctx = getctx (h);
    if (ctx) {
        ctx->cb = fun;
        ctx->cb_arg = arg;
    }
}

const char *flux_strerror (int errnum)
{
    /* zeromq-4.2.1 reports EHOSTUNREACH as "Host unreachable",
     * but "No route to host" is canonical on Linux and we have some
     * tests that depend on it, so remap here.
     */
    if (errnum == EHOSTUNREACH)
        return "No route to host";
    return zmq_strerror (errnum);
}

static int log_rpc (flux_t *h, const char *buf, int len, int flags)
{
    flux_future_t *f;
    int rc = (flags & FLUX_RPC_NORESPONSE) ? 0 : -1;

    if (!(f = flux_rpc_raw (h, "log.append", buf, len, FLUX_NODEID_ANY, flags)))
        goto done;
    if ((flags & FLUX_RPC_NORESPONSE))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int flux_vlog (flux_t *h, int level, const char *fmt, va_list ap)
{
    logctx_t *ctx;
    int saved_errno = errno;
    uint32_t rank;
    int len;
    char timestamp[WALLCLOCK_MAXLEN];
    char hostname[STDLOG_MAX_HOSTNAME + 1];
    struct stdlog_header hdr;
    int rpc_flags = FLUX_RPC_NORESPONSE;

    if (!h) {
        char buf[FLUX_MAX_LOGBUF + 1];
        const char *lstr = stdlog_severity_to_string (LOG_PRI (level));

        (void)vsnprintf (buf, sizeof (buf), fmt, ap);
        return fprintf (stderr, "%s: %s\n", lstr, buf);
    }

    if (!(ctx = getctx (h))) {
        errno = ENOMEM;
        goto fatal;
    }

    if ((level & FLUX_LOG_CHECK)) {
        rpc_flags &= ~FLUX_RPC_NORESPONSE;
        level &= ~FLUX_LOG_CHECK;
    }

    stdlog_init (&hdr);
    hdr.pri = STDLOG_PRI (level, LOG_USER);
    if (wallclock_get_zulu (timestamp, sizeof (timestamp)) >= 0)
        hdr.timestamp = timestamp;
    if (flux_get_rank (h, &rank) == 0) {
        snprintf (hostname, sizeof (hostname), "%" PRIu32, rank);
        hdr.hostname = hostname;
    }
    hdr.appname = ctx->appname;
    hdr.procid = ctx->procid;

    len = stdlog_vencodef (ctx->buf, sizeof (ctx->buf), &hdr,
                           STDLOG_NILVALUE, fmt, ap);
    if (len >= sizeof (ctx->buf))
        len = sizeof (ctx->buf);
    if (ctx->cb) {
        ctx->cb (ctx->buf, len, ctx->cb_arg);
    } else {
        if (log_rpc (h, ctx->buf, len, rpc_flags) < 0)
            goto fatal;
    }
    errno = saved_errno;
    return 0;
fatal:
    FLUX_FATAL (h);
    return -1;
}

int flux_log (flux_t *h, int lev, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vlog (h, lev, fmt, ap);
    va_end (ap);
    return rc;
}

void flux_log_verror (flux_t *h, const char *fmt, va_list ap)
{
    int saved_errno = errno;
    char buf[FLUX_MAX_LOGBUF + 1];

    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    flux_log (h, LOG_ERR, "%s: %s", buf, flux_strerror (saved_errno));
    errno = saved_errno;
}

void flux_log_error (flux_t *h, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    flux_log_verror (h, fmt, ap);
    va_end (ap);
}

static int dmesg_clear (flux_t *h, int seq)
{
    flux_future_t *f;
    int rc = -1;

    if (!(f = flux_rpc_pack (h, "log.clear", FLUX_NODEID_ANY, 0,
                             "{s:i}", "seq", seq)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static flux_future_t *dmesg_rpc (flux_t *h, int seq, bool follow)
{
    return flux_rpc_pack (h, "log.dmesg", FLUX_NODEID_ANY, 0,
                          "{s:i s:b}", "seq", seq, "follow", follow);
}

static int dmesg_rpc_get (flux_future_t *f, int *seq, flux_log_f fun, void *arg)
{
    const char *buf;
    int rc = -1;

    if (flux_rpc_get_unpack (f, "{s:i s:s}", "seq", seq, "buf", &buf) < 0)
        goto done;
    fun (buf, strlen (buf), arg);
    rc = 0;
done:
    return rc;
}

int flux_dmesg (flux_t *h, int flags, flux_log_f fun, void *arg)
{
    int rc = -1;
    int seq = -1;
    bool eof = false;
    bool follow = false;

    if (flags & FLUX_DMESG_FOLLOW)
        follow = true;
    if (fun) {
        while (!eof) {
            flux_future_t *f;
            if (!(f = dmesg_rpc (h, seq, follow)))
                goto done;
            if (dmesg_rpc_get (f, &seq, fun, arg) < 0) {
                if (errno != ENOENT) {
                    flux_future_destroy (f);
                    goto done;
                }
                eof = true;
            }
            flux_future_destroy (f);
        }
    }
    if ((flags & FLUX_DMESG_CLEAR)) {
        if (dmesg_clear (h, seq) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

void flux_log_fprint (const char *buf, int len, void *arg)
{
    FILE *f = arg;
    struct stdlog_header hdr;
    const char *msg;
    int msglen, severity;
    uint32_t nodeid;

    if (f) {
        if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
            fprintf (f, "%.*s\n", len, buf);
        else {
            nodeid = strtoul (hdr.hostname, NULL, 10);
            severity = STDLOG_SEVERITY (hdr.pri);
            fprintf (f, "%s %s.%s[%" PRIu32 "]: %.*s\n",
                     hdr.timestamp,
                     hdr.appname,
                     stdlog_severity_to_string (severity),
                     nodeid,
                     msglen, msg);
        }
        fflush (f);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
