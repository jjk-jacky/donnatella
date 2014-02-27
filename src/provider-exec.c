/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-exec.c
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

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include "provider-exec.h"
#include "provider.h"
#include "task-process.h"
#include "treeview.h"
#include "node.h"
#include "util.h"
#include "macros.h"


enum mode
{
    MODE_EXEC = 1,
    MODE_EXEC_AND_WAIT,
    MODE_TERMINAL,
    MODE_EMBEDDED_TERMINAL,
    MODE_PARSE_OUTPUT,
    MODE_DESKTOP_FILE
};

struct exec
{
    enum mode mode;
    guint extra;
    gchar *terminal;
    gchar *terminal_cmdline; /* in MODE_EMBEDDED_TERMINAL */
};


/* internal, used by app.c */
gboolean
_donna_provider_exec_register_extras (DonnaConfig *config, GError **error);

/* DonnaProvider */
static const gchar *    provider_exec_get_domain    (DonnaProvider      *provider);
static DonnaProviderFlags provider_exec_get_flags   (DonnaProvider      *provider);
static DonnaTask *      provider_exec_get_node_children_task (
                                                     DonnaProvider      *provider,
                                                     DonnaNode          *node,
                                                     DonnaNodeType       node_types,
                                                     GError            **error);
static DonnaTask *      provider_exec_trigger_node_task (
                                                     DonnaProvider      *provider,
                                                     DonnaNode          *node,
                                                     GError            **error);
/* DonnaProviderBase */
static void             provider_exec_unref_node    (DonnaProviderBase  *provider,
                                                     DonnaNode          *node);
static DonnaTaskState   provider_exec_new_node      (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     const gchar        *location);
static DonnaTaskState   provider_exec_has_children  (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node,
                                                     DonnaNodeType       node_types);

static void
provider_exec_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain               = provider_exec_get_domain;
    interface->get_flags                = provider_exec_get_flags;
    interface->get_node_children_task   = provider_exec_get_node_children_task;
    interface->trigger_node_task        = provider_exec_trigger_node_task;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderExec, donna_provider_exec,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_exec_provider_init)
        )

static void
donna_provider_exec_class_init (DonnaProviderExecClass *klass)
{
    DonnaProviderBaseClass *pb_class;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->unref_node    = provider_exec_unref_node;
    pb_class->new_node      = provider_exec_new_node;
    pb_class->has_children  = provider_exec_has_children;
}

static void
donna_provider_exec_init (DonnaProviderExec *provider)
{
}

static DonnaProviderFlags
provider_exec_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_EXEC (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_exec_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_EXEC (provider), NULL);
    return "exec";
}

gboolean
_donna_provider_exec_register_extras (DonnaConfig *config, GError **error)
{
    DonnaConfigItemExtraListInt it[6];
    gint i;

    i = 0;
    it[i].value     = MODE_EXEC;
    it[i].in_file   = "exec";
    it[i].label     = "Execute";
    ++i;
    it[i].value     = MODE_EXEC_AND_WAIT;
    it[i].in_file   = "exec_and_wait";
    it[i].label     = "Execute and Wait (Capture output)";
    ++i;
    it[i].value     = MODE_TERMINAL;
    it[i].in_file   = "terminal";
    it[i].label     = "Run in Terminal";
    ++i;
    it[i].value     = MODE_EMBEDDED_TERMINAL;
    it[i].in_file   = "embedded_terminal";
    it[i].label     = "Run in Embedded Terminal";
    ++i;
    it[i].value     = MODE_PARSE_OUTPUT;
    it[i].in_file   = "parse_output";
    it[i].label     = "Execute & Parse Output (e.g. search reults)";
    ++i;
    it[i].value     = MODE_DESKTOP_FILE;
    it[i].in_file   = "desktop_file";
    it[i].label     = "Execute .desktop file";
    ++i;
    if (G_UNLIKELY (!donna_config_add_extra (config,
                    DONNA_CONFIG_EXTRA_TYPE_LIST_INT, "exec-mode",
                    "Mode of Execution",
                    i, it, error)))
        return FALSE;

    return TRUE;
}

struct children
{
    /* because it's used as both data of the worker & the duplicator */
    gint ref_count;
    /* to run task get_node when parsing "search results" */
    DonnaApp *app;
    /* node's provider (*not* ref-d), convenience to emit new-child signal */
    DonnaProvider *provider;
    /* node we're getting children of */
    DonnaNode *node;
    /* type of children we want */
    DonnaNodeType node_types;
    /* workdir, so we can duplicate the task */
    gchar *workdir;
    /* provider-fs, to get_node for children */
    DonnaProvider *pfs;
    /* actual children found, to set a return value of get_children */
    GPtrArray *children;
    /* the task will be failed if TRUE, i.e. there was something on stderr */
    gboolean has_error;
};

static void
free_children (struct children *data)
{
    if (!data)
        return;
    /* the first free can only come from worker being done, which means we want
     * to keep everything we need to duplicate, but the children we don't need
     * */
    if (data->children)
        g_ptr_array_unref (data->children);
    data->children = NULL;
    if (--data->ref_count == 0)
    {
        g_object_unref (data->app);
        g_object_unref (data->node);
        g_object_unref (data->pfs);
        g_free (data->workdir);
        g_free (data);
    }
}

static DonnaTaskState
children_closer (DonnaTask          *task,
                 gint                rc,
                 DonnaTaskState      state,
                 struct children    *data)
{
    GValue *value;

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_set_boxed (value, data->children);
    donna_task_release_return_value (task);

    /* emit node-children */
    donna_provider_node_children (data->provider,
            data->node,
            data->node_types,
            data->children);

    state = (state == DONNA_TASK_CANCELLED) ? DONNA_TASK_CANCELLED
        : ((data->has_error) ? DONNA_TASK_FAILED : DONNA_TASK_DONE);
    free_children (data);
    return state;
}

static gchar *
resolve_path (const gchar *curdir, const gchar *path)
{
    GString *str;
    const gchar *s;

    str = g_string_new (NULL);

    if (*path == '/')
    {
        g_string_append_c (str, '/');
        ++path;
    }
    else
    {
        g_string_append (str, curdir);
        if (str->str[str->len - 1] != '/')
            g_string_append_c (str, '/');
    }

    for (;;)
    {
        if (path[0] == '.')
        {
            if (path[1] == '/')
                /* stay where we are, so just move in path */
                path += 2;
            else if (path[1] == '\0')
                break;
            else if (path[1] == '.' && (path[2] == '/' || path[2] == '\0'))
            {
                for (s = str->str + str->len - 2; s >= str->str && *s != '/'; --s)
                    ;
                if (s < str->str)
                    /* trying to go up the root ("/") just stays there */
                    s = str->str;
                /* go past the '/' */
                ++s;
                g_string_truncate (str, (gsize) (s - str->str));
                if (path[2] == '/')
                    path += 3;
                else
                    break;
            }
            else
                goto component;
        }
        else if (path[0] == '/')
            /* "//" -> "/" */
            ++path;
        else
        {
component:
            s = strchr (path, '/');
            if (s)
            {
                g_string_append_len (str, path, s - path + 1);
                path += s - path + 1;
            }
            else
            {
                g_string_append (str, path);
                break;
            }
        }
    }

    return g_string_free (str, FALSE);
}

static gboolean
refresh_path (DonnaTask *task, DonnaNode *node, const gchar *name)
{
    GValue value = G_VALUE_INIT;
    gchar *location;
    gchar *s;

    /* only called on nodes in "fs" for property "path" */

    location = donna_node_get_location (node);
    s = strrchr (location, '/');
    if (G_LIKELY (s != location))
        *s = '\0';
    else
        *++s = '\0';
    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, location);
    donna_node_set_property_value (node, "path", &value);
    g_value_unset (&value);
    return TRUE;
}

static void
pipe_new_line_cb (DonnaTaskProcess  *taskp,
                  DonnaPipe          pipe,
                  gchar             *line,
                  struct children   *data)
{
    DonnaNode *n;
    GValue value = G_VALUE_INIT;
    gchar *location;
    gchar *path;
    gchar *s;

    if (!line)
        /* EOF */
        return;

    if (pipe == DONNA_PIPE_ERROR)
    {
        data->has_error = TRUE;
        return;
    }

    if (*line == '/')
        path = line;
    else
    {
        path = resolve_path (data->workdir, line);
        if (!path)
            /* XXX: should we set has_error (& and something to the taskui?) */
            return;
    }

    n = donna_provider_get_node (data->pfs, path, NULL);
    if (!n)
    {
        if (path != line)
            g_free (path);
        return;
    }
    if (path != line)
        g_free (path);

    if (!(donna_node_get_node_type (n) & data->node_types))
    {
        g_object_unref (n);
        return;
    }

    /* add a property "path" to the node, for the "Path" column. Getting the
     * location from the node helps with trailing slashes on folders
     * (auto-removed). Also makes things easier to deal with (since we'd have
     * not to free path when we do, etc) */
    location = donna_node_get_location (n);
    s = strrchr (location, '/');
    if (G_LIKELY (s != location))
        *s = '\0';
    else
        *++s = '\0';
    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, location);
    donna_node_add_property (n, "path", G_TYPE_STRING, &value,
            (refresher_fn) refresh_path, NULL, NULL);
    g_value_unset (&value);

    /* give our ref on n to the array */
    g_ptr_array_add (data->children, n);

    /* emit new-child */
    donna_provider_node_new_child (data->provider, data->node, n);
}

static DonnaTask *
duplicate_get_children_task (struct children *dup_data, GError **error)
{
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;
    struct exec *ex;
    struct children *data;
    gchar *location;

    donna_node_get (dup_data->node, FALSE, "_exec", &has, &v, NULL);
    if (G_UNLIKELY (has != DONNA_NODE_VALUE_SET))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'exec': Invalid node (%d), missing internal exec property",
                has);
        return NULL;
    }
    ex = g_value_get_pointer (&v);
    g_value_unset (&v);

    location = donna_node_get_location (dup_data->node);

    data = g_new0 (struct children, 1);
    data->ref_count = 2; /* one for task, one for duplicator */
    data->app = g_object_ref (dup_data->app);
    data->provider = dup_data->provider;
    data->node = g_object_ref (dup_data->node);
    data->node_types = dup_data->node_types;
    data->workdir = g_strdup (dup_data->workdir);
    data->pfs = g_object_ref (dup_data->pfs);
    data->children = g_ptr_array_new_full (0, g_object_unref);

    task = donna_task_process_new (data->workdir, location + ex->extra, TRUE,
            (task_closer_fn) children_closer, data, (GDestroyNotify) free_children);
    g_free (location);

    donna_task_process_set_ui_msg ((DonnaTaskProcess *) task);

    g_signal_connect (task, "pipe-new-line", (GCallback) pipe_new_line_cb, data);

    donna_task_set_duplicator (task,
            (task_duplicate_fn) duplicate_get_children_task,
            data, (GDestroyNotify) free_children);

    return task;
}

static DonnaTaskState
appinfo_launch (DonnaTask *task, GAppInfo *appinfo)
{
    GError *err = NULL;

    if (!g_app_info_launch (appinfo, NULL, NULL, &err))
    {
        g_object_unref (appinfo);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    g_object_unref (appinfo);

    return DONNA_TASK_DONE;
}

static DonnaTask *
get_node_children_task (DonnaProvider      *provider,
                        DonnaNode          *node,
                        DonnaNodeType       node_types,
                        GError            **error)
{
    DonnaApp *app;
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;
    struct exec *ex;
    gchar *location;
    gchar *cmdline;
    gboolean wait = FALSE;
    task_closer_fn closer = NULL;
    struct children *data = NULL;

    donna_node_get (node, FALSE, "_exec", &has, &v, NULL);
    if (G_UNLIKELY (has != DONNA_NODE_VALUE_SET))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'exec': Invalid node (%d), missing internal exec property",
                has);
        return NULL;
    }
    ex = g_value_get_pointer (&v);
    g_value_unset (&v);

    location = donna_node_get_location (node);
    cmdline = location + ex->extra;

    if (ex->mode == MODE_EXEC || ex->mode == MODE_EXEC_AND_WAIT)
        wait = ex->mode == MODE_EXEC_AND_WAIT;
    else if (ex->mode == MODE_TERMINAL)
    {
        cmdline = g_strconcat (ex->terminal, " ", location + ex->extra, NULL);
        g_free (location);
        location = cmdline;
    }
    else if (ex->mode == MODE_EMBEDDED_TERMINAL)
    {
        DonnaNode *n;
        GString *str;

        str = g_string_new ("command:terminal_add_tab(");
        donna_g_string_append_quoted (str, ex->terminal, FALSE);
        g_string_append_c (str, ',');
        donna_g_string_append_quoted (str, cmdline, FALSE);
        if (ex->terminal_cmdline)
        {
            g_string_append_c (str, ',');
            donna_g_string_append_quoted (str, ex->terminal_cmdline, FALSE);
        }
        g_string_append_c (str, ')');

        g_object_get (provider, "app", &app, NULL);
        n = donna_app_get_node (app, str->str, FALSE, error);
        g_string_free (str, TRUE);
        if (G_UNLIKELY (!n))
        {
            g_prefix_error (error, "Provider 'exec': Failed to get node for command "
                    " to use embedded terminal: ");
            g_object_unref (app);
            g_free (location);
            return NULL;
        }

        task = donna_node_trigger_task (n, error);
        g_object_unref (n);
        return task;
    }
    else if (ex->mode == MODE_PARSE_OUTPUT)
    {
        wait = TRUE;

        data = g_new0 (struct children, 1);
        data->ref_count = 2; /* one for task, one for duplicator */
        data->provider = provider;
        data->node = g_object_ref (node);
        data->node_types = node_types;
        data->children = g_ptr_array_new_full (0, g_object_unref);
        closer = (task_closer_fn) children_closer;
    }
    else if (ex->mode == MODE_DESKTOP_FILE)
    {
        GAppInfo *appinfo;

        if (location[ex->extra] == '/')
            appinfo = (GAppInfo *) g_desktop_app_info_new_from_filename (location);
        else
        {
            gsize len;

            len = strlen (location + ex->extra);
            /* 9 = 8 + 1; 8 = strlen (".desktop") */
            if (len < 9 || !streq (location + ex->extra + len - 8, ".desktop"))
                cmdline = g_strconcat (location + ex->extra, ".desktop", NULL);
            else
                cmdline = location + ex->extra;
            appinfo = (GAppInfo *) g_desktop_app_info_new (cmdline);
            if (cmdline != location)
                g_free (cmdline);
        }

        if (!appinfo)
        {
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Provider 'exec': Unable to load .desktop file for '%s'",
                    location);
            g_free (location);
            return NULL;
        }
        g_free (location);

        task = donna_task_new ((task_fn) appinfo_launch, appinfo, g_object_unref);
        return task;
    }

    task = donna_task_process_new (NULL, cmdline, wait,
            closer, data, (GDestroyNotify) free_children);
    g_free (location);

    g_object_get (provider, "app", &app, NULL);
    if (!donna_task_process_set_workdir_to_curdir ((DonnaTaskProcess *) task, app))
    {
        g_object_unref (app);
        g_object_ref_sink (task);
        g_object_unref (task);
        return NULL;
    }

    if (wait)
    {
        donna_task_process_set_ui_msg ((DonnaTaskProcess *) task);
        if (closer)
        {
            data->app = app;
            data->pfs = donna_app_get_provider (app, "fs");
            g_object_get (task, "workdir", &data->workdir, NULL);
            g_signal_connect (task, "pipe-new-line",
                    (GCallback) pipe_new_line_cb, data);

            donna_task_set_duplicator (task,
                    (task_duplicate_fn) duplicate_get_children_task,
                    data, (GDestroyNotify) free_children);
        }
        else
            donna_task_process_set_default_closer ((DonnaTaskProcess *) task);
    }

    if (!data)
        g_object_unref (app);
    return task;
}

static DonnaTask *
provider_exec_get_node_children_task (DonnaProvider      *provider,
                                      DonnaNode          *node,
                                      DonnaNodeType       node_types,
                                      GError            **error)
{
    return get_node_children_task (provider, node, node_types, error);
}

static DonnaTask *
provider_exec_trigger_node_task (DonnaProvider      *provider,
                                 DonnaNode          *node,
                                 GError            **error)
{
    return get_node_children_task (provider, node, 0, error);
}

static void
provider_exec_unref_node (DonnaProviderBase  *provider,
                          DonnaNode          *node)
{
    GValue v = G_VALUE_INIT;
    DonnaNodeHasValue has;

    donna_node_get (node, FALSE, "_exec", &has, &v, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        struct exec *ex = g_value_get_pointer (&v);
        g_value_unset (&v);
        g_free (ex->terminal);
        g_free (ex->terminal_cmdline);
        g_slice_free (struct exec, ex);
        /* after that, the node is either finalized (as it should) or marked
         * invalid, so there's no risk of "_exec" being used again */
    }
}

static DonnaTaskState
provider_exec_new_node (DonnaProviderBase  *_provider,
                        DonnaTask          *task,
                        const gchar        *location)
{
    GError *err = NULL;
    DonnaProviderBaseClass *klass;
    DonnaApp *app;
    DonnaConfig *config;
    DonnaNode *node;
    DonnaNode *n;
    struct exec exec = { 0, };
    struct exec *ex;
    struct prefix
    {
        const gchar *name;
        enum mode mode;
    } prefixes[] = {
        { "exec",               MODE_EXEC },
        { "exec_and_wait",      MODE_EXEC_AND_WAIT },
        { "terminal",           MODE_TERMINAL },
        { "embedded_terminal",  MODE_EMBEDDED_TERMINAL },
        { "parse_output",       MODE_PARSE_OUTPUT },
        { "desktop_file",       MODE_DESKTOP_FILE },
    };
    guint nb_prefixes = G_N_ELEMENTS (prefixes);
    guint i;
    GValue *value;
    GValue v = G_VALUE_INIT;
    gchar *s;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);

    g_object_get (_provider, "app", &app, NULL);
    config = donna_app_peek_config (app);
    g_object_unref (app);

    for (i = 0; i < nb_prefixes; ++i)
    {
        if (donna_config_get_string (config, NULL, &s, "providers/exec/prefix_%s",
                    prefixes[i].name))
        {
            if (*s == '\0' || s[1] != '\0')
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'exec': Cannot create new node: "
                        "Invalid value (%s) for option 'prefix_%s'; "
                        "Must be a single character",
                        s, prefixes[i].name);
                g_free (s);
                return DONNA_TASK_FAILED;
            }
            if (*location == *s)
            {
                exec.extra = 1;
                exec.mode = prefixes[i].mode;
            }
            g_free (s);
        }
        if (exec.mode > 0)
            break;
    }
    if (exec.mode == 0)
    {
        if (!donna_config_get_int (config, NULL, (gint *) &exec.mode,
                    "providers/exec/default_mode"))
        {
            g_warning ("Provider 'exec': No default mode set, using EXEC");
            exec.mode = MODE_EXEC;
        }
        else
        {
            for (i = 0; i < nb_prefixes; ++i)
                if (exec.mode == prefixes[i].mode)
                    break;
            if (i >= nb_prefixes)
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'exec': Cannot create new node, "
                        "invalid default mode (%d)",
                        exec.mode);
                return DONNA_TASK_FAILED;
            }
        }
    }

    if (exec.mode == MODE_TERMINAL)
    {
        GPtrArray *arr = NULL;

        if (donna_config_list_options (config, &arr, DONNA_CONFIG_OPTION_TYPE_NUMBERED,
                    "providers/exec/terminal"))
        {
            for (i = 0; i < arr->len; ++i)
            {
                if (donna_config_get_string (config, NULL, &s,
                            "providers/exec/terminal/%s/prefix", arr->pdata[i]))
                {
                    if (streqn (location + exec.extra, s, strlen (s)))
                    {
                        if (!donna_config_get_string (config, &err, &exec.terminal,
                                    "providers/exec/terminal/%s/cmdline",
                                    arr->pdata[i]))
                        {
                            g_prefix_error (&err,
                                    "Provider 'exec': Cannot create new node: "
                                    "Failed to get option 'cmdline': ");
                            donna_task_take_error (task, err);
                            g_free (s);
                            g_ptr_array_unref (arr);
                            return DONNA_TASK_FAILED;
                        }
                        exec.extra += (guint) strlen (s);
                        g_free (s);
                        break;
                    }
                    g_free (s);
                }
            }
            g_ptr_array_unref (arr);
        }

        if (!exec.terminal)
        {
            donna_config_get_string (config, NULL, &exec.terminal,
                    "providers/exec/terminal/cmdline");
            if (!exec.terminal)
            {
                gchar *terminal;

                /* try to default to common terminal emulator */
                terminal = g_find_program_in_path ("urxvt");
                if (!terminal)
                    terminal = g_find_program_in_path ("rxvt");
                if (!terminal)
                    terminal = g_find_program_in_path ("xterm");
                if (!terminal)
                    terminal = g_find_program_in_path ("konsole");

                if (terminal)
                    exec.terminal = g_strconcat (terminal, " -e", NULL);
                else
                {
                    /* those should be using -x instead of -e */
                    terminal = g_find_program_in_path ("xfce4-terminal");
                    if (!terminal)
                        terminal = g_find_program_in_path ("gnome-terminal");
                    if (terminal)
                        exec.terminal = g_strconcat (terminal, " -x", NULL);
                    else
                    {
                        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                                DONNA_PROVIDER_ERROR_OTHER,
                                "Provider 'exec': Unable to find a terminal emulator, "
                                "you can define the command line in option "
                                "'providers/exec/terminal/cmdline'");
                        return DONNA_TASK_FAILED;
                    }
                }

                g_free (terminal);
            }
        }
    }
    else if (exec.mode == MODE_EMBEDDED_TERMINAL)
    {
        GPtrArray *arr = NULL;

        if (donna_config_list_options (config, &arr, DONNA_CONFIG_OPTION_TYPE_NUMBERED,
                    "providers/exec/embedded_terminal"))
        {
            for (i = 0; i < arr->len; ++i)
            {
                if (donna_config_get_string (config, NULL, &s,
                            "providers/exec/embedded_terminal/%s/prefix", arr->pdata[i]))
                {
                    if (streqn (location + exec.extra, s, strlen (s)))
                    {
                        if (!donna_config_get_string (config, &err, &exec.terminal,
                                    "providers/exec/embedded_terminal/%s/terminal",
                                    arr->pdata[i]))
                        {
                            g_prefix_error (&err,
                                    "Provider 'exec': Cannot create new node: "
                                    "Failed to get option 'terminal': ");
                            donna_task_take_error (task, err);
                            g_free (s);
                            g_ptr_array_unref (arr);
                            return DONNA_TASK_FAILED;
                        }
                        exec.extra += (guint) strlen (s);
                        g_free (s);

                        donna_config_get_string (config, NULL, &exec.terminal_cmdline,
                                "providers/exec/embedded_terminal/%s/terminal_cmdline",
                                arr->pdata[i]);
                        break;
                    }
                    g_free (s);
                }
            }
            g_ptr_array_unref (arr);
        }

        if (!exec.terminal)
        {
            donna_config_get_string (config, NULL, &exec.terminal,
                    "providers/exec/embedded_terminal/terminal");
            if (!exec.terminal)
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'exec': Unable to find an embedded terminal, "
                        "you can define the terminal to use in option "
                        "'providers/exec/embedded_terminal/terminal'");
                return DONNA_TASK_FAILED;
            }
            donna_config_get_string (config, NULL, &exec.terminal_cmdline,
                    "providers/exec/embedded_terminal/terminal_cmdline");
        }
    }

    node = donna_node_new ((DonnaProvider *) _provider, location,
            exec.mode == MODE_PARSE_OUTPUT ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
            NULL, (refresher_fn) gtk_true, NULL, location,
            DONNA_NODE_ICON_EXISTS);
    if (!node)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'exec': Failed to create a new node");
        g_free (exec.terminal);
        return DONNA_TASK_FAILED;
    }

    ex = g_slice_new (struct exec);
    memcpy (ex, &exec, sizeof (struct exec));
    g_value_init (&v, G_TYPE_POINTER);
    g_value_set_pointer (&v, ex);
    if (!donna_node_add_property (node, "_exec", G_TYPE_POINTER, &v,
            (refresher_fn) gtk_true, NULL, &err))
    {
        g_prefix_error (&err, "Provider 'exec': Failed to create a new node: "
                "Couldn't set internal exec property: ");
        donna_task_take_error (task, err);
        g_free (ex->terminal);
        g_slice_free (struct exec, ex);
        g_value_unset (&v);
        return DONNA_TASK_FAILED;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_ICON);
    g_value_take_object (&v, g_themed_icon_new ("application-x-executable"));
    donna_node_set_property_value (node, "icon", &v);
    g_value_unset (&v);

    klass->lock_nodes (_provider);
    n = klass->get_cached_node (_provider, location);
    if (n)
    {
        /* already one added while we were busy */
        g_object_unref (node);
        node = n;
        /* since we didn't add it to cache, unref_node() isn't called */
        g_free (ex->terminal);
        g_slice_free (struct exec, ex);
    }
    else
        klass->add_node_to_cache (_provider, node);
    klass->unlock_nodes (_provider);

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    /* take_object to not increment the ref count, as it was already done for
     * this task in add_node_to_cache() */
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_exec_has_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_INVALID_CALL,
            "Provider 'exec': has_children() not supported");
    return DONNA_TASK_FAILED;
}
