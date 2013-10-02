
#include <gtk/gtk.h>
#include <string.h>                 /* strrchr() */
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>
#include "provider-fs.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "task-process.h"
#include "app.h"
#include "macros.h"

struct io_engine
{
    gchar *name;
    fs_engine_io_task io_engine_task;
};

struct _DonnaProviderFsPrivate
{
    GSList *io_engines;
};

static void             provider_fs_finalize        (GObject *object);

/* DonnaProvider */
static const gchar *    provider_fs_get_domain      (DonnaProvider      *provider);
static DonnaProviderFlags provider_fs_get_flags     (DonnaProvider      *provider);
static DonnaTask *      provider_fs_io_task         (DonnaProvider      *provider,
                                                     DonnaIoType         type,
                                                     gboolean            is_source,
                                                     GPtrArray          *sources,
                                                     DonnaNode          *dest,
                                                     GError            **error);
static gchar *          provider_fs_get_context_alias_new_nodes (
                                                     DonnaProvider      *provider,
                                                     const gchar        *extra,
                                                     DonnaNode          *location,
                                                     const gchar        *prefix,
                                                     GError            **error);
static gboolean         provider_fs_get_context_item_info (
                                                     DonnaProvider      *provider,
                                                     const gchar        *item,
                                                     const gchar        *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode          *node_ref,
                                                     tree_context_get_sel_fn get_sel,
                                                     gpointer            get_sel_data,
                                                     DonnaContextInfo   *info,
                                                     GError            **error);
/* DonnaProviderBase */
static DonnaTaskState   provider_fs_new_node        (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     const gchar        *location);
static DonnaTaskState   provider_fs_has_children    (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node,
                                                     DonnaNodeType       node_types);
static DonnaTaskState   provider_fs_get_children    (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node,
                                                     DonnaNodeType       node_types);
static DonnaTaskState   provider_fs_trigger_node    (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node);
static DonnaTaskState   provider_fs_new_child       (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *parent,
                                                     DonnaNodeType       type,
                                                     const gchar        *name);

/* fsengine-basic.c */
DonnaTask *     donna_fs_engine_basic_io_task       (DonnaApp           *app,
                                                     DonnaIoType         type,
                                                     GPtrArray          *sources,
                                                     DonnaNode          *dest,
                                                     fs_parse_cmdline    parser,
                                                     GError            **error);

static void
provider_fs_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain                   = provider_fs_get_domain;
    interface->get_flags                    = provider_fs_get_flags;
    interface->io_task                      = provider_fs_io_task;
    interface->get_context_alias_new_nodes  = provider_fs_get_context_alias_new_nodes;
    interface->get_context_item_info        = provider_fs_get_context_item_info;
}

static void
donna_provider_fs_class_init (DonnaProviderFsClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node      = provider_fs_new_node;
    pb_class->has_children  = provider_fs_has_children;
    pb_class->get_children  = provider_fs_get_children;
    pb_class->trigger_node  = provider_fs_trigger_node;
    pb_class->new_child     = provider_fs_new_child;

    o_class = (GObjectClass *) klass;
    o_class->finalize = provider_fs_finalize;

    g_type_class_add_private (klass, sizeof (DonnaProviderFsPrivate));
}

static void
donna_provider_fs_init (DonnaProviderFs *provider)
{
    DonnaProviderFsPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_FS,
            DonnaProviderFsPrivate);

    donna_provider_fs_add_io_engine (provider, "basic",
            donna_fs_engine_basic_io_task, NULL);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderFs, donna_provider_fs,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_fs_provider_init)
        )

static void
free_io_engine (struct io_engine *ioe)
{
    g_free (ioe->name);
    g_free (ioe);
}

static void
provider_fs_finalize (GObject *object)
{
    DonnaProviderFsPrivate *priv;

    priv = DONNA_PROVIDER_FS (object)->priv;
    g_slist_free_full (priv->io_engines, (GDestroyNotify) free_io_engine);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_fs_parent_class)->finalize (object);
}

static DonnaProviderFlags
provider_fs_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_FS (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return 0;
}

static const gchar *
provider_fs_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_FS (provider), NULL);
    return "fs";
}

gboolean
donna_provider_fs_add_io_engine (DonnaProviderFs    *pfs,
                                 const gchar        *name,
                                 fs_engine_io_task   engine,
                                 GError            **error)
{
    DonnaProviderFsPrivate *priv;
    GSList *l;
    struct io_engine *ioe;

    g_return_val_if_fail (DONNA_IS_PROVIDER_FS (pfs), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (engine != NULL, FALSE);
    priv = pfs->priv;

    for (l = priv->io_engines; l; l = l->next)
    {
        if (streq (((struct io_engine *) l->data)->name, name))
        {
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                    "Provider 'fs': Cannot add IO engine '%s', there's already one",
                    name);
            return FALSE;
        }
    }

    ioe = g_new (struct io_engine, 1);
    ioe->name = g_strdup (name);
    ioe->io_engine_task = engine;
    priv->io_engines = g_slist_prepend (priv->io_engines, ioe);
    return TRUE;
}

static gchar *
parse_cmdline (const gchar        *cmdline,
               GPtrArray          *sources,
               DonnaNode          *dest,
               GError            **error)
{
    GString *str;
    const gchar *fmt;
    const gchar *s;

    s = fmt = cmdline;
    str = g_string_new (NULL);
    while ((s = strchr (s, '%')))
    {
        gchar *ss;
        gchar *qs;
        guint i;

        switch (s[1])
        {
            case 's':
                g_string_append_len (str, fmt, s - fmt);
                for (i = 0; i < sources->len; ++i)
                {
                    ss = donna_node_get_location (sources->pdata[i]);
                    qs = g_shell_quote (ss);
                    if (i > 0)
                        g_string_append_c (str, ' ');
                    g_string_append (str, qs);
                    g_free (qs);
                    g_free (ss);
                }
                s += 2;
                fmt = s;
                break;

            case 'd':
                g_string_append_len (str, fmt, s - fmt);
                ss = donna_node_get_location (dest);
                qs = g_shell_quote (ss);
                g_string_append (str, qs);
                g_free (qs);
                g_free (ss);
                s += 2;
                fmt = s;
                break;

            case '%':
                g_string_append_len (str, fmt, s - fmt);
                g_string_append_c (str, '%');
                s += 2;
                fmt = s;
                break;

            default:
                g_set_error (error, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'fs': Invalid syntax in command line: %s",
                        cmdline);
                g_string_free (str, TRUE);
                return NULL;
        }
    }
    g_string_append (str, fmt);

    return g_string_free (str, FALSE);
}

static DonnaTask *
provider_fs_io_task (DonnaProvider      *provider,
                     DonnaIoType         type,
                     gboolean            is_source,
                     GPtrArray          *sources,
                     DonnaNode          *dest,
                     GError            **error)
{
    DonnaTask *task;
    GSList *l;
    struct io_engine *ioe;

    if ((!is_source && donna_node_peek_provider (sources->pdata[0]) != provider)
        || (is_source && type != DONNA_IO_DELETE
            && donna_node_peek_provider (dest) != provider))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'fs': Does not support IO operation outside of 'fs'");
        return NULL;
    }

    for (l = ((DonnaProviderFs *) provider)->priv->io_engines; l; l = l->next)
    {
        ioe = l->data;

        task = ioe->io_engine_task (((DonnaProviderBase *) provider)->app,
                type, sources, dest, parse_cmdline, error);
        if (G_UNLIKELY (!task))
        {
            if (l->next)
                g_clear_error (error);
            else
            {
                g_prefix_error (error, "Provider 'fs': Failed to create IO task: ");
                return NULL;
            }
        }
    }

    if (G_UNLIKELY (!task))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'fs': No IO engine available");
        return NULL;
    }

    return task;
}

static gchar *
provider_fs_get_context_alias_new_nodes (DonnaProvider      *provider,
                                         const gchar        *extra,
                                         DonnaNode          *location,
                                         const gchar        *prefix,
                                         GError            **error)
{
    return g_strdup_printf ("%snew_folder,%snew_file", prefix, prefix);
}

static gboolean
provider_fs_get_context_item_info (DonnaProvider            *provider,
                                   const gchar              *item,
                                   const gchar              *extra,
                                   DonnaContextReference     reference,
                                   DonnaNode                *node_ref,
                                   tree_context_get_sel_fn   get_sel,
                                   gpointer                  get_sel_data,
                                   DonnaContextInfo         *info,
                                   GError                  **error)
{
    if (streq (item, "new_folder"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New Folder";
        info->icon_name = "folder-new";
        info->trigger = "command:tree_goto_line (%o, f+s, "
            "@node_new_child (@tree_get_location (%o), c, "
            "@ask_text (Please enter the name of the new folder to create)))";
        return TRUE;
    }
    else if (streq (item, "new_file"))
    {
        info->is_visible = info->is_sensitive = TRUE;
        info->name = "New File";
        info->icon_name = "document-new";
        info->trigger = "command:tree_goto_line (%o, f+s, "
            "@node_new_child (@tree_get_location (%o), i, "
            "@ask_text (Please enter the name of the new file to create)))";
        return TRUE;
    }

    g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
            DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
            "Provider 'fs': No such context item: '%s'", item);
    return FALSE;
}

static inline gboolean
stat_node (DonnaNode *node, const gchar *filename)
{
    struct stat st;
    GValue value = G_VALUE_INIT;

    if (lstat (filename, &st) == -1)
    {
        if (errno == ENOENT)
            /* seems the file has been deleted */
            donna_provider_node_deleted (donna_node_peek_provider (node), node);
        return FALSE;
    }

    g_value_init (&value, G_TYPE_UINT);

    g_value_set_uint (&value, (guint) st.st_mode);
    donna_node_set_property_value (node, "mode", &value);
    g_value_set_uint (&value, (guint) st.st_uid);
    donna_node_set_property_value (node, "uid", &value);
    g_value_set_uint (&value, (guint) st.st_gid);
    donna_node_set_property_value (node, "gid", &value);

    g_value_unset (&value);
    g_value_init (&value, G_TYPE_UINT64);

    g_value_set_uint64 (&value, (guint64) st.st_size);
    donna_node_set_property_value (node, "size", &value);
    g_value_set_uint64 (&value, (guint64) st.st_ctime);
    donna_node_set_property_value (node, "ctime", &value);
    g_value_set_uint64 (&value, (guint64) st.st_mtime);
    donna_node_set_property_value (node, "mtime", &value);
    g_value_set_uint64 (&value, (guint64) st.st_atime);
    donna_node_set_property_value (node, "atime", &value);

    g_value_unset (&value);
    return TRUE;
}

static inline gchar *
content_type_guess (const gchar *filename)
{
    gboolean uncertain;
    gchar *mt;

    mt = g_content_type_guess (filename, NULL, 0, &uncertain);
    if (uncertain)
    {
        gint fd;

        fd = open (filename, O_RDONLY | O_NONBLOCK);
        if (fd > -1)
        {
            guchar data[1024];
            gssize len;

            len = read (fd, data, 1024);
            if (len > 0)
            {
                g_free (mt);
                mt = g_content_type_guess (filename, data, len, NULL);
            }
            close (fd);
        }
    }
    return mt;
}

static DonnaTaskState
set_icon_worker (DonnaTask *task, gpointer _data)
{
    struct {
        DonnaNode *node;
        const gchar *filename;
    } *data = _data;
    gchar *mt;
    GIcon *icon;
    GtkIconInfo *ii;
    GValue value = G_VALUE_INIT;

    mt = content_type_guess (data->filename);
    if (!mt)
        return DONNA_TASK_FAILED;

    icon = g_content_type_get_icon (mt);
    if (!icon)
    {
        g_free (mt);
        return DONNA_TASK_FAILED;
    }
    ii = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
            icon,/* FIXME: get it from somewhere? */ 16, 0);
    if (!ii)
    {
        g_object_unref (icon);
        g_free (mt);
        return DONNA_TASK_FAILED;
    }

    g_value_init (&value, G_TYPE_OBJECT);
    g_value_take_object (&value, gtk_icon_info_load_icon (ii, NULL));
    donna_node_set_property_value (data->node, "icon", &value);
    g_value_unset (&value);

    g_object_unref (ii);
    g_object_unref (icon);
    g_free (mt);
    return DONNA_TASK_DONE;
}

static inline gboolean
set_icon (DonnaApp *app, DonnaNode *node, const gchar *filename)
{
    DonnaTask *task;
    struct {
        DonnaNode *node;
        const gchar *filename;
    } data = { node, filename };
    gboolean ret;

    task = donna_task_new (set_icon_worker, &data, NULL);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    donna_app_run_task (app, g_object_ref (task));
    donna_task_wait_for_it (task, NULL, NULL);
    ret = donna_task_get_state (task) == DONNA_TASK_DONE;
    g_object_unref (task);
    return ret;
}

static gboolean
refresher (DonnaTask    *task,
           DonnaNode    *node,
           const gchar  *name)
{
    gboolean ret = FALSE;
    gchar *filename;

    filename = donna_node_get_filename (node);

    if (streq (name, "icon"))
    {
        if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
            ret = TRUE;
        else
        {
            DonnaProvider *provider;

            provider = donna_node_peek_provider (node);
            ret = set_icon (((DonnaProviderBase *) provider)->app, node, filename);
        }
    }
    else if (streq (name, "desc"))
    {
        gchar *mt;
        gchar *desc;
        GValue value = G_VALUE_INIT;

        mt = content_type_guess (filename);
        if (!mt)
            goto done;
        desc = g_content_type_get_description (mt);
        g_free (mt);
        if (!desc)
            goto done;
        g_value_init (&value, G_TYPE_STRING);
        g_value_take_string (&value, desc);

        donna_node_set_property_value (node, "desc", &value);
        g_value_unset (&value);
        ret = TRUE;
    }
    else
        ret = stat_node (node, filename);

done:
    g_free (filename);
    return ret;
}

enum
{
    TIME_NONE = 0,
    TIME_MTIME,
    TIME_ATIME,
};

static DonnaTaskState
setter (DonnaTask       *task,
        DonnaNode       *node,
        const gchar     *name,
        const GValue    *value)
{
    GValue v = G_VALUE_INIT;
    gint do_time = TIME_NONE;

    if (streq (name, "name"))
    {
        gboolean is_utf8;
        gchar *old;
        gchar *new;
        gchar *s;
        gchar *new_from_utf8 = NULL;
        gint st;

        /* TODO once we'll have our function to rename/move files, we'll use
         * that to support relative path, moving to dir, creating new path on
         * the fly, etc */

        is_utf8 = g_get_filename_charsets (NULL);

        /* new name cannot contain any path elements */
        new = (gchar *) g_value_get_string (value);
        if (strchr (new, '/') != NULL)
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Invalid new name: path not supported");
            return DONNA_TASK_FAILED;
        }
        if (!is_utf8)
            new = new_from_utf8 = g_filename_from_utf8 (new, -1, NULL, NULL, NULL);

        old = donna_node_get_filename (node);
        s = strrchr (old, '/');
        *s = '\0';
        new = g_strdup_printf ("%s/%s", old, new);
        *s = '/';
        g_free (new_from_utf8);

        if (streq (old, new))
        {
            g_free (old);
            g_free (new);
            return DONNA_TASK_DONE;
        }

        /* check if new exists, if so we fail. TODO: use DonnaTaskHelperAsk to
         * ask user for confirmation of overwriting */
        st = access (new, F_OK);
        if (st == 0)
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Destination name already exists: %s", new);
            g_free (old);
            g_free (new);
            return DONNA_TASK_FAILED;
        }

        st = rename (old, new);
        if (st < 0)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Renaming failed: %s", g_strerror (_errno));
            g_free (old);
            g_free (new);
            return DONNA_TASK_FAILED;
        }

        /* success - let's update the node */

        g_value_init (&v, G_TYPE_STRING);
        g_value_take_string (&v, new);
        if (is_utf8)
            donna_node_set_property_value (node, "location", &v);
        else
        {
            gchar *l;

            donna_node_set_property_value (node, "filename", &v);
            l = donna_node_get_location (node);
            s = strrchr (l, '/');
            *s = '\0';
            s = g_strdup_printf ("%s/%s", l, g_value_get_string (value));
            g_free (l);
            g_value_unset (&v); /* free-s new */

            g_value_init (&v, G_TYPE_STRING);
            g_value_take_string (&v, s);
            donna_node_set_property_value (node, "location", &v);
        }
        donna_node_set_property_value (node, "full-name", &v);
        g_value_unset (&v); /* free-s new */

        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, g_value_get_string (value));
        donna_node_set_property_value (node, "name", &v);
        g_value_unset (&v);

        if (!refresher (NULL, node, "icon"))
            /* unset whatever we had before. This is weird since the file is the
             * same, but also our icon comes from the filename, so for
             * consistency sake this might be better... */
            donna_node_set_property_value (node, "icon", NULL);

        g_free (old);
        return DONNA_TASK_DONE;
    }
    else if (streq (name, "mtime"))
        do_time = TIME_MTIME;
    else if (streq (name, "atime"))
        do_time = TIME_ATIME;
    else if (streq (name, "mode"))
    {
        gchar *filename;
        mode_t mode;

        mode = (mode_t) g_value_get_uint (value);
        mode = mode & (S_IRWXU | S_IRWXG | S_IRWXO);

        filename = donna_node_get_filename (node);
        if (chmod (filename, mode) < 0)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change permissions: %s", g_strerror (_errno));
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* update the node */
        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, (guint) mode);
        donna_node_set_property_value (node, "mode", &v);
        g_value_unset (&v);

        g_free (filename);
        return DONNA_TASK_DONE;
    }
    else if (streq (name, "uid"))
    {
        gchar *filename;
        uid_t uid;

        uid = (uid_t) g_value_get_uint (value);

        filename = donna_node_get_filename (node);
        if (chown (filename, uid, (gid_t) -1) < 0)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change owner: %s", g_strerror (_errno));
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* update the node */
        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, (guint) uid);
        donna_node_set_property_value (node, "uid", &v);
        g_value_unset (&v);

        g_free (filename);
        return DONNA_TASK_DONE;
    }
    else if (streq (name, "gid"))
    {
        gchar *filename;
        gid_t gid;

        gid = (gid_t) g_value_get_uint (value);

        filename = donna_node_get_filename (node);
        if (chown (filename, (uid_t) -1, gid) < 0)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change group: %s", g_strerror (_errno));
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* update the node */
        g_value_init (&v, G_TYPE_UINT);
        g_value_set_uint (&v, (guint) gid);
        donna_node_set_property_value (node, "gid", &v);
        g_value_unset (&v);

        g_free (filename);
        return DONNA_TASK_DONE;
    }

    if (do_time != TIME_NONE)
    {
        gchar *filename;
        guint64 ts;
        struct stat st;
        struct utimbuf times;

        filename = donna_node_get_filename (node);
        if (lstat (filename, &st) == -1)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change time, lstat failed: %s", g_strerror (_errno));
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* preserve current values, so we only change one */
        if (do_time == TIME_MTIME)
        {
            times.modtime = (time_t) g_value_get_uint64 (value);
            times.actime  = st.st_atime;
        }
        else /* TIME_ATIME */
        {
            times.modtime = st.st_mtime;
            times.actime  = (time_t) g_value_get_uint64 (value);
        }

        if (utime (filename, &times) == -1)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change time: %s", g_strerror (_errno));
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* get the new values (atime could be ignored, ctime was updated) */
        if (lstat (filename, &st) == -1)
        {
            gint _errno = errno;
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Time was set, but post-lstat failed: %s", g_strerror (_errno));
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* update the node */
        g_value_init (&v, G_TYPE_UINT64);
        if (do_time == TIME_MTIME)
        {
            g_value_set_uint64 (&v, (guint64) st.st_mtime);
            donna_node_set_property_value (node, "mtime", &v);
        }
        else /* TIME_ATIME */
        {
            g_value_set_uint64 (&v, (guint64) st.st_atime);
            donna_node_set_property_value (node, "atime", &v);
        }
        g_value_unset (&v);

        g_value_init (&v, G_TYPE_UINT64);
        g_value_set_uint64 (&v, (guint64) st.st_ctime);
        donna_node_set_property_value (node, "ctime", &v);
        g_value_unset (&v);

        g_free (filename);
        return DONNA_TASK_DONE;
    }

    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_OTHER,
            "Invalid property: '%s'", name);
    return DONNA_TASK_FAILED;
}

static DonnaNode *
new_node (DonnaProviderBase *_provider,
          const gchar       *location,
          const gchar       *filename)
{
    DonnaProviderBaseClass *klass;
    DonnaNode       *n;
    DonnaNode       *node;
    DonnaNodeType    type;
    DonnaNodeFlags   flags;
    const gchar     *name;
    gboolean         free_filename = FALSE;

    if (!filename)
    {
        gsize len;

        /* if filename encoding if UTF8, just use location */
        if (g_get_filename_charsets (NULL))
            filename = location;
        else
        {
            filename = g_filename_from_utf8 (location, -1, NULL, NULL, NULL);
            free_filename = TRUE;
        }
    }

    if (!g_file_test (filename, G_FILE_TEST_EXISTS))
    {
        if (free_filename)
            g_free ((gchar *) filename);
        return NULL;
    }

    type = (g_file_test (filename, G_FILE_TEST_IS_DIR))
        ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM;

    /* from location, since we want an UTF8 string */
    if (location[0] == '/' && location[1] == '\0')
        name = location;
    else
        /* we go past the last / */
        name = strrchr (location, '/') + 1;

    flags = DONNA_NODE_ALL_EXISTS | DONNA_NODE_NAME_WRITABLE
        | DONNA_NODE_MODE_WRITABLE | DONNA_NODE_UID_WRITABLE
        | DONNA_NODE_GID_WRITABLE | DONNA_NODE_MTIME_WRITABLE
        | DONNA_NODE_ATIME_WRITABLE;
    if (type == DONNA_NODE_CONTAINER)
        flags &= ~(DONNA_NODE_ICON_EXISTS | DONNA_NODE_DESC_EXISTS);

    node = donna_node_new (DONNA_PROVIDER (_provider),
            location,
            type,
            (filename == location) ? NULL : filename,
            refresher,
            setter,
            name,
            flags);

    /* this will load up all properties from a stat() call */
    stat_node (node, filename);
    /* files only: icon is very likely to be used, so let's load it up */
    if (type == DONNA_NODE_ITEM)
        set_icon (_provider->app, node, filename);

    if (free_filename)
        g_free ((gchar *) filename);

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);

    klass->lock_nodes (_provider);
    /* did someone already add it while we were busy? */
    n = klass->get_cached_node (_provider, location);
    if (G_UNLIKELY (n))
    {
        klass->unlock_nodes (_provider);
        g_object_unref (node);
        return n;
    }
    /* this adds another reference (from our own) so we send it to the caller */
    klass->add_node_to_cache (_provider, node);
    klass->unlock_nodes (_provider);

    return node;
}

static DonnaTaskState
provider_fs_new_node (DonnaProviderBase  *_provider,
                      DonnaTask          *task,
                      const gchar        *location)
{
    DonnaNode   *node;
    GString     *str = NULL;
    const gchar *s;
    GValue      *value;

    /* must start with a '/' */
    if (*location != '/')
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Cannot create node, file doesn't exist: %s", location);
        return DONNA_TASK_FAILED;
    }

    /* convert e.g. "///tmp///" into "/tmp" as is expected */
    for (s = location + 1; *s != '\0'; ++s)
    {
        if (*s == '/' && s[-1] == '/')
        {
            if (!str)
            {
                str = g_string_sized_new (strlen (location) - 1);
                g_string_append_len (str, location, s - location);
            }
        }
        else if (str)
            g_string_append_c (str, *s);
    }
    if (!str && s - location > 1 && s[-1] == '/')
        str = g_string_new (location);
    if (str && str->len > 1 && str->str[str->len - 1] == '/')
        g_string_truncate (str, str->len - 1);

    s = (str) ? str->str : location;
    node = new_node (_provider, s, NULL);
    if (!node)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Cannot create node, file doesn't exist: %s", s);
        if (str)
            g_string_free (str, TRUE);
        return DONNA_TASK_FAILED;
    }
    if (str)
        g_string_free (str, TRUE);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    /* take_object to not increment the ref count, as it was already done for
     * this task in new_node */
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
has_get_children (DonnaProviderBase  *_provider,
                  DonnaTask          *task,
                  DonnaNode          *node,
                  DonnaNodeType       node_types,
                  gboolean            get_children)
{
    DonnaProviderBaseClass  *klass;
    GError                  *err = NULL;
    gchar                   *filename;
    gchar                   *fn;
    gboolean                 is_utf8;
    GDir                    *dir;
    const gchar             *name;
    gboolean                 match;
    GValue                  *value;
    GPtrArray               *arr;

    if (!(node_types & DONNA_NODE_ITEM || node_types & DONNA_NODE_CONTAINER))
        return DONNA_TASK_FAILED;

    filename = donna_node_get_filename (node);
    dir = g_dir_open (filename, 0, &err);
    if (err)
    {
        donna_task_take_error (task, err);
        g_free (filename);
        return DONNA_TASK_FAILED;
    }

    fn = filename;
    /* root is "/" so it would get us "//bin" */
    if (fn[0] == '/' && fn[1] == '\0')
        ++fn;

    if (get_children)
    {
        arr = g_ptr_array_new_full (16, g_object_unref);
        is_utf8 = g_get_filename_charsets (NULL);
    }

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    match = FALSE;
    while ((name = g_dir_read_name (dir)))
    {
        gchar  buf[1024];
        gchar *b;

        if (donna_task_is_cancelling (task))
        {
            if (get_children)
                g_ptr_array_unref (arr);
            g_dir_close (dir);
            g_free (filename);
            return DONNA_TASK_CANCELLED;
        }

        b = buf;
        if (g_snprintf (buf, 1024, "%s/%s", fn, name) >= 1024)
            b = g_strdup_printf ("%s/%s", fn, name);

        match = FALSE;
        if (node_types & DONNA_NODE_CONTAINER && node_types & DONNA_NODE_ITEM)
            match = TRUE;
        else
        {
            if (g_file_test (b, G_FILE_TEST_IS_DIR))
            {
                if (node_types & DONNA_NODE_CONTAINER)
                    match = TRUE;
            }
            else
            {
                if (node_types & DONNA_NODE_ITEM)
                    match = TRUE;
            }
        }

        if (match)
        {
            if (get_children)
            {
                DonnaNode *node;
                gchar *location;

                if (is_utf8)
                    location = b;
                else
                    location = g_filename_to_utf8 (b, -1, NULL, NULL, NULL);

                klass->lock_nodes (_provider);
                node = klass->get_cached_node (_provider, location);
                klass->unlock_nodes (_provider);
                if (!node)
                    node = new_node (_provider, location, b);
                if (node)
                    g_ptr_array_add (arr, node);
                else
                    g_warning ("Provider 'fs': Unable to create a node for '%s'",
                            location);
                if (location != b)
                    g_free (location);
            }
            else
            {
                if (b != buf)
                    g_free (b);
                break;
            }
        }

        if (b != buf)
            g_free (b);
    }
    g_dir_close (dir);

    value = donna_task_grab_return_value (task);
    if (get_children)
    {
        g_value_init (value, G_TYPE_PTR_ARRAY);
        /* take our ref on the array */
        g_value_take_boxed (value, arr);
    }
    else
    {
        g_value_init (value, G_TYPE_BOOLEAN);
        g_value_set_boolean (value, match);
    }
    donna_task_release_return_value (task);

    g_free (filename);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_fs_has_children (DonnaProviderBase  *_provider,
                          DonnaTask          *task,
                          DonnaNode          *node,
                          DonnaNodeType       node_types)
{
    return has_get_children (_provider, task, node, node_types, FALSE);
}

static DonnaTaskState
provider_fs_get_children (DonnaProviderBase  *_provider,
                          DonnaTask          *task,
                          DonnaNode          *node,
                          DonnaNodeType       node_types)
{
    return has_get_children (_provider, task, node, node_types, TRUE);
}

static DonnaTaskState
provider_fs_trigger_node (DonnaProviderBase  *_provider,
                          DonnaTask          *task,
                          DonnaNode          *node)
{
    GError *err = NULL;
    DonnaTaskState ret = DONNA_TASK_DONE;
    gchar *filename;
    gchar *mt;

    filename = donna_node_get_filename (node);
    mt = content_type_guess (filename);
    /* trying to avoid launching random files set executable */
    if (g_content_type_can_be_executable (mt)
            && g_file_test (filename, G_FILE_TEST_IS_EXECUTABLE))
    {
        DonnaApp *app;
        DonnaTask *tp;
        gchar *s;

        tp = donna_task_process_new (NULL, filename, FALSE, NULL, NULL, NULL);
        s = strrchr (filename, '/');
        *s = '\0';
        g_object_set (tp, "workdir", filename, NULL);

        g_object_get (_provider, "app", &app, NULL);
        if (!donna_app_run_task_and_wait (app, g_object_ref (tp), task, &err))
        {
            g_prefix_error (&err, "Failed to run task: ");
            donna_task_take_error (task, err);
            g_object_unref (app);
            g_object_unref (tp);
            return DONNA_TASK_FAILED;
        }
        g_object_unref (app);

        ret = donna_task_get_state (tp);
        if (ret == DONNA_TASK_FAILED)
            donna_task_take_error (task, g_error_copy (donna_task_get_error (tp)));
        g_object_unref (tp);

        g_free (mt);
        g_free (filename);
        return ret;
    }
    else
    {
        GAppInfo *appinfo;
        GFile *gfile;
        GList *list;

        gfile = g_file_new_for_path (filename);
        list = g_list_prepend (NULL, gfile);
        appinfo = g_app_info_get_default_for_type (mt, FALSE);

        g_free (mt);
        g_free (filename);

        if (!appinfo)
        {
            ret = DONNA_TASK_FAILED;
            filename = donna_node_get_location (node);
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to open '%s': cannot find application to use", filename);
            g_free (filename);
        }
        else if (!g_app_info_launch (appinfo, list, NULL, &err))
        {
            ret = DONNA_TASK_FAILED;
            donna_task_take_error (task, err);
        }

        if (appinfo)
            g_object_unref (appinfo);
        g_list_free (list);
        g_object_unref (gfile);
        return ret;
    }
}

static DonnaTaskState
provider_fs_new_child (DonnaProviderBase  *_provider,
                       DonnaTask          *task,
                       DonnaNode          *parent,
                       DonnaNodeType       type,
                       const gchar        *name)
{
    DonnaApp *app;
    DonnaConfig *config;
    DonnaNode *node;
    GValue *v;
    gchar *location;
    gchar *filename;
    gchar *s;
    gint mode;
    gint st;

    if (strchr (name, '/'))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_NAME,
                "Invalid filename, cannot contain '/': '%s'", name);
        return DONNA_TASK_FAILED;
    }

    s = donna_node_get_location (parent);
    location = g_strdup_printf ("%s/%s", (streq (s, "/")) ? "" : s, name);
    g_free (s);

    /* if filename encoding if UTF8, just use location */
    if (g_get_filename_charsets (NULL))
        filename = location;
    else
        filename = g_filename_from_utf8 (location, -1, NULL, NULL, NULL);

    g_object_get (_provider, "app", &app, NULL);
    config = donna_app_peek_config (app);
    g_object_unref (app);

    if (type == DONNA_NODE_CONTAINER)
    {
        gint i;
        if (donna_config_get_int (config, &i, "providers/fs/mode_new_folder"))
        {
            gchar buf[4];

            snprintf (buf, 4, "%d", i);
            mode = 00;
            for (s = buf; *s != '\0'; ++s)
            {
                mode *= 8;
                mode += *s - '0';
            }
        }
        else
            mode = 0755;
        st = mkdir (filename, mode);
    }
    else /* DONNA_NODE_ITEM */
    {
        gint i;
        if (donna_config_get_int (config, &i, "providers/fs/mode_new_file"))
        {
            gchar buf[4];

            snprintf (buf, 4, "%d", i);
            mode = 00;
            for (s = buf; *s != '\0'; ++s)
            {
                mode *= 8;
                mode += *s - '0';
            }
        }
        else
            mode = 0644;
again:
        st = open (filename, O_WRONLY | O_CREAT | O_EXCL, mode);
        if (st >= 0)
            close (st);
    }

    if (st < 0)
    {
        gint _errno = errno;
        if (errno == EINTR)
            goto again;
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Failed to create '%s': %s", location, g_strerror (_errno));
        g_free (location);
        if (filename != location)
            g_free (filename);
        return DONNA_TASK_FAILED;
    }

    node = new_node (_provider, location, filename);
    if (G_UNLIKELY (!node))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Failed to get node for newly-created '%s'", location);
        return DONNA_TASK_FAILED;
    }

    /* emit new-child signal */
    donna_provider_node_new_child ((DonnaProvider *) _provider, parent, node);

    /* set node as return value */
    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    /* take our ref on node */
    g_value_take_object (v, node);
    donna_task_release_return_value (task);

    g_free (location);
    if (filename != location)
        g_free (filename);
    return DONNA_TASK_DONE;
}
