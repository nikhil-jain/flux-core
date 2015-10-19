#include <errno.h>
#include <czmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "src/common/libflux/reactor.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

static const size_t zmqwriter_msgcount = 1024;

static void zmqwriter (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *sock = flux_zmq_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        uint8_t blob[64];
        zmsg_t *zmsg = zmsg_new ();
        if (!zmsg || zmsg_addmem (zmsg, blob, sizeof (blob)) < 0) {
            fprintf (stderr, "%s: failed to create message: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (zmsg_send (&zmsg, sock) < 0) {
            fprintf (stderr, "%s: zmsg_send: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        count++;
        if (count == zmqwriter_msgcount)
            flux_watcher_stop (w);
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void zmqreader (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *sock = flux_zmq_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        zmsg_t *zmsg = zmsg_recv (sock);
        if (!zmsg) {
            fprintf (stderr, "%s: zmsg_recv: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        zmsg_destroy (&zmsg);
        count++;
        if (count == zmqwriter_msgcount)
            flux_watcher_stop (w);
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void test_zmq (flux_reactor_t *reactor)
{
    zctx_t *zctx;
    void *zs[2];
    flux_watcher_t *r, *w;

    ok ((zctx = zctx_new ()) != NULL,
        "zmq: created zmq context");
    zs[0] = zsocket_new (zctx, ZMQ_PAIR);
    zs[1] = zsocket_new (zctx, ZMQ_PAIR);
    ok (zs[0] && zs[1]
        && zsocket_bind (zs[0], "inproc://test_zmq") == 0
        && zsocket_connect (zs[1], "inproc://test_zmq") == 0,
        "zmq: connected ZMQ_PAIR sockets over inproc");
    r = flux_zmq_watcher_create (reactor, zs[0], FLUX_POLLIN, zmqreader, NULL);
    w = flux_zmq_watcher_create (reactor, zs[1], FLUX_POLLOUT, zmqwriter, NULL);
    ok (r != NULL && w != NULL,
        "zmq: nonblocking reader and writer created");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run  (reactor, 0) == 0,
        "zmq: reactor ran to completion after %d messages", zmqwriter_msgcount);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);

    zsocket_destroy (zctx, zs[0]);
    zsocket_destroy (zctx, zs[1]);
    zctx_destroy (&zctx);
}

static const size_t fdwriter_bufsize = 10*1024*1024;

static void fdwriter (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        if ((n = write (fd, buf + count, fdwriter_bufsize - count)) < 0
                                && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: write failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}
static void fdreader (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        if ((n = read (fd, buf + count, fdwriter_bufsize - count)) < 0
                            && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: read failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static int set_nonblock (int fd)
{
    int flags = fcntl (fd, F_GETFL, NULL);
    if (flags < 0 || fcntl (fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf (stderr, "fcntl: %s\n", strerror (errno));
        return -1;
    }
    return 0;
}

static void test_fd (flux_reactor_t *reactor)
{
    int fd[2];
    flux_watcher_t *r, *w;

    ok (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) == 0
        && set_nonblock (fd[0]) == 0 && set_nonblock (fd[1]) == 0,
        "fd: successfully created non-blocking socketpair");
    r = flux_fd_watcher_create (reactor, fd[0], FLUX_POLLIN, fdreader, NULL);
    w = flux_fd_watcher_create (reactor, fd[1], FLUX_POLLOUT, fdwriter, NULL);
    ok (r != NULL && w != NULL,
        "fd: reader and writer created");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "fd: reactor ran to completion after %lu bytes", fdwriter_bufsize);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);
    close (fd[0]);
    close (fd[1]);
}

static int repeat_countdown = 10;
static void repeat (flux_reactor_t *r, flux_watcher_t *w,
                    int revents, void *arg)
{
    repeat_countdown--;
    if (repeat_countdown == 0)
        flux_watcher_stop (w);
}

static bool oneshot_ran = false;
static int oneshot_errno = 0;
static void oneshot (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    oneshot_ran = true;
    if (oneshot_errno != 0) {
        errno = oneshot_errno;
        flux_reactor_stop_error (r);
    }
}

static void test_timer (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    errno = 0;
    ok (!flux_timer_watcher_create (reactor, -1, 0, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative timeout fails with EINVAL");
    ok (!flux_timer_watcher_create (reactor, 0, -1, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative repeat fails with EINVAL");
    ok ((w = flux_timer_watcher_create (reactor, 0, 0, oneshot, NULL)) != NULL,
        "timer: creating zero timeout works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor ran to completion (single oneshot)");
    ok (oneshot_ran == true,
        "timer: oneshot was executed");
    oneshot_ran = false;
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor ran to completion (expired oneshot)");
    ok (oneshot_ran == false,
        "timer: expired oneshot was not re-executed");

    errno = 0;
    oneshot_errno = ESRCH;
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) < 0 && errno == ESRCH,
        "general: reactor stop_error worked with errno passthru");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    ok ((w = flux_timer_watcher_create (reactor, 0.01, 0.01, repeat, NULL))
        != NULL,
        "timer: creating 1ms timeout with 1ms repeat works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor ran to completion (single repeat)");
    ok (repeat_countdown == 0,
        "timer: repeat timer stopped itself after countdown");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);
}

static int idle_count = 0;
static void idle_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    if (++idle_count == 42)
        flux_watcher_stop (w);
}

static void test_idle (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    w = flux_idle_watcher_create (reactor, idle_cb, NULL);
    ok (w != NULL,
        "created idle watcher");
    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");
    ok (idle_count == 42,
        "idle watcher ran until stopped");
    flux_watcher_destroy (w);
}

static int prepare_count = 0;
static void prepare_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    prepare_count++;
}

static int check_count = 0;
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    check_count++;
}

static int prepchecktimer_count = 0;
static void prepchecktimer_cb (flux_reactor_t *r, flux_watcher_t *w,
                               int revents, void *arg)
{
    if (++prepchecktimer_count == 8)
        flux_reactor_stop (r);
}

static void test_prepcheck (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_watcher_t *prep;
    flux_watcher_t *chk;

    w = flux_timer_watcher_create (reactor, 0.01, 0.01,
                                   prepchecktimer_cb, NULL);
    ok (w != NULL,
        "created timer watcher that fires every 0.01s");
    flux_watcher_start (w);

    prep = flux_prepare_watcher_create (reactor, prepare_cb, NULL);
    ok (w != NULL,
        "created prepare watcher");
    flux_watcher_start (prep);

    chk = flux_check_watcher_create (reactor, check_cb, NULL);
    ok (w != NULL,
        "created check watcher");
    flux_watcher_start (chk);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");
    ok (prepchecktimer_count == 8,
        "timer fired 8 times, then reactor was stopped");
    diag ("prep %d check %d timer %d", prepare_count, check_count,
                                       prepchecktimer_count);
    ok (prepare_count >= 8,
        "prepare watcher ran at least once per timer");
    ok (check_count >= 8,
        "check watcher ran at least once per timer");

    flux_watcher_destroy (w);
    flux_watcher_destroy (prep);
    flux_watcher_destroy (chk);
}

static int sigusr1_count = 0;
static void sigusr1_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    if (++sigusr1_count == 8)
        flux_reactor_stop (r);
}

static void sigidle_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    if (kill (getpid (), SIGUSR1) < 0)
        flux_reactor_stop_error (r);
}

static void test_signal (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_watcher_t *idle;

    w = flux_signal_watcher_create (reactor, SIGUSR1, sigusr1_cb, NULL);
    ok (w != NULL,
        "created signal watcher");
    flux_watcher_start (w);

    idle = flux_idle_watcher_create (reactor, sigidle_cb, NULL);
    ok (idle != NULL,
        "created idle watcher");
    flux_watcher_start (idle);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");
    ok (sigusr1_count == 8,
        "signal watcher handled correct number of SIGUSR1's");

    flux_watcher_destroy (w);
    flux_watcher_destroy (idle);
}

static pid_t child_pid = -1;
static void child_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    int pid = flux_child_watcher_get_rpid (w);
    int rstatus = flux_child_watcher_get_rstatus (w);
    ok (pid == child_pid,
        "child watcher called with expected rpid");
    ok (WIFSIGNALED (rstatus) && WTERMSIG (rstatus) == SIGHUP,
        "child watcher called with expected rstatus");
    flux_watcher_stop (w);
}

static void test_child  (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_reactor_t *r;

    child_pid = fork ();
    if (child_pid == 0) {
        pause ();
        exit (0);
    }
    errno = 0;
    w = flux_child_watcher_create (reactor, child_pid, false, child_cb, NULL);
    ok (w == NULL && errno == EINVAL,
        "child watcher failed with EINVAL on non-SIGCHLD reactor");
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "created reactor with SIGCHLD flag");
    w = flux_child_watcher_create (r, child_pid, false, child_cb, NULL);
    ok (w != NULL,
        "created child watcher");

    ok (kill (child_pid, SIGHUP) == 0,
        "sent child SIGHUP");
    flux_watcher_start (w);

    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran successfully");
    flux_watcher_destroy (w);
    flux_reactor_destroy (r);
}

static int stat_size = 0;
static int stat_nlink = 0;
static void stat_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct stat new, old;
    flux_stat_watcher_get_rstat (w, &new, &old);
    if (new.st_nlink == 0) {
        diag ("%s: nlink: old: %d new %d", __FUNCTION__,
                old.st_nlink, new.st_nlink);
        stat_nlink++;
        flux_watcher_stop (w);
    } else {
        if (old.st_size != new.st_size) {
            diag ("%s: size: old=%ld new=%ld", __FUNCTION__,
                  (long)old.st_size, (long)new.st_size);
            stat_size++;
        }
    }
}

struct stattimer_ctx {
    int fd;
    char *path;
    enum { STATTIMER_APPEND, STATTIMER_UNLINK } state;
};

static void stattimer_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    struct stattimer_ctx *ctx = arg;
    if (ctx->state == STATTIMER_APPEND) {
        if (write (ctx->fd, "hello\n", 6) < 0 || close (ctx->fd) < 0)
            flux_reactor_stop_error (r);
        ctx->state = STATTIMER_UNLINK;
    } else if (ctx->state == STATTIMER_UNLINK) {
        if (unlink (ctx->path) < 0)
            flux_reactor_stop_error (r);
        flux_watcher_stop (w);
    }
}

static void test_stat (flux_reactor_t *reactor)
{
    flux_watcher_t *w, *tw;
    struct stattimer_ctx ctx;
    const char *tmpdir = getenv ("TMPDIR");

    ctx.path = xasprintf ("%s/reactor-test.XXXXXX", tmpdir ? tmpdir : "/tmp");
    ctx.fd = mkstemp (ctx.path);
    ctx.state = STATTIMER_APPEND;

    ok (ctx.fd >= 0,
        "created temporary file");
    w = flux_stat_watcher_create (reactor, ctx.path, 0., stat_cb, NULL);
    ok (w != NULL,
        "created stat watcher");
    flux_watcher_start (w);

    tw = flux_timer_watcher_create (reactor, 0.01, 0.01,
                                    stattimer_cb, &ctx);
    ok (tw != NULL,
        "created timer watcher");
    flux_watcher_start (tw);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");

    ok (stat_size == 1,
        "stat watcher invoked once for size chnage");
    ok (stat_nlink == 1,
        "stat watcher invoked once for nlink set to zero");

    flux_watcher_destroy (w);
    flux_watcher_destroy (tw);
}

static void reactor_destroy_early (void)
{
    flux_reactor_t *r;
    flux_watcher_t *w;

    if (!(r = flux_reactor_create (0)))
        exit (1);
    if (!(w = flux_idle_watcher_create (r, NULL, NULL)))
        exit (1);
    flux_watcher_start (w);
    flux_reactor_destroy (r);
    flux_watcher_destroy (w);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran to completion (no watchers)");

    test_timer (reactor);
    test_fd (reactor);
    test_zmq (reactor);
    test_idle (reactor);
    test_prepcheck (reactor);
    test_signal (reactor);
    test_child (reactor);
    test_stat (reactor);

    flux_reactor_destroy (reactor);

    lives_ok ({ reactor_destroy_early ();},
        "destroying reactor then watcher doesn't segfault");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

