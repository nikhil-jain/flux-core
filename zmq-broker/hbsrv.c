/* hbsrv.c - generate session heartbeat */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#define MAX_SYNC_PERIOD_SEC 30*60.0

static int epoch = 0;

static int timeout_cb (flux_t h, void *arg)
{
    json_object *o = util_json_object_new_object ();
    int rc = -1;
    util_json_object_add_int (o, "epoch", ++epoch);
    if (flux_event_send (h, o, "hb") < 0) {
        err ("flux_event_send");
        goto done;
    }
    rc = 0;
done:
    json_object_put (o);
    return rc;
}

static void set_config (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    flux_t h = arg;
    double val;
    char *key = NULL;

    if (errnum > 0) {
        flux_log (h, LOG_ERR, "%s: %s", key, strerror (errnum));
        goto done;
    }
    key = kvsdir_key_at (dir, "period-sec");
    if (kvs_get_double (h, key, &val) < 0) {
        flux_log (h, LOG_ERR, "%s: %s", key, strerror (errno));
        goto done;
    }
    if (val == NAN || val <= 0 || val > MAX_SYNC_PERIOD_SEC) {
        flux_log (h, LOG_ERR, "%s: %.1f out of range (0 < sec < %.1f", key,
                  val, MAX_SYNC_PERIOD_SEC);
        goto done;
    }
    if (flux_timeout_set (h, (int)(val * 1000)) < 0) { /* msec */
        flux_log (h, LOG_ERR, "flux_timeout_set %d", (int)(val * 1000));
        goto done;
    }
    flux_log (h, LOG_INFO, "heartbeat period set to %.1fs", val);
done:
    if (key)
        free (key);
}

static int hbsrv_main (flux_t h, zhash_t *args)
{
    if (flux_tmouthandler_set (h, timeout_cb, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_tmouthandler_set: %s", strerror (errno));
        return -1;
    }
    if (kvs_watch_dir (h, set_config, h, "conf.hb") < 0) {
        flux_log (h, LOG_ERR, "kvs_watch_dir conf.hb: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

const struct plugin_ops ops = {
    .main    = hbsrv_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */