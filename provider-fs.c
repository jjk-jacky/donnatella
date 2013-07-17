
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
#include "macros.h"

struct _DonnaProviderFsPrivate
{
};

static void             provider_fs_finalize        (GObject *object);

/* DonnaProvider */
static const gchar *    provider_fs_get_domain      (DonnaProvider      *provider);
static DonnaProviderFlags provider_fs_get_flags     (DonnaProvider      *provider);
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
static DonnaTaskState   provider_fs_remove_node     (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node);
static DonnaTaskState   provider_fs_trigger_node    (DonnaProviderBase  *provider,
                                                     DonnaTask          *task,
                                                     DonnaNode          *node);

static void
provider_fs_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain = provider_fs_get_domain;
    interface->get_flags  = provider_fs_get_flags;
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
    pb_class->remove_node   = provider_fs_remove_node;
    pb_class->trigger_node  = provider_fs_trigger_node;

    o_class = (GObjectClass *) klass;
    o_class->finalize = provider_fs_finalize;

//    g_type_class_add_private (klass, sizeof (DonnaProviderFsPrivate));
}

static void
donna_provider_fs_init (DonnaProviderFs *provider)
{
    return;
    DonnaProviderFsPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_FS,
            DonnaProviderFsPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderFs, donna_provider_fs,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_fs_provider_init)
        )

static void
provider_fs_finalize (GObject *object)
{
//    DonnaProviderFsPrivate *priv;

//    priv = DONNA_PROVIDER_FS (object)->priv;

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

static inline gboolean
stat_node (DonnaNode *node, const gchar *filename)
{
    struct stat st;
    GValue value = G_VALUE_INIT;

    if (lstat (filename, &st) == -1)
    {
        if (errno == ENOENT)
            /* seems the file has been removed */
            donna_provider_node_removed (donna_node_peek_provider (node), node);
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

static inline gboolean
set_icon (DonnaNode *node, const gchar *filename)
{
    gchar *mt;
    GIcon *icon;
    GtkIconInfo *ii;
    GValue value = G_VALUE_INIT;

    mt = content_type_guess (filename);
    if (!mt)
        return FALSE;

    icon = g_content_type_get_icon (mt);
    if (!icon)
    {
        g_free (mt);
        return FALSE;
    }
    ii = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
            icon,/* FIXME: get it from somewhere? */ 16, 0);
    if (!ii)
    {
        g_object_unref (icon);
        g_free (mt);
        return FALSE;
    }

    g_value_init (&value, G_TYPE_OBJECT);
    g_value_take_object (&value, gtk_icon_info_load_icon (ii, NULL));
    donna_node_set_property_value (node, "icon", &value);
    g_value_unset (&value);

    g_object_unref (ii);
    g_object_unref (icon);
    g_free (mt);
    return TRUE;
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
        ret = set_icon (node, filename);
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
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Renaming failed: %s", buf);
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
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change permissions: %s", buf);
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
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change owner: %s", buf);
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
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change group: %s", buf);
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
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change time, lstat failed: %s", buf);
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
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Failed to change time: %s", buf);
            g_free (filename);
            return DONNA_TASK_FAILED;
        }

        /* get the new values (atime could be ignored, ctime was updated) */
        if (lstat (filename, &st) == -1)
        {
            gchar buf[255];
            if (strerror_r (errno, buf, 255) != 0)
                buf[0] = '\0';
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Time was set, but post-lstat failed: %s", buf);
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
          const gchar       *filename,
          gboolean           need_lock)
{
    DonnaProviderBaseClass *klass;
    DonnaNode       *node;
    DonnaNodeType    type;
    DonnaNodeFlags   flags;
    const gchar     *name;
    gboolean         free_filename = FALSE;

    if (!filename)
    {
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
        set_icon (node, filename);

    if (free_filename)
        g_free ((gchar *) filename);

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    if (need_lock)
    {
        DonnaNode *n;

        klass->lock_nodes (_provider);
        /* did someone already add it while we were busy? */
        n = klass->get_cached_node (_provider, location);
        if (n)
        {
            klass->unlock_nodes (_provider);
            g_object_unref (node);
            return n;
        }
    }
    /* this adds another reference (from our own) so we send it to the caller */
    klass->add_node_to_cache (_provider, node);
    if (need_lock)
        klass->unlock_nodes (_provider);

    return node;
}

static DonnaTaskState
provider_fs_new_node (DonnaProviderBase  *_provider,
                      DonnaTask          *task,
                      const gchar        *location)
{
    DonnaNode *node;
    GValue    *value;

    node = new_node (_provider, location, NULL, TRUE);
    if (!node)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Cannot create node, file doesn't exist: %s", location);
        return DONNA_TASK_FAILED;
    }

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
    gboolean                 is_locked;
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
    is_locked = match = FALSE;
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

                if (!is_locked)
                {
                    klass->lock_nodes (_provider);
                    is_locked = TRUE;
                }
                node = klass->get_cached_node (_provider, location);
                if (!node)
                    node = new_node (_provider, location, b, FALSE);
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
    if (is_locked)
        klass->unlock_nodes (_provider);

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
provider_fs_remove_node (DonnaProviderBase  *_provider,
                         DonnaTask          *task,
                         DonnaNode          *node)
{
    /* TODO */
    return DONNA_TASK_FAILED;
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
        DonnaTask *tp;
        gchar *s;

        tp = donna_task_process_new (NULL, filename, FALSE, NULL, NULL);
        s = strrchr (filename, '/');
        *s = '\0';
        g_object_set (tp, "workdir", filename, NULL);

        /* we don't have app, so we can't use donna_app_run_task() (having made
         * tp blocking, of course). Luckily this is a simple run & be done, so
         * we can "abuse" the system and just call (blocking) donna_task_run()
         * directly. */
        g_object_ref_sink (tp);
        donna_task_run (tp);
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
