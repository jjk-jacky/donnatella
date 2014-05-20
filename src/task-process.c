/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * task-process.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <glib-object.h>
#include <stdio.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "task-process.h"
#include "taskui-messages.h"
#include "common.h"
#include "treeview.h"
#include "closures.h"
#include "macros.h"
#include "debug.h"

enum
{
    PROP_0,

    PROP_WORKDIR,
    PROP_CMDLINE,
    PROP_ENVIRON,
    PROP_AUTOPULSE,

    NB_PROPS
};

enum
{
    PIPE_DATA_RECEIVED,
    PIPE_NEW_LINE,
    PROCESS_STARTED,
    PROCESS_ENDED,
    NB_SIGNALS
};

struct _DonnaTaskProcessPrivate
{
    gboolean             autopulse;

    task_init_fn         init_fn;
    gpointer             init_data;
    GDestroyNotify       init_destroy;

    gchar               *workdir;
    gchar               *cmdline;
    gchar              **envp;
    gboolean             wait;
    DonnaTaskUiMessages *tuimsg;

    task_pauser_fn       pauser_fn;
    gpointer             pauser_data;
    GDestroyNotify       pauser_destroy;

    task_stdin_fn        stdin_fn;
    gpointer             stdin_data;
    GDestroyNotify       stdin_destroy;

    task_closer_fn       closer_fn;
    gpointer             closer_data;
    GDestroyNotify       closer_destroy;

    GString             *str_out;
    GString             *str_err;
};

static GParamSpec * donna_task_process_props[NB_PROPS] = { NULL };
static guint donna_task_process_signals[NB_SIGNALS] = { 0 };

static void     donna_task_process_get_property         (GObject            *object,
                                                         guint               prop_id,
                                                         GValue             *value,
                                                         GParamSpec         *pspec);
static void     donna_task_process_set_property         (GObject            *object,
                                                         guint               prop_id,
                                                         const GValue       *value,
                                                         GParamSpec         *pspec);
static void     donna_task_process_finalize             (GObject            *object);

static void     donna_task_process_pipe_data_received   (DonnaTaskProcess   *taskp,
                                                         DonnaPipe           pipe,
                                                         gsize               len,
                                                         const gchar        *str);

G_DEFINE_TYPE (DonnaTaskProcess, donna_task_process, DONNA_TYPE_TASK)

static void
donna_task_process_class_init (DonnaTaskProcessClass *klass)
{
    GObjectClass *o_class;

    donna_task_process_props[PROP_WORKDIR] =
        g_param_spec_string ("workdir", "workdir",
                "Working directory for the executed process",
                NULL, /* default */
                G_PARAM_READWRITE);
    donna_task_process_props[PROP_CMDLINE] =
        g_param_spec_string ("cmdline", "cmdline",
                "Command-line to execute",
                NULL, /* default */
                G_PARAM_READWRITE);
    donna_task_process_props[PROP_ENVIRON] =
        g_param_spec_boxed ("environ", "environ",
                "Environnement for the executed process",
                G_TYPE_STRV,
                G_PARAM_READWRITE);
    donna_task_process_props[PROP_AUTOPULSE] =
        g_param_spec_boolean ("autopulse", "autopulse",
                "Whether to automatically pulse during process execution",
                TRUE, /* default */
                G_PARAM_READWRITE);

    donna_task_process_signals[PIPE_DATA_RECEIVED] =
        g_signal_new ("pipe-data-received",
                DONNA_TYPE_TASK_PROCESS,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTaskProcessClass, pipe_data_received),
                NULL,
                NULL,
                /* POINTER and not STRING because it might contain NUL-byte, and
                 * STRING assumes a NUL-terminated string (and therefore will be
                 * truncated at the first NUL) */
                g_cclosure_user_marshal_VOID__INT_LONG_POINTER,
                G_TYPE_NONE,
                3,
                G_TYPE_INT,
                G_TYPE_ULONG,
                G_TYPE_POINTER);
    donna_task_process_signals[PIPE_NEW_LINE] =
        g_signal_new ("pipe-new-line",
                DONNA_TYPE_TASK_PROCESS,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTaskProcessClass, pipe_new_line),
                NULL,
                NULL,
                g_cclosure_user_marshal_VOID__INT_STRING,
                G_TYPE_NONE,
                2,
                G_TYPE_INT,
                G_TYPE_STRING);
    donna_task_process_signals[PROCESS_STARTED] =
        g_signal_new ("process-started",
                DONNA_TYPE_TASK_PROCESS,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTaskProcessClass, process_started),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__VOID,
                G_TYPE_NONE,
                0);
    donna_task_process_signals[PROCESS_ENDED] =
        g_signal_new ("process-ended",
                DONNA_TYPE_TASK_PROCESS,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTaskProcessClass, process_ended),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__VOID,
                G_TYPE_NONE,
                0);

    o_class = (GObjectClass *) klass;
    o_class->get_property = donna_task_process_get_property;
    o_class->set_property = donna_task_process_set_property;
    o_class->finalize     = donna_task_process_finalize;

    klass->pipe_data_received = donna_task_process_pipe_data_received;

    g_object_class_install_properties (o_class, NB_PROPS, donna_task_process_props);

    g_type_class_add_private (klass, sizeof (DonnaTaskProcessPrivate));
}

static void
donna_task_process_init (DonnaTaskProcess *taskp)
{
    DonnaTaskProcessPrivate *priv;

    priv = taskp->priv = G_TYPE_INSTANCE_GET_PRIVATE (taskp,
            DONNA_TYPE_TASK_PROCESS, DonnaTaskProcessPrivate);
    priv->autopulse = TRUE;
}

static void
donna_task_process_finalize (GObject *object)
{
    DonnaTaskProcessPrivate *priv;

    priv = ((DonnaTaskProcess *) object)->priv;

    if (priv->closer_destroy && priv->closer_data)
        priv->closer_destroy (priv->closer_data);

    if (priv->pauser_destroy && priv->pauser_data)
        priv->pauser_destroy (priv->pauser_data);

    if (priv->init_destroy && priv->init_data)
        priv->init_destroy (priv->init_data);

    g_free (priv->workdir);
    g_free (priv->cmdline);
    g_strfreev (priv->envp);

    /* chain up */
    G_OBJECT_CLASS (donna_task_process_parent_class)->finalize (object);
}

static void
set_taskui_title (DonnaTaskUi *taskui, gchar *title, gboolean need_dup)
{
    if (need_dup)
        donna_taskui_set_title (taskui, title);
    else
        donna_taskui_take_title (taskui, title);
}

static void
donna_task_process_get_property (GObject            *object,
                                 guint               prop_id,
                                 GValue             *value,
                                 GParamSpec         *pspec)
{
    DonnaTaskProcessPrivate *priv = ((DonnaTaskProcess *) object)->priv;

    switch (prop_id)
    {
        case PROP_WORKDIR:
            g_value_set_string (value, priv->workdir);
            break;

        case PROP_CMDLINE:
            g_value_set_string (value, priv->cmdline);
            break;

        case PROP_ENVIRON:
            g_value_set_boxed (value, priv->envp);
            break;

        case PROP_AUTOPULSE:
            g_value_set_boolean (value, priv->autopulse);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
donna_task_process_set_property (GObject            *object,
                                 guint               prop_id,
                                 const GValue       *value,
                                 GParamSpec         *pspec)
{
    DonnaTaskProcessPrivate *priv = ((DonnaTaskProcess *) object)->priv;

    switch (prop_id)
    {
        case PROP_WORKDIR:
            g_free (priv->workdir);
            priv->workdir = g_value_dup_string (value);
            break;

        case PROP_CMDLINE:
            g_free (priv->cmdline);
            priv->cmdline = g_value_dup_string (value);
            if (priv->wait)
            {
                gchar *s = g_strdup_printf ("Execute: %s", priv->cmdline);
                donna_task_take_desc ((DonnaTask *) object, s);
                if (priv->tuimsg)
                    set_taskui_title ((DonnaTaskUi *) priv->tuimsg, s, TRUE);
            }
            break;

        case PROP_ENVIRON:
            g_strfreev (priv->envp);
            priv->envp = g_value_dup_boxed (value);
            break;

        case PROP_AUTOPULSE:
            priv->autopulse = g_value_get_boolean (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

enum
{
    FAILED_NOT = 0,
    FAILED_ERROR,
    FAILED_CANCELLED
};

static void
close_fd (DonnaTask *task, DonnaPipe pipe, gint *fd)
{
    DonnaTaskProcessPrivate *priv = ((DonnaTaskProcess *) task)->priv;
    GString *str = (pipe == DONNA_PIPE_OUTPUT) ? priv->str_out : priv->str_err;

    if (*fd < 0)
        return;

    /* we should NOT check for EINTR and try again. POSIX says in that case the
     * state in undefined, but on Linux it WILL be closed (and maybe already
     * reused in threaded env) so we shouldn't try to close it again.
     *
     * See http://lkml.indiana.edu/hypermail/linux/kernel/0509.1/0877.html */
    close (*fd);
    *fd = -1;

    g_signal_emit (task, donna_task_process_signals[PIPE_DATA_RECEIVED], 0,
            pipe, 0, NULL);

    if (str)
    {
        if (str->len > 0)
        {
            g_string_append_c (str, '\0');
            g_signal_emit (task, donna_task_process_signals[PIPE_NEW_LINE], 0,
                    pipe, str->str);
        }
        g_string_free (str, TRUE);
        if (pipe == DONNA_PIPE_OUTPUT)
            priv->str_out = NULL;
        else
            priv->str_err = NULL;
    }
}

static void
donna_task_process_pipe_data_received (DonnaTaskProcess   *taskp,
                                       DonnaPipe           pipe,
                                       gsize               len,
                                       const gchar        *data)
{
    DonnaTaskProcessPrivate *priv = taskp->priv;

    /* if there's no handler connected, no need to process this. This will
     * save us a little time/memory since we don't need to bother with the
     * GString and whatnot.
     * Obviously, this would be bad/bugged if someone decided to connect a
     * handler *during* the task's execution. That's true, but also quite
     * unlikely (since it wouldn't really make sense) so let's do it... */
    if (g_signal_has_handler_pending (taskp,
                donna_task_process_signals[PIPE_NEW_LINE], 0, TRUE))
    {
        GString *str = (pipe == DONNA_PIPE_OUTPUT) ? priv->str_out : priv->str_err;
        gchar *s;

        if (G_LIKELY (str))
            g_string_append_len (str, data, (gssize) len);
        else
        {
            str = g_string_new_len (data, (gssize) len);
            if (pipe == DONNA_PIPE_OUTPUT)
                priv->str_out = str;
            else
                priv->str_err = str;
        }

        while ((s = strchr (str->str, '\n')))
        {
            *s = '\0';
            g_signal_emit (taskp, donna_task_process_signals[PIPE_NEW_LINE], 0,
                    pipe, str->str);
            g_string_erase (str, 0, s - str->str + 1);
        }
    }
}

enum rd
{
    RD_NONE,
    RD_READ,
    RD_FAIL
};

static guint
read_data (DonnaTask *task, DonnaPipe pipe, gint *fd)
{
    gssize len;
    gchar buf[4096];

again:
    len = read (*fd, buf, 4096);

    if (len == 0)
        /* EOF */
        close_fd (task, pipe, fd);
    else if (len > 0)
        g_signal_emit (task, donna_task_process_signals[PIPE_DATA_RECEIVED], 0,
                pipe, len, buf);
    else if (errno == EINTR)
        goto again;
    else
    {
        gint _errno = errno;

        g_signal_emit (task, donna_task_process_signals[PIPE_DATA_RECEIVED], 0,
                pipe, 0, NULL);
        donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_READ,
                "Failed to read data from %s of child process: %s",
                (pipe == DONNA_PIPE_OUTPUT) ? "stdout" : "stderr",
                g_strerror (_errno));
        return RD_FAIL;
    }

    return (len > 0) ? RD_READ : RD_NONE;
}

static DonnaTaskState
default_closer (DonnaTask          *task,
                gint                rc,
                DonnaTaskState      state,
                gpointer            data)
{
    if (state != DONNA_TASK_DONE || rc == 0)
        return state;

    donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
            DONNA_TASK_PROCESS_ERROR_OTHER,
            "Process ended with return code %d",
            rc);
    return DONNA_TASK_FAILED;
}

static gboolean
default_pauser (DonnaTask *task, GPid pid)
{
    gboolean ret;

    kill (pid, SIGSTOP);
    ret = donna_task_is_cancelling (task);

    if (ret)
        kill (pid, SIGTERM);
    kill (pid, SIGCONT);

    return ret;
}

static gboolean
pulse_cb (DonnaTask *task)
{
    donna_task_update (task, DONNA_TASK_UPDATE_PROGRESS_PULSE, 0, NULL);
    return TRUE;
}

static void
close_fd_in (gint *fd)
{
    /* see close_fd() for lack of loop in case of EINTR */
    close (*fd);
    *fd = -1;
}

static DonnaTaskState
task_worker (DonnaTask *task, gpointer data)
{
    GError *err = NULL;
    DonnaTaskProcessPrivate *priv = ((DonnaTaskProcess *) task)->priv;
    DonnaTaskState state;
    gint argc;
    gchar **argv;
    GSpawnFlags flags;
    GPid pid;
    gint status;
    struct pollfd pfd[4];
    gint fd_task;
    gint fd_in = -1;
    gint fd_out;
    gint fd_err;
    gint ret;
    nfds_t n_in = (nfds_t) -1;
    gint closed = 0;
    gint failed = FAILED_NOT;
    guint sid;

    if (priv->init_fn)
        priv->init_fn ((DonnaTaskProcess *) task, priv->init_data, &err);
    if (!priv->workdir || !priv->cmdline)
    {
        if (err)
            donna_task_take_error (task, err);
        else
            donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                    DONNA_TASK_PROCESS_ERROR_NO_CMDLINE,
                    "Failed getting working directory and/or command-line to execute");
        return DONNA_TASK_FAILED;
    }
    /* we don't want to free/destroy data in finalize anymore */
    priv->init_destroy = NULL;

    if (!g_shell_parse_argv (priv->cmdline, &argc, &argv, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    flags = G_SPAWN_SEARCH_PATH;
    if (priv->wait)
        flags |= G_SPAWN_DO_NOT_REAP_CHILD;
    if (!g_spawn_async_with_pipes (priv->workdir, argv, priv->envp, flags, NULL, NULL,
                (priv->wait) ? &pid : NULL,
                (priv->wait && priv->stdin_fn) ? &fd_in : NULL,
                (priv->wait) ? &fd_out : NULL,
                (priv->wait) ? &fd_err : NULL,
                &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    g_strfreev (argv);

    if (!priv->wait)
        return DONNA_TASK_DONE;

    g_signal_emit (task, donna_task_process_signals[PROCESS_STARTED], 0);

    /* install a timeout to pulsate our progress */
    if (priv->autopulse)
        sid = g_timeout_add (100, (GSourceFunc) pulse_cb, task);
    else
        sid = 0;

    if (!priv->stdin_fn)
        ++closed;

    fd_task = donna_task_get_fd (task);
    while (fd_out >= 0 || fd_err >= 0)
    {
        nfds_t n = 0;

        if (failed == FAILED_NOT)
        {
            pfd[n].fd = fd_task;
            pfd[n].events = POLLIN;
            ++n;
        }

        if (fd_in >= 0)
        {
            DonnaTaskProcessStdin r;

            /* we always at least want to get POLLHUP | POLLERR */
            pfd[n].fd = fd_in;
            pfd[n].events = 0;

            /* we want to know if we can non-blockingly write if this is the
             * first time (check before calling stdin_fn), or stdin_fn asks for
             * it (got an EAGAIN error from write()) */
            if (n_in == (nfds_t) -1)
                r = DONNA_TASK_PROCESS_STDIN_WAIT_NONBLOCKING;
            else
                r = priv->stdin_fn (task, pid, fd_in, priv->stdin_data);

            if (r == DONNA_TASK_PROCESS_STDIN_WAIT_NONBLOCKING)
                pfd[n].events = POLLOUT;
            else if (r == DONNA_TASK_PROCESS_STDIN_FAILED)
            {
                failed = FAILED_ERROR;
                break;
            }

            n_in = n;
            ++n;
        }

        if (fd_out >= 0)
        {
            pfd[n].fd = fd_out;
            pfd[n].events = POLLIN;
            ++n;
        }

        if (fd_err >= 0)
        {
            pfd[n].fd = fd_err;
            pfd[n].events = POLLIN;
            ++n;
        }

        if (n == 0)
            break;

        ret = poll (pfd, n, -1);
        if (ret < 0)
        {
            int _errno = errno;

            if (errno == EINTR || errno == EAGAIN)
                continue;

            failed = FAILED_ERROR;
            donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                    DONNA_TASK_PROCESS_ERROR_READ,
                    "Unexpected error in poll() reading data from child process: %s",
                    g_strerror (_errno));
            break;
        }

        n = 0;

        if (failed == FAILED_NOT)
        {
            if (pfd[n].revents & POLLIN)
            {
                gboolean is_cancelling;

                if (sid)
                    g_source_remove (sid);

                if (priv->pauser_fn)
                    is_cancelling = priv->pauser_fn (task, pid, priv->pauser_data);
                else
                    is_cancelling = default_pauser (task, pid);

                if (is_cancelling)
                {
                    sid = 0;
                    failed = FAILED_CANCELLED;
                }
                else if (sid)
                    sid = g_timeout_add (100, (GSourceFunc) pulse_cb, task);
            }
            ++n;
        }

        /* we just close the fd in case of POLLERR|POLLHUP because this might
         * just be the process ending its execution normally. If there was an
         * error, it shall come from the return code (or whatever else the close
         * uses, e.g. parsing stderr) */

        if (fd_in >= 0)
        {
            if (pfd[n_in].revents & (POLLERR | POLLHUP))
            {
                close_fd_in (&fd_in);
                ++closed;
            }
            ++n;
        }

        if (fd_out >= 0)
        {
            guint rd = RD_NONE;

            if (pfd[n].revents & POLLIN)
            {
                rd = read_data (task, DONNA_PIPE_OUTPUT, &fd_out);
                if (rd == RD_FAIL)
                {
                    failed = FAILED_ERROR;
                    break;
                }
            }
            if (rd == RD_NONE && pfd[n].revents & (POLLERR | POLLHUP))
            {
                close_fd (task, DONNA_PIPE_OUTPUT, &fd_out);
                ++closed;
            }
            ++n;
        }

        if (fd_err >= 0)
        {
            guint rd = RD_NONE;

            if (pfd[n].revents & POLLIN)
            {
                rd = read_data (task, DONNA_PIPE_ERROR, &fd_err);
                if (rd == RD_FAIL)
                {
                    failed = FAILED_ERROR;
                    break;
                }
            }
            if (rd == RD_NONE && pfd[n].revents & (POLLERR | POLLHUP))
            {
                close_fd (task, DONNA_PIPE_ERROR, &fd_err);
                ++closed;
            }
            ++n;
        }

        if (closed == 3)
            break;
    }

    if (fd_out >= 0)
        close_fd (task, DONNA_PIPE_OUTPUT, &fd_out);
    if (fd_err >= 0)
        close_fd (task, DONNA_PIPE_ERROR, &fd_err);
    if (fd_in >= 0)
        close_fd_in (&fd_in);

again:
    ret = waitpid (pid, &status, 0);

    if (ret < 0)
    {
        if (errno == EINTR)
            goto again;
        /* ignore is we already have an error */
        else if (!failed)
        {
            gint _errno = errno;

            failed = FAILED_ERROR;
            donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                    DONNA_TASK_PROCESS_ERROR_READ,
                    "Unexpected error (%d) in waitpid(): %s",
                    _errno, g_strerror (_errno));
        }
    }

    if (failed == FAILED_NOT)
        state = DONNA_TASK_DONE;
    else if (failed == FAILED_CANCELLED)
        state = DONNA_TASK_CANCELLED;
    else /* if (failed == FAILED_ERROR) */
        state = DONNA_TASK_FAILED;

    if (priv->closer_fn)
        state = priv->closer_fn (task, WEXITSTATUS (status), state,
                priv->closer_data);
    /* we don't want to free/destroy data in finalize anymore */
    priv->closer_destroy = NULL;

    if (priv->tuimsg)
    {
        gchar *s = g_strdup_printf ("%s: %s",
                (state == DONNA_TASK_DONE) ? "Success" : "Failed",
                priv->cmdline);
        set_taskui_title ((DonnaTaskUi *) priv->tuimsg, s, FALSE);
    }

    g_signal_emit (task, donna_task_process_signals[PROCESS_ENDED], 0);

    if (sid)
        g_source_remove (sid);

    if (state == DONNA_TASK_DONE)
        donna_task_update (task, DONNA_TASK_UPDATE_PROGRESS, 100, NULL);
    else
        donna_task_update (task, DONNA_TASK_UPDATE_PROGRESS_PULSE, -1, NULL);

    return state;
}

DonnaTask *
donna_task_process_new (const gchar        *workdir,
                        const gchar        *cmdline,
                        gboolean            wait,
                        task_closer_fn      closer,
                        gpointer            closer_data,
                        GDestroyNotify      closer_destroy)
{
    DonnaTask *task;
    DonnaTaskProcessPrivate *priv;

    task = (DonnaTask *) g_object_new (DONNA_TYPE_TASK_PROCESS, NULL);
    priv = ((DonnaTaskProcess *) task)->priv;

    if (workdir)
        priv->workdir = g_strdup (workdir);
    if (cmdline)
        priv->cmdline = g_strdup (cmdline);
    priv->wait           = wait;
    priv->closer_fn      = closer;
    priv->closer_data    = closer_data;
    priv->closer_destroy = closer_destroy;

    donna_task_set_worker (task, task_worker, NULL, NULL);
    if (wait)
    {
        donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_PULIC);
        if (cmdline)
            donna_task_take_desc (task, g_strdup_printf ("Execute: %s", cmdline));
        else
            donna_task_set_desc (task, "Execute process");
    }
    else
        donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL);

    return task;
}

DonnaTask *
donna_task_process_new_full (task_init_fn        init,
                             gpointer            data,
                             GDestroyNotify      destroy,
                             gboolean            wait,
                             task_pauser_fn      pauser,
                             gpointer            pauser_data,
                             GDestroyNotify      pauser_destroy,
                             task_stdin_fn       stdin_fn,
                             gpointer            stdin_data,
                             GDestroyNotify      stdin_destroy,
                             task_closer_fn      closer,
                             gpointer            closer_data,
                             GDestroyNotify      closer_destroy)
{
    DonnaTask *task;
    DonnaTaskProcessPrivate *priv;

    g_return_val_if_fail (init != NULL, NULL);

    task = (DonnaTask *) g_object_new (DONNA_TYPE_TASK_PROCESS, NULL);
    priv = ((DonnaTaskProcess *) task)->priv;

    priv->init_fn        = init;
    priv->init_data      = data;
    priv->init_destroy   = destroy;
    priv->wait           = wait;
    priv->pauser_fn      = pauser;
    priv->pauser_data    = pauser_data;
    priv->pauser_destroy = pauser_destroy;
    priv->stdin_fn       = stdin_fn;
    priv->stdin_data     = stdin_data;
    priv->stdin_destroy  = stdin_destroy;
    priv->closer_fn      = closer;
    priv->closer_data    = closer_data;
    priv->closer_destroy = closer_destroy;

    donna_task_set_worker (task, task_worker, NULL, NULL);
    if (wait)
        donna_task_set_desc (task, "Execute process");
    donna_task_set_visibility (task, (wait)
            ? DONNA_TASK_VISIBILITY_PULIC : DONNA_TASK_VISIBILITY_INTERNAL);

    return task;
}

gboolean
donna_task_process_set_workdir_to_curdir (DonnaTaskProcess   *taskp,
                                          DonnaApp           *app)
{
    gchar *workdir;

    workdir = donna_app_get_current_dirname (app);
    if (!workdir)
        return FALSE;

    taskp->priv->workdir = workdir;
    return TRUE;
}

gboolean
donna_task_process_set_pauser (DonnaTaskProcess   *taskp,
                               task_pauser_fn      pauser,
                               gpointer            data,
                               GDestroyNotify      destroy)
{
    DonnaTaskProcessPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK_PROCESS (taskp), FALSE);
    priv = taskp->priv;

    if (priv->pauser_fn)
        return FALSE;

    priv->pauser_fn      = pauser;
    priv->pauser_data    = data;
    priv->pauser_destroy = destroy;
    return TRUE;
}

gboolean
donna_task_process_set_stdin (DonnaTaskProcess   *taskp,
                              task_stdin_fn       fn,
                              gpointer            data,
                              GDestroyNotify      destroy)
{
    DonnaTaskProcessPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK_PROCESS (taskp), FALSE);
    priv = taskp->priv;

    if (priv->stdin_fn)
        return FALSE;

    priv->stdin_fn      = fn;
    priv->stdin_data    = data;
    priv->stdin_destroy = destroy;
    return TRUE;
}

void
donna_task_process_import_environ (DonnaTaskProcess *taskp,
                                   DonnaApp         *app)
{
    DonnaTaskProcessPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK_PROCESS (taskp));
    g_return_if_fail (DONNA_IS_APP (app));
    priv = taskp->priv;

    if (priv->envp)
        g_strfreev (priv->envp);
    priv->envp = g_strdupv (donna_app_get_environ (app));
}

void
donna_task_process_setenv (DonnaTaskProcess   *taskp,
                           const gchar        *variable,
                           const gchar        *value,
                           gboolean            overwrite)
{
    DonnaTaskProcessPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK_PROCESS (taskp));
    priv = taskp->priv;

    priv->envp = g_environ_setenv (priv->envp, variable, value, overwrite);
}

void
donna_task_process_unsetenv (DonnaTaskProcess   *taskp,
                             const gchar        *variable)
{
    DonnaTaskProcessPrivate *priv;

    g_return_if_fail (DONNA_IS_TASK_PROCESS (taskp));
    priv = taskp->priv;

    priv->envp = g_environ_unsetenv (priv->envp, variable);
}

gboolean
donna_task_process_set_default_closer (DonnaTaskProcess   *taskp)
{
    DonnaTaskProcessPrivate *priv;

    g_return_val_if_fail (DONNA_IS_TASK_PROCESS (taskp), FALSE);
    priv = taskp->priv;

    if (priv->closer_fn)
        return FALSE;

    priv->closer_fn = default_closer;
    return TRUE;
}

static void
pipe_new_line_cb (DonnaTaskProcess   *taskp,
                  DonnaPipe           pipe,
                  gchar              *line)
{
    donna_task_ui_messages_add (taskp->priv->tuimsg,
            (pipe == DONNA_PIPE_OUTPUT) ? G_LOG_LEVEL_INFO : G_LOG_LEVEL_ERROR,
            line);
}

gboolean
donna_task_process_set_ui_msg (DonnaTaskProcess   *taskp)
{
    DonnaTaskProcessPrivate *priv;
    DonnaTaskUi *tui;

    g_return_val_if_fail (DONNA_IS_TASK_PROCESS (taskp), FALSE);

    priv = taskp->priv;

    tui = g_object_new (DONNA_TYPE_TASKUI_MESSAGES, NULL);
    if (!donna_task_set_taskui ((DonnaTask *) taskp, tui))
    {
        g_object_ref_sink (tui);
        g_object_unref (tui);
        return FALSE;
    }
    priv->tuimsg = (DonnaTaskUiMessages *) tui;
    if (priv->cmdline)
    {
        gchar *s = g_strdup_printf ("Execute: %s", priv->cmdline);
        set_taskui_title (tui, s, FALSE);
    }
    else
        set_taskui_title (tui, (gchar *) "Execute process", TRUE);
    g_signal_connect (taskp, "pipe-new-line", (GCallback) pipe_new_line_cb, NULL);

    return TRUE;
}
