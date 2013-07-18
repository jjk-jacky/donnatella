
#include <glib-object.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "task-process.h"
#include "taskui-messages.h"
#include "common.h"
#include "treeview.h"
#include "closures.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_WORKDIR,
    PROP_CMDLINE,

    NB_PROPS
};

enum
{
    PIPE_DATA_RECEIVED,
    PIPE_NEW_LINE,
    NB_SIGNALS
};

struct _DonnaTaskProcessPrivate
{
    task_init_fn         init_fn;
    gpointer             init_data;
    GDestroyNotify       init_destroy;

    gchar               *workdir;
    gchar               *cmdline;
    gboolean             wait;
    DonnaTaskUiMessages *tuimsg;

    task_closer_fn       closer_fn;
    gpointer             closer_data;

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

    donna_task_process_signals[PIPE_DATA_RECEIVED] =
        g_signal_new ("pipe-data-received",
                DONNA_TYPE_TASK_PROCESS,
                G_SIGNAL_RUN_FIRST,
                0,
                NULL,
                NULL,
                g_cclosure_user_marshal_VOID__INT_LONG_STRING,
                G_TYPE_NONE,
                3,
                G_TYPE_INT,
                G_TYPE_LONG,
                G_TYPE_STRING);
    donna_task_process_signals[PIPE_NEW_LINE] =
        g_signal_new ("pipe-new-line",
                DONNA_TYPE_TASK_PROCESS,
                G_SIGNAL_RUN_FIRST,
                0,
                NULL,
                NULL,
                g_cclosure_user_marshal_VOID__INT_STRING,
                G_TYPE_NONE,
                2,
                G_TYPE_INT,
                G_TYPE_STRING);

    o_class = (GObjectClass *) klass;
    o_class->get_property = donna_task_process_get_property;
    o_class->set_property = donna_task_process_set_property;
    o_class->finalize     = donna_task_process_finalize;

    g_object_class_install_properties (o_class, NB_PROPS, donna_task_process_props);

    g_type_class_add_private (klass, sizeof (DonnaTaskProcessPrivate));
}

static void
donna_task_process_init (DonnaTaskProcess *taskp)
{
    DonnaTaskProcessPrivate *priv;

    priv = taskp->priv = G_TYPE_INSTANCE_GET_PRIVATE (taskp,
            DONNA_TYPE_TASK_PROCESS, DonnaTaskProcessPrivate);
}

static void
donna_task_process_finalize (GObject *object)
{
    DonnaTaskProcessPrivate *priv;

    priv = ((DonnaTaskProcess *) object)->priv;

    if (priv->init_destroy && priv->init_data)
        priv->init_destroy (priv->init_data);

    g_free (priv->cmdline);

    /* chain up */
    G_OBJECT_CLASS (donna_task_process_parent_class)->finalize (object);
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
                    donna_taskui_set_title ((DonnaTaskUi *) priv->tuimsg, s);
                g_free (s);
            }
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
    gint ret;

    if (*fd < 0)
        return;

again:
    ret = close (*fd);
    if (ret == -1 && errno == EINTR)
        goto again;
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

static gboolean
read_data (DonnaTask *task, DonnaPipe pipe, gint *fd)
{
    DonnaTaskProcessPrivate *priv = ((DonnaTaskProcess *) task)->priv;
    gssize len;
    gchar buf[4096];

again:
    len = read (*fd, buf, 4096);

    if (len == 0)
        /* EOF */
        close_fd (task, pipe, fd);
    else if (len > 0)
    {
        g_signal_emit (task, donna_task_process_signals[PIPE_DATA_RECEIVED], 0,
                pipe, len, buf);

        /* if there's no handler connected, no need to process this. This will
         * save us a little time/memory since we don't need to bother with the
         * GString and whatnot.
         * Obviously, this would be bad/bugged if someone decided to connect a
         * handler *during* the task's execution. That's true, but also quite
         * unlikely (since it wouldn't really make sense) so let's do it... */
        if (g_signal_has_handler_pending (task,
                    donna_task_process_signals[PIPE_NEW_LINE], 0, TRUE))
        {
            GString *str = (pipe == DONNA_PIPE_OUTPUT) ? priv->str_out : priv->str_err;
            gchar *s;

            if (G_LIKELY (str))
                g_string_append_len (str, buf, len);
            else
            {
                str = g_string_new_len (buf, len);
                if (pipe == DONNA_PIPE_OUTPUT)
                    priv->str_out = str;
                else
                    priv->str_err = str;
            }

            while ((s = strchr (str->str, '\n')))
            {
                *s = '\0';
                g_signal_emit (task, donna_task_process_signals[PIPE_NEW_LINE], 0,
                        pipe, str->str);
                g_string_erase (str, 0, s - str->str + 1);
            }
        }
    }
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
        return FALSE;
    }

    return TRUE;
}

static DonnaTaskState
default_closer (DonnaTask          *task,
                gpointer            data,
                gint                rc,
                DonnaTaskState      state)
{
    if (state != DONNA_TASK_DONE || rc == 0)
        return state;

    donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
            DONNA_TASK_PROCESS_ERROR_OTHER,
            "Process ended with return code %d",
            rc);
    return DONNA_TASK_FAILED;
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
    fd_set fds;
    gint fd_task;
    gint fd_out;
    gint fd_err;
    gint ret;
    gint failed = FAILED_NOT;

    if (priv->init_fn)
        priv->init_fn ((DonnaTaskProcess *) task, priv->init_data, &err);
    if (!priv->workdir || !priv->cmdline)
    {
        if (err)
            donna_task_take_error (task, err);
        else
            donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                    DONNA_TASK_PROCESS_ERROR_NO_CMDLINE,
                    "Failed getting working directory and command-line to execute");
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
    if (!g_spawn_async_with_pipes (priv->workdir, argv, NULL, flags, NULL, NULL,
                (priv->wait) ? &pid : NULL,
                NULL,
                (priv->wait) ? &fd_out : NULL,
                (priv->wait) ? &fd_err : NULL,
                &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!priv->wait)
        return DONNA_TASK_DONE;

    fd_task = donna_task_get_fd (task);
    while (failed == FAILED_NOT && (fd_out >= 0 || fd_err >= 0))
    {
        gint max;

        FD_ZERO (&fds);
        FD_SET (fd_task, &fds);
        if (fd_out >= 0)
            FD_SET (fd_out, &fds);
        if (fd_err >= 0)
            FD_SET (fd_err, &fds);

        max = MAX (fd_out, fd_err);
        ret = select (MAX (fd_task, max) + 1, &fds, NULL, NULL, NULL);

        if (ret < 0)
        {
            int _errno = errno;

            if (errno == EINTR)
                continue;

            failed = FAILED_ERROR;
            donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                    DONNA_TASK_PROCESS_ERROR_READ,
                    "Unexpected error in select() reading data from child process: %s",
                    g_strerror (_errno));
            break;
        }

        if (FD_ISSET (fd_task, &fds))
        {
            kill (pid, SIGSTOP);
            if (donna_task_is_cancelling (task))
            {
                kill (pid, SIGABRT);
                failed = FAILED_CANCELLED;
                break;
            }
            else
                kill (pid, SIGCONT);
        }

        if (fd_out >= 0 && FD_ISSET (fd_out, &fds))
            if (!read_data (task, DONNA_PIPE_OUTPUT, &fd_out))
            {
                failed = FAILED_ERROR;
                break;
            }

        if (fd_err >= 0 && FD_ISSET (fd_err, &fds))
            if (!read_data (task, DONNA_PIPE_ERROR, &fd_err))
            {
                failed = FAILED_ERROR;
                break;
            }
    }

    if (fd_out >= 0)
        close_fd (task, DONNA_PIPE_OUTPUT, &fd_out);
    if (fd_err >= 0)
        close_fd (task, DONNA_PIPE_ERROR, &fd_err);

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
        state = priv->closer_fn (task, priv->closer_data,
                WEXITSTATUS (status), state);

    if (priv->tuimsg)
    {
        gchar *s = g_strdup_printf ("%s: %s",
                (state == DONNA_TASK_DONE) ? "Success" : "Failed",
                priv->cmdline);
        donna_taskui_set_title ((DonnaTaskUi *) priv->tuimsg, s);
        g_free (s);
    }

    return state;
}

DonnaTask *
donna_task_process_new (const gchar        *workdir,
                        const gchar        *cmdline,
                        gboolean            wait,
                        task_closer_fn      closer,
                        gpointer            closer_data)
{
    DonnaTask *task;
    DonnaTaskProcessPrivate *priv;

    task = (DonnaTask *) g_object_new (DONNA_TYPE_TASK_PROCESS, NULL);
    priv = ((DonnaTaskProcess *) task)->priv;

    if (workdir)
        priv->workdir = g_strdup (workdir);
    if (cmdline)
        priv->cmdline = g_strdup (cmdline);
    priv->wait          = wait;
    priv->closer_fn     = closer;
    priv->closer_data   = closer_data;

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
donna_task_process_new_init (task_init_fn        init,
                             gpointer            data,
                             GDestroyNotify      destroy,
                             gboolean            wait,
                             task_closer_fn      closer,
                             gpointer            closer_data)
{
    DonnaTask *task;
    DonnaTaskProcessPrivate *priv;

    g_return_val_if_fail (init != NULL, NULL);

    task = (DonnaTask *) g_object_new (DONNA_TYPE_TASK_PROCESS, NULL);
    priv = ((DonnaTaskProcess *) task)->priv;

    priv->init_fn       = init;
    priv->init_data     = data;
    priv->init_destroy  = destroy;
    priv->wait          = wait;
    priv->closer_fn     = closer;
    priv->closer_data   = closer_data;

    donna_task_set_worker (task, task_worker, NULL, NULL);
    if (wait)
        donna_task_set_desc (task, "Execute process");
    donna_task_set_visibility (task, (wait)
            ? DONNA_TASK_VISIBILITY_PULIC : DONNA_TASK_VISIBILITY_INTERNAL);

    return task;
}

gboolean
donna_task_process_set_workdir_to_curdir (DonnaTaskProcess   *taskp,
                                          DonnaApp           *app,
                                          GError            **error)
{
    DonnaTreeView *tree;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_TASK_PROCESS (taskp), FALSE);

    g_object_get (app, "active-list", &tree, NULL);
    if (!tree)
    {
        g_set_error (error, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_OTHER,
                "Cannot set working directory: failed to get active-list");
        return FALSE;
    }

    g_object_get (tree, "location", &node, NULL);
    if (!node)
    {
        g_set_error (error, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_OTHER,
                "Cannot set working directory: failed to get current location from treeview '%s'",
                donna_tree_view_get_name (tree));
        g_object_unref (tree);
        return FALSE;
    }
    g_object_unref (tree);

    if (!streq ("fs", donna_node_get_domain (node)))
    {
        gchar *fl = donna_node_get_full_location (node);
        g_set_error (error, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_OTHER,
                "Cannot set working directory: current location (%s) of active-list (treeview '%s') must be in domain 'fs'",
                fl, donna_tree_view_get_name (tree));
        g_object_unref (tree);
        g_object_unref (node);
        g_free (fl);
        return FALSE;
    }

    taskp->priv->workdir = donna_node_get_location (node);
    g_object_unref (node);
    return TRUE;
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
        donna_taskui_set_title (tui, s);
        g_free (s);
    }
    else
        donna_taskui_set_title (tui, "Execute process");
    g_signal_connect (taskp, "pipe-new-line", (GCallback) pipe_new_line_cb, NULL);

    return TRUE;
}
