
#include <glib-object.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "app.h"
#include "provider.h"
#include "provider-fs.h"
#include "task-process.h"
#include "task-helpers.h"
#include "macros.h"
#include "debug.h"

#define LEN_PREFIX      4

struct data
{
    DonnaApp        *app;
    /* to create nodes for return value */
    DonnaProvider   *pfs;
    /* location of sources, to construct return value's nodes */
    GHashTable      *loc_sources;
    /* the nodes for return value, e.g. new (copied/moved) nodes */
    GPtrArray       *ret_nodes;

    /* openning quote around filename */
    gchar           *openq;
    /* closing quote around filename */
    gchar           *closeq;
    /* prefix to id confirmation, must start with it (e.g. "cp: ") */
    gchar            prefix[LEN_PREFIX + 1];
    /* buffer to write the answer to stdin */
    gchar            wbuf[8];
    /* len of data to write (from wbuf) */
    gint             wlen;
    /* buffer for data received from stderr */
    GString         *str;
    /* whether it's a DELETE operation or not (parsing err isn't the same) */
    guint            is_rm          : 1;
    /* set to 1 if the answer to write was too long */
    guint            has_error      : 1;
    /* internal flags while parsing stderr */
    guint            in_line        : 1; /* inside a line, w/ at least LEN_PREFIX bytes */
    guint            in_msg         : 1; /* inside a confirmation line */
    guint            in_filename    : 1; /* inside a (quoted) filename */
    guint            has_question   : 1; /* do we have a question to ask */
};

static void
free_data (struct data *data)
{
    donna_g_object_unref (data->app);
    donna_g_object_unref (data->pfs);
    if (data->loc_sources)
        g_hash_table_unref (data->loc_sources);
    if (data->ret_nodes)
        g_ptr_array_unref (data->ret_nodes);
    if (data->str)
        g_string_free (data->str, TRUE);
    g_slice_free (struct data, data);
}

static gboolean
get_filename (gchar  *str,
              gchar  *openq,
              gchar  *closeq,
              gchar **filename,
              gchar **end)
{
    gchar *s;

    s = strstr (str, openq);
    if (!s)
        return FALSE;
    s += strlen (openq);

    if (filename)
        *filename = str + (s - str);

again:
    s = strstr (s, closeq);
    if (!s)
        return FALSE;
    if (s[-1] == '\\')
    {
        s += strlen (closeq);
        goto again;
    }

    if (end)
        *end = str + (s - str);
    return TRUE;
}

static gchar *
unesc_fn (gchar *filename, gchar *end, gchar *buf, gint bufsize)
{
    if (!strchr (filename, '\\'))
        return NULL;
    else
    {
        gchar *b;
        gint l;

        if (end - filename < bufsize)
            b = buf;
        else
            b = g_new (gchar, end - filename + 1);

        for (l = 0; *filename != '\0'; ++filename)
        {
            if (*filename == '\\')
            {
                if (*++filename == '\0')
                    break;
                /* in case UTF8 characters were escaped (as octal values) */
                if (*filename >= '0' && *filename <= '7'
                        && filename[1] >= '0' && filename[1] <= '7'
                        && filename[2] >= '0' && filename[2] <= '7')
                {
                    b[l++] = g_ascii_strtoull (filename, NULL, 8);
                    filename += 2;
                    continue;
                }
            }
            b[l++] = *filename;
        }
        b[l] = '\0';
        return b;
    }
}

static void
pipe_new_line (DonnaTask    *task,
               DonnaPipe    *pipe,
               gchar        *str,
               struct data  *data)
{
    gchar buf[255], *b;
    gchar *filename;
    gchar *s;
    gchar *e;
    gchar c;

    if (pipe != DONNA_PIPE_OUTPUT || !data->loc_sources)
        return;

    if (!get_filename (str, data->openq, data->closeq, &s, &e))
        return;

    c = *e;
    *e = '\0';
    b = unesc_fn (s, e, buf, 255);

    filename = g_hash_table_lookup (data->loc_sources, (b) ? b : s);
    if (filename)
    {
        GError *err = NULL;
        DonnaTask *task;

        *e = c;
        if (b && b != buf)
            g_free (b);

        if (!get_filename (e + strlen (data->closeq), data->openq, data->closeq, &s, &e))
        {
            g_warning ("FS Engine 'basic': Failed to get new filename for '%s'; "
                    "Will be skipped in returned nodes", filename);
            g_hash_table_remove (data->loc_sources, filename);
            if (g_hash_table_size (data->loc_sources) == 0)
            {
                g_hash_table_unref (data->loc_sources);
                data->loc_sources = NULL;
            }
            return;
        }

        c = *e;
        *e = '\0';
        b = unesc_fn (s, e, buf, 255);

        task = donna_provider_get_node_task (data->pfs, (b) ? b : s, &err);
        if (G_UNLIKELY (!task))
        {
            g_warning ("FS Engine 'basic': Failed to get task for '%s': %s",
                    (b) ? b : s,
                    (err) ? err->message : "(no error message)");
            g_clear_error (&err);
            goto remove;
        }

        donna_task_set_can_block (g_object_ref_sink (task));
        donna_app_run_task (data->app, task);
        donna_task_wait_for_it (task);
        if (donna_task_get_state (task) != DONNA_TASK_DONE)
        {
            g_warning ("FS Engine 'basic': Failed to get node for '%s'",
                    (b) ? b : s);
            g_object_unref (task);
            goto remove;
        }

        g_ptr_array_add (data->ret_nodes, g_value_dup_object (
                    donna_task_get_return_value (task)));
        g_object_unref (task);

remove:
        g_hash_table_remove (data->loc_sources, filename);
        if (g_hash_table_size (data->loc_sources) == 0)
        {
            g_hash_table_unref (data->loc_sources);
            data->loc_sources = NULL;
        }
    }

done:
    *e = c;
    if (b && b != buf)
        g_free (b);
}

static void
pipe_data_received (DonnaTask     *task,
                    DonnaPipe      pipe,
                    gsize          len,
                    gchar         *str,
                    struct data   *data)
{
    gchar *s;

    if (pipe != DONNA_PIPE_ERROR)
        return;

    if (!data->str)
        data->str = g_string_new (NULL);

    g_string_append_len (data->str, str, len);

    if (data->in_line)
    {
        if (!data->in_msg)
        {
            s = strchr (data->str->str, '\n');
            if (s)
            {
                g_string_erase (data->str, 0, s - data->str->str);
                data->in_line = FALSE;
            }
        }
    }

    if (!data->in_line && data->str->len >= LEN_PREFIX)
    {
        data->in_line = TRUE;
        data->in_msg = streqn (data->str->str, data->prefix, LEN_PREFIX);
    }

    if (data->in_msg)
    {
        if (data->is_rm)
            s = data->str->str;
        else if (!get_filename (data->str->str, data->openq, data->closeq, NULL, &s))
            return;

        s = strchr (s, '?');
        if (s)
            data->has_question = TRUE;
    }
}

static DonnaTaskProcessStdin
handle_stdin (DonnaTask          *task,
              GPid                pid,
              gint                fd,
              struct data        *data)
{
    gint r;
    gchar *s, c;
    gchar *desc;
    gchar *details;

    if (!data->has_question)
        return DONNA_TASK_PROCESS_STDIN_DONE;

    if (data->wlen > 0)
        goto write;

    if (data->is_rm)
        s = data->str->str;
    else if (G_UNLIKELY (!get_filename (data->str->str, data->openq, data->closeq,
                    NULL, &s)))
        return DONNA_TASK_PROCESS_STDIN_DONE;

    s = strchr (s, '?');
    if (G_LIKELY (s) && s[1] != '\0')
    {
        c = s[1];
        s[1] = '\0';
    }
    else
        c = '\0';

    desc = donna_task_get_desc (task);
    details = g_strdup_printf ("%s\n\n%s", desc,
            (data->str) ? (gchar *) data->str->str : "(no data)");
    g_free (desc);

    r = donna_task_helper_ask (task, "Confirmation required", details, FALSE, 0, NULL);
    g_free (details);
    if (s)
        s[1] = c;

    /* no answer given (e.g. just closed the window) -> cancel */
    if (r == DONNA_TASK_HELPER_ASK_RC_NO_ANSWER)
        r = 1;

    if (r > 0)
    {
        if (r == 1)
            donna_task_cancel (task);
        else
        {
            const gchar *answer;
            gssize written;

            if (r == 2)
                answer = "n";
            else /* if (r == 3) */
                answer = "y";

            data->wlen = snprintf (data->wbuf, 8, "%s\n", answer);
            if (G_UNLIKELY (data->wlen >= 8))
            {
                kill (pid, SIGTERM);
                data->has_error = 1;
                return DONNA_TASK_PROCESS_STDIN_DONE;
            }

write:
            written = write (fd, data->wbuf, data->wlen);
            if (written < 0)
            {
                if (errno == EAGAIN)
                    return DONNA_TASK_PROCESS_STDIN_WAIT_NONBLOCKING;
                else
                {
                    donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                            DONNA_TASK_PROCESS_ERROR_OTHER,
                            "Failed to write answer to child process' stdin");
                    return DONNA_TASK_PROCESS_STDIN_FAILED;
                }
            }

            /* so what we've written gets logged by the taskui */
            g_signal_emit_by_name (task, "pipe-data-received", DONNA_PIPE_ERROR,
                    written, data->wbuf);

            if (written < data->wlen)
            {
                data->wlen -= written;
                memmove (data->wbuf, data->wbuf + written, data->wlen);
                return DONNA_TASK_PROCESS_STDIN_WAIT_NONBLOCKING;
            }

            data->in_line = data->in_msg = data->has_question = FALSE;
            data->wlen = 0;
        }

        g_string_set_size (data->str, 0);
    }

    return DONNA_TASK_PROCESS_STDIN_DONE;
}

static DonnaTaskState
closer (DonnaTask       *task,
        gint             rc,
        DonnaTaskState   state,
        struct data     *data)
{
    if (state != DONNA_TASK_DONE)
        return state;
    else if (data->has_error)
    {
        donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_OTHER,
                "Answer to confirmation too long");
        return DONNA_TASK_FAILED;
    }
    else if (rc != 0)
    {
        donna_task_set_error (task, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_OTHER,
                "Process ended with return code %d",
                rc);
        return DONNA_TASK_FAILED;
    }
    else
    {
        GValue *value;

        value = donna_task_grab_return_value (task);
        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_take_boxed (value, data->ret_nodes);
        donna_task_release_return_value (task);

        data->ret_nodes = NULL;
        return DONNA_TASK_DONE;
    }
}

static void
set_cmdline (DonnaTaskProcess   *taskp,
             gpointer            data,
             GError            **error)
{
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_STRING);
    g_value_take_string (&v, data);
    g_object_set_property ((GObject *) taskp, "cmdline", &v);
    g_value_unset (&v);

    donna_task_process_setenv (taskp, "LANG", "C", TRUE);
}

DonnaTask *
donna_fs_engine_basic_io_task (DonnaApp           *app,
                               DonnaIoType         type,
                               GPtrArray          *sources,
                               DonnaNode          *dest,
                               fs_parse_cmdline    parser,
                               GError            **error)
{
    DonnaTaskProcess *taskp;
    struct data *data;
    gchar *cmdline;
    guint i;

    data = g_slice_new0 (struct data);
    data->app = g_object_ref (app);
    if (type == DONNA_IO_COPY || type == DONNA_IO_MOVE)
    {
        data->pfs = donna_app_get_provider (app, "fs");
        if (!data->pfs)
        {
            free_data (data);
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "FS Engine 'basic': Failed to get provider 'fs'");
            return NULL;
        }
    }
    data->openq = data->closeq = "'";
    switch (type)
    {
        case DONNA_IO_COPY:
            cmdline = (gchar *) "cp -irvat %d %s";
            snprintf (data->prefix, LEN_PREFIX + 1, "cp: ");
            break;

        case DONNA_IO_MOVE:
            cmdline = (gchar *) "mv -irvat %d %s";
            snprintf (data->prefix, LEN_PREFIX + 1, "mv: ");
            break;

        case DONNA_IO_DELETE:
            cmdline = (gchar *) "rm -Ir %s";
            snprintf (data->prefix, LEN_PREFIX + 1, "rm: ");
            data->is_rm = TRUE;
            break;

        default:
            free_data (data);
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                    "FS Engine 'basic': Operation not supported (%d)", type);
            return NULL;
    }

    cmdline = parser (cmdline, sources, dest, error);
    if (G_UNLIKELY (!cmdline))
    {
        free_data (data);
        g_prefix_error (error, "FS Engine 'basic': Failed to parse command line: ");
        return NULL;
    }


    taskp = (DonnaTaskProcess *) donna_task_process_new_full (
            (task_init_fn) set_cmdline, cmdline, g_free,
            TRUE /* wait */,
            NULL, NULL, NULL, /* default pauser */
            (task_stdin_fn) handle_stdin, data, NULL,
            (task_closer_fn) closer, data, NULL);
    if (G_UNLIKELY (!taskp))
    {
        free_data (data);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "FS Engine 'basic': Failed to create new task-process");
        g_free (cmdline);
        return NULL;
    }

    donna_task_process_set_ui_msg (taskp);

    if (G_UNLIKELY (!donna_task_process_set_workdir_to_curdir (taskp, app)))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "FS Engine 'basic': Failed to set workdir for task-process");
        g_object_unref (taskp);
        return NULL;
    }

    if (data->pfs)
    {
        /* construct the list of location to watch for return nodes */
        data->loc_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        for (i = 0; i < sources->len; ++i)
            g_hash_table_add (data->loc_sources,
                    donna_node_get_location (sources->pdata[i]));
        /* return value */
        data->ret_nodes = g_ptr_array_new_full (sources->len, g_object_unref);
    }

    g_signal_connect (taskp, "pipe-data-received", (GCallback) pipe_data_received, data);
    g_signal_connect (taskp, "pipe-new-line", (GCallback) pipe_new_line, data);
    g_signal_connect_swapped (taskp, "process-ended", (GCallback) free_data, data);

    return (DonnaTask *) taskp;
}
