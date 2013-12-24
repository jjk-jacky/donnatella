
#include "config.h"

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include "provider-exec.h"
#include "provider.h"
#include "task-process.h"
#include "treeview.h"
#include "node.h"
#include "macros.h"


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
    struct children *data;
    gchar *location;

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

    task = donna_task_process_new (data->workdir, location + 1, TRUE,
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
    DonnaApp *app = NULL;
    DonnaTask *task;
    gchar *location;
    gchar *cmdline;
    gboolean wait;
    task_closer_fn closer = NULL;
    struct children *data = NULL;

    location = donna_node_get_location (node);

    if (*location == '!')
    {
        cmdline = location + 1;
        wait = TRUE;
    }
    else if (*location == '&')
    {
        cmdline = location + 1;
        wait = FALSE;
    }
    else if (*location == '>')
    {
        gchar *terminal = NULL;

        g_object_get (provider, "app", &app, NULL);

        donna_config_get_string (donna_app_peek_config (app), NULL, &terminal,
                    "providers/exec/terminal");
        if (terminal)
            cmdline = g_strdup_printf ("%s %s", terminal, location + 1);
        else
        {
            /* try to default to common terminal emulator */
            terminal = g_find_program_in_path ("urxvt");
            if (!terminal)
                terminal = g_find_program_in_path ("rxvt");
            if (!terminal)
                terminal = g_find_program_in_path ("xterm");
            if (!terminal)
                terminal = g_find_program_in_path ("konsole");

            if (terminal)
                cmdline = g_strdup_printf ("%s -e %s", terminal, location + 1);
            else
            {
                /* those should be using -x instead of -e */
                terminal = g_find_program_in_path ("xfce4-terminal");
                if (!terminal)
                    terminal = g_find_program_in_path ("gnome-terminal");
                if (terminal)
                    cmdline = g_strdup_printf ("%s -x %s", terminal, location + 1);
                else
                {
                    g_set_error (error, DONNA_PROVIDER_ERROR,
                            DONNA_PROVIDER_ERROR_OTHER,
                            "Provider 'exec': Unable to find a terminal, you can define the prefix in /providers/exec/terminal");
                    g_object_unref (app);
                    g_free (location);
                    return NULL;
                }
            }
        }

        g_free (terminal);
        g_free (location);
        location = cmdline;
        wait = FALSE;
    }
    else if (*location == '<')
    {
        data = g_new0 (struct children, 1);
        data->ref_count = 2; /* one for task, one for duplicator */
        data->provider = provider;
        data->node = g_object_ref (node);
        data->node_types = node_types;
        data->children = g_ptr_array_new_full (0, g_object_unref);
        cmdline = location + 1;
        wait = TRUE;
        closer = (task_closer_fn) children_closer;
    }
    else /* .desktop file */
    {
        GAppInfo *appinfo;

        if (*location == '/')
            appinfo = (GAppInfo *) g_desktop_app_info_new_from_filename (location);
        else
        {
            gsize len = strlen (location);
            /* 9 = 8 + 1; 8 = strlen (".desktop") */
            if (len < 9 || !streq (location + len - 8, ".desktop"))
                cmdline = g_strdup_printf ("%s.desktop", location);
            else
                cmdline = location;
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

    if (!app)
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

static gboolean
refresher (DonnaTask    *task,
           DonnaNode    *node,
           const gchar  *name)
{
    return TRUE;
}

static DonnaTaskState
provider_exec_new_node (DonnaProviderBase  *_provider,
                        DonnaTask          *task,
                        const gchar        *location)
{
    DonnaProviderBaseClass *klass;
    DonnaNode *node;
    DonnaNode *n;
    GValue *value;
    GValue v = G_VALUE_INIT;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);

    node = donna_node_new ((DonnaProvider *) _provider, location,
            (*location == '<') ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
            NULL, refresher, NULL, location,
            DONNA_NODE_ICON_EXISTS);
    if (!node)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'exec': Unable to create a new node");
        return DONNA_TASK_FAILED;
    }

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
    }
    else
        klass->add_node_to_cache (_provider, node);
    klass->unlock_nodes (_provider);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
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
