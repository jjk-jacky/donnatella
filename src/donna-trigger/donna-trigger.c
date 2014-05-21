/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * donna-trigger.c
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

#include <glib-unix.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include "../socket.h"
#include "../macros.h"


#ifdef DONNA_DEBUG_ENABLED
#define DONNA_DEBUG(type, name, action) do { action } while (0)
#else
#define DONNA_DEBUG(type, name, action)
#endif /* DONNA_DEBUG_ENABLED */


#define DT_ERROR         g_quark_from_static_string ("DonnaTrigger-Error")

enum rc
{
    RC_OK = 0,
    RC_PARSE_CMDLINE_FAILED,
    RC_NO_SOCKET_PATH,
    RC_SOCKET_FAILED,
    RC_NO_FULL_LOCATION,
    RC_TRIGGER_ERROR,
    RC_TASK_FAILED,
    RC_TASK_CANCELLED
};


static GLogLevelFlags show_log = G_LOG_LEVEL_WARNING;
guint donna_debug_flags = 0;

struct priv
{
    DonnaSocket *socket;
    gchar *socket_path;
    GMainLoop *loop;
    gboolean no_wait;
    gboolean failed_on_err;
    guint nb_pending;
    guint task_id;
    enum rc rc;
};

static void
free_priv (gpointer data)
{
    struct priv *priv = data;

    if (priv->socket)
        donna_socket_close (priv->socket);
    g_free (priv->socket_path);
    if (priv->loop)
        g_main_loop_unref (priv->loop);
}

/* from util.c */
static void
donna_g_string_append_concat (GString            *str,
                              const gchar        *string,
                              ...)
{
    va_list va_args;

    if (G_UNLIKELY (!string))
        return;

    g_string_append (str, string);

    va_start (va_args, string);
    for (;;)
    {
        const gchar *s;

        s = va_arg (va_args, const gchar *);
        if (s)
            g_string_append (str, s);
        else
            break;
    }
    va_end (va_args);
}

static void
log_handler (const gchar    *domain,
             GLogLevelFlags  log_level,
             const gchar    *message)
{
    time_t now;
    struct tm *tm;
    gchar buf[12];
    gboolean colors;
    GString *str;

    if (log_level > show_log)
        return;

    colors = isatty (fileno (stdout));

    now = time (NULL);
    tm = localtime (&now);
    strftime (buf, 12, "[%H:%M:%S] ", tm);
    str = g_string_new (buf);

    if (log_level & G_LOG_LEVEL_ERROR)
        donna_g_string_append_concat (str,
                (colors) ? "\x1b[31m" : "** ",
                "ERROR: ",
                (colors) ? "\x1b[0m" : "",
                NULL);
    if (log_level & G_LOG_LEVEL_CRITICAL)
        donna_g_string_append_concat (str,
                (colors) ? "\x1b[1;31m" : "** ",
                "CRITICAL: ",
                (colors) ? "\x1b[0m" : "",
                NULL);
    if (log_level & G_LOG_LEVEL_WARNING)
        donna_g_string_append_concat (str,
                (colors) ? "\x1b[33m" : "",
                "WARNING: ",
                (colors) ? "\x1b[0m" : "",
                NULL);
    if (log_level & G_LOG_LEVEL_MESSAGE)
        g_string_append (str, "MESSAGE: ");
    if (log_level & G_LOG_LEVEL_INFO)
        g_string_append (str, "INFO: ");
    if (log_level & G_LOG_LEVEL_DEBUG)
        g_string_append (str, "DEBUG: ");
    /* custom/user log levels, for extra debug verbosity */
    if (log_level & DONNA_LOG_LEVEL_DEBUG2)
        g_string_append (str, "DEBUG: ");
    if (log_level & DONNA_LOG_LEVEL_DEBUG3)
        g_string_append (str, "DEBUG: ");
    if (log_level & DONNA_LOG_LEVEL_DEBUG4)
        g_string_append (str, "DEBUG: ");

    if (domain)
        donna_g_string_append_concat (str, "[", domain, "] ", NULL);

    g_string_append (str, message);
    puts (str->str);
    g_string_free (str, TRUE);

#ifdef DONNA_DEBUG_AUTOBREAK
    if (log_level & G_LOG_LEVEL_CRITICAL)
    {
        gboolean under_gdb = FALSE;
        FILE *f;
        gchar buffer[64];

        /* try to determine if we're running under GDB or not, and if so we
         * break. This is done by reading our /proc/PID/status and checking if
         * TracerPid if non-zero or not.
         * This doesn't guarantee GDB, and we don't check the name of that PID,
         * because this is a dev thing and good enough for me.
         * We also don't cache this info so we can attach/detach without
         * worries, and when attached it will break automagically.
         */

        snprintf (buffer, 64, "/proc/%d/status", getpid ());
        f = fopen (buffer, "r");
        if (f)
        {
            while ((fgets (buffer, 64, f)))
            {
                if (streqn ("TracerPid:\t", buffer, 11))
                {
                    under_gdb = buffer[11] != '0';
                    break;
                }
            }
            fclose (f);
        }

        if (under_gdb)
            G_BREAKPOINT ();
    }
#endif
}


struct cmdline_opt
{
    guint loglevel;
};

static gboolean
cmdline_cb (const gchar         *option,
            const gchar         *value,
            struct cmdline_opt  *data,
            GError             **error)
{
    if (streq (option, "-v") || streq (option, "--verbose"))
    {
        switch (data->loglevel)
        {
            case G_LOG_LEVEL_WARNING:
                data->loglevel = G_LOG_LEVEL_MESSAGE;
                break;
            case G_LOG_LEVEL_MESSAGE:
                data->loglevel = G_LOG_LEVEL_INFO;
                break;
            case G_LOG_LEVEL_INFO:
                data->loglevel = G_LOG_LEVEL_DEBUG;
                break;
            case G_LOG_LEVEL_DEBUG:
                data->loglevel = DONNA_LOG_LEVEL_DEBUG2;
                break;
            case DONNA_LOG_LEVEL_DEBUG2:
                data->loglevel = DONNA_LOG_LEVEL_DEBUG3;
                break;
            case DONNA_LOG_LEVEL_DEBUG3:
                data->loglevel = DONNA_LOG_LEVEL_DEBUG4;
                break;
        }
        return TRUE;
    }
    else if (streq (option, "-q") || streq (option, "--quiet"))
    {
        data->loglevel = G_LOG_LEVEL_ERROR;
        return TRUE;
    }
#ifdef DONNA_DEBUG_ENABLED
    else if (streq (option, "-d") || streq (option, "--debug"))
    {
        data->loglevel = G_LOG_LEVEL_DEBUG;
        return TRUE;
    }
#endif

    g_set_error (error, DT_ERROR, RC_PARSE_CMDLINE_FAILED,
            "Cannot parse unknown option '%s'", option);
    return FALSE;
}

static gboolean
parse_cmdline (struct priv  *priv,
               int          *argc,
               char        **argv[],
               GError      **error)
{
    GError *err = NULL;
    struct cmdline_opt data = { G_LOG_LEVEL_WARNING, };
    gchar *log_level = NULL;
    gboolean version = FALSE;
    GOptionContext *context;
    GOptionEntry entries[] =
    {
        { "log-level",  'L', 0, G_OPTION_ARG_STRING, &log_level,
            "Set LEVEL as the minimum log level to show", "LEVEL" },
        { "verbose",    'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cmdline_cb,
            "Increase verbosity of log; Repeat multiple times as needed.", NULL },
        { "quiet",      'q', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cmdline_cb,
            "Quiet mode (Same as --log-level=error)", NULL },
        { "socket",     's', 0, G_OPTION_ARG_STRING, &priv->socket_path,
            "Use SOCKET to communicate with donnatella", "SOCKET" },
        { "no-wait",    'n', 0, G_OPTION_ARG_NONE, &priv->no_wait,
            "Don't wait for trigger's error message/return value", NULL },
        { "failed-on-err", 'e', 0, G_OPTION_ARG_NONE, &priv->failed_on_err,
            "Show error messages of failed trigger on stderr", NULL },
#ifdef DONNA_DEBUG_ENABLED
        { "debug",      'd', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, cmdline_cb,
            "Debug mode (Same as --log-level=debug)", NULL },
#endif
        { "version",    'V', 0, G_OPTION_ARG_NONE, &version,
            "Show version and exit", NULL },
        { NULL }
    };
    GOptionGroup *group;

    context = g_option_context_new ("<FULL LOCATION>");
    group = g_option_group_new ("donna", "donna-trigger", "Main options",
            &data, NULL);
    g_option_group_add_entries (group, entries);
    //g_option_group_set_translation_domain (group, "domain");
    g_option_context_set_main_group (context, group);
    if (!g_option_context_parse (context, argc, argv, &err))
    {
        g_set_error (error, DT_ERROR, RC_PARSE_CMDLINE_FAILED,
                "%s", err->message);
        g_clear_error (&err);
        return FALSE;
    }

    if (version)
    {
        puts (  "donna-trigger v" PACKAGE_VERSION
                "\n"
                "Copyright (C) 2014 Olivier Brunel - http://jjacky.com/donnatella\n"
                "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
                "This is free software: you are free to change and redistribute it.\n"
                "There is NO WARRANTY, to the extent permitted by law."
                );
        exit (RC_OK);
    }

    /* log level (default/init to G_LOG_LEVEL_WARNING) */
    if (log_level)
    {
        if (streq (log_level, "debug4"))
            show_log = DONNA_LOG_LEVEL_DEBUG4;
        else if (streq (log_level, "debug3"))
            show_log = DONNA_LOG_LEVEL_DEBUG3;
        else if (streq (log_level, "debug2"))
            show_log = DONNA_LOG_LEVEL_DEBUG2;
        else if (streq (log_level, "debug"))
            show_log = G_LOG_LEVEL_DEBUG;
        else if (streq (log_level, "info"))
            show_log = G_LOG_LEVEL_INFO;
        else if (streq (log_level, "message"))
            show_log = G_LOG_LEVEL_MESSAGE;
        else if (streq (log_level, "warning"))
            show_log = G_LOG_LEVEL_WARNING;
        else if (streq (log_level, "critical"))
            show_log = G_LOG_LEVEL_CRITICAL;
        else if (streq (log_level, "error"))
            show_log = G_LOG_LEVEL_ERROR;
        else
        {
            g_set_error (error, DT_ERROR, RC_PARSE_CMDLINE_FAILED,
                    "Invalid minimum log level '%s': Must be one of "
                    "'debug4', 'debug3', 'debug2', 'debug', 'info', 'message' "
                    "'warning', 'critical' or 'error'",
                    log_level);
            g_free (log_level);
            return FALSE;
        }

        g_free (log_level);
    }
    else
        show_log = data.loglevel;

    return TRUE;
}

static void
socket_process (DonnaSocket *socket, gchar *message, struct priv *priv)
{
    if (!message)
    {
        g_debug ("socket closed");
        donna_socket_unref (socket);
        priv->socket = NULL;
        g_main_loop_quit (priv->loop);
        return;
    }

    g_debug2 ("received message:%s", message);

    if (streqn (message, "OK ", strlen ("OK ")))
    {
        --priv->nb_pending;
        message += strlen ("OK ");
        if (!priv->no_wait && streqn (message, "TRIGGER ", strlen ("TRIGGER ")))
        {
            ++priv->nb_pending;
            priv->task_id = (guint) g_ascii_strtoull (message + strlen ("TRIGGER "),
                    NULL, 10);
        }
    }
    else if (streqn (message, "ERR ", strlen ("ERR ")))
    {
        --priv->nb_pending;
        priv->rc = RC_TRIGGER_ERROR;

        message = strchr (message + strlen ("ERR "), ' ');
        if (message)
            fputs (message + 1, stderr);
        else
            fputs ("Failed without error message", stderr);
        fputc ('\n', stderr);
    }
    else if (streqn (message, "DONE ", strlen ("DONE ")))
    {
        --priv->nb_pending;
        message += strlen ("DONE ");
        message = strchr (message, ' ');
        if (message)
        {
            fputs (message + 1, stdout);
            fputc ('\n', stdout);
        }
    }
    else if (streqn (message, "FAILED ", strlen ("FAILED ")))
    {
        --priv->nb_pending;
        priv->rc = RC_TASK_FAILED;
        message += strlen ("FAILED ");
        message = strchr (message, ' ');
        if (message)
        {
            fputs (message + 1, (priv->failed_on_err) ? stderr : stdout);
            fputc ('\n', (priv->failed_on_err) ? stderr : stdout);
        }
    }
    else if (streqn (message, "CANCELLED ", strlen ("CANCELLED ")))
    {
        --priv->nb_pending;
        priv->rc = RC_TASK_CANCELLED;
    }

    if (priv->nb_pending == 0)
    {
        g_debug ("nothing left, closing socket");
        donna_socket_close (socket);
    }
    else
        g_debug2 ("still %d pending", priv->nb_pending);
}

static gboolean
init_socket (struct priv *priv, GError **error)
{
    struct sockaddr_un sock = { 0, };
    gint socket_fd;

    g_debug ("init socket");

    if (!priv->socket_path)
    {
        const gchar *s = g_getenv ("DONNATELLA_SOCKET");

        if (s)
            strcpy (sock.sun_path, s);
    }
    else
        strcpy (sock.sun_path, priv->socket_path);

    if (!*sock.sun_path)
    {
        g_set_error (error, DT_ERROR, RC_NO_SOCKET_PATH,
                "No socket path defined");
        return FALSE;
    }
    g_debug2 ("socket path=%s",sock.sun_path);

    socket_fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        gint _errno = errno;

        g_set_error (error, DT_ERROR, RC_SOCKET_FAILED,
                "Failed to create socket: %s",
                g_strerror (_errno));
        return FALSE;
    }

    if (fcntl (socket_fd, F_SETFL, O_NONBLOCK) == -1)
    {
        gint _errno = errno;

        close (socket_fd);
        g_set_error (error, DT_ERROR, RC_SOCKET_FAILED,
                "Failed to init socket: %s",
                g_strerror (_errno));
        return FALSE;
    }

    sock.sun_family = AF_UNIX;
    if (connect (socket_fd, &sock, sizeof (sock)) == -1)
    {
        gint _errno = errno;

        close (socket_fd);
        g_set_error (error, DT_ERROR, RC_SOCKET_FAILED,
                "Failed to connect socket: %s",
                g_strerror (_errno));
        return FALSE;
    }

    priv->socket = donna_socket_new (socket_fd,
            (socket_process_fn) socket_process, priv, NULL);
    return TRUE;
}

static gboolean
signal_handler (struct priv *priv)
{
    g_debug ("got a SIGINT");

    if (priv->no_wait || priv->task_id == 0)
    {
        /* no_wait: this should very rarely happen, since donna should always
         * reply right away, but in case let's close the socket.
         * task_id=0: means we asked to cancel the task, but it still isn't
         * POST_RUN and another SIGINT was received */
        g_debug ("closing socket");
        donna_socket_close (priv->socket);
    }
    else
    {
        gchar *s;

        /* we might have a task pending, so let's cancel it */

        g_debug ("cancelling pending task (%u)", priv->task_id);
        s = g_strdup_printf ("CANCEL %u", priv->task_id);
        ++priv->nb_pending;
        donna_socket_send (priv->socket, s, (gsize) -1);
        g_free (s);
        priv->task_id = 0;
    }

    return G_SOURCE_CONTINUE;
}

#define ensure( _code_) \
    if (!(_code_)) \
    {                                   \
        gint rc = err->code;            \
        fputs (err->message, stderr);   \
        fputc ('\n', stderr);           \
        g_clear_error (&err);           \
        free_priv (&priv);              \
        return rc;                      \
    }

int
main (int argc, char *argv[])
{
    GError *err = NULL;
    struct priv priv = { NULL, };
    gint i;

    g_log_set_default_handler ((GLogFunc) log_handler, NULL);
    g_unix_signal_add (SIGINT, (GSourceFunc) signal_handler, &priv);

    ensure (parse_cmdline (&priv, &argc, &argv, &err));

    if (argc <= 1)
    {
        fputs ("No full location to trigger specified", stderr);
        fputc ('\n', stderr);
        return RC_NO_FULL_LOCATION;
    }
    else if (argc > 2)
    {
        g_debug ("%d full locations, forcing option no-wait", argc - 1);
        priv.no_wait = TRUE;
    }
    else if (priv.no_wait)
        g_debug ("option no-wait enabled");

    ensure (init_socket (&priv, &err));

    for (i = 1; i < argc; ++i)
    {
        gchar *msg = g_strconcat ("TRIGGER ", argv[i], NULL);
        g_debug ("Send trigger:%s", argv[i]);
        donna_socket_send (priv.socket, msg, (gsize) -1);
        ++priv.nb_pending;
        g_free (msg);
    }

    priv.loop = g_main_loop_new (NULL, TRUE);
    g_main_loop_run (priv.loop);

    g_debug("ending");
    free_priv (&priv);
    return priv.rc;
}
