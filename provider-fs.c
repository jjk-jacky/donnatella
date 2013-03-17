
#include <glib-object.h>
#include <string.h>                 /* strrchr() */
#include "provider-fs.h"
#include "provider.h"
#include "node.h"
#include "task.h"

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

static gboolean
refresher (DonnaTask    *task,
           DonnaNode    *node,
           const gchar  *name)
{
    /* TODO */
    return FALSE;
}

static DonnaTaskState
setter (DonnaTask       *task,
        DonnaNode       *node,
        const gchar     *name,
        const GValue    *value)
{
    /* TODO */
    return DONNA_TASK_FAILED;
}

static DonnaNode *
new_node (DonnaProviderBase *_provider,
          const gchar       *location,
          gboolean           need_lock)
{
    DonnaProviderBaseClass *klass;
    DonnaNode       *node;
    DonnaNodeType    type;
    const gchar     *name;
    DonnaNodeFlags   flags;

    if (!g_file_test (location, G_FILE_TEST_EXISTS))
        return NULL;

    type = (g_file_test (location, G_FILE_TEST_IS_DIR))
        ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM;

    /* FIXME: g_display_name or something... */
    if (location[0] == '/' && location[1] == '\0')
        name = location;
    else
        /* we go past the last / */
        name = strrchr (location, '/') + 1;

    flags = DONNA_NODE_ALL_EXISTS | DONNA_NODE_NAME_WRITABLE;

    node = donna_node_new (DONNA_PROVIDER (_provider),
            location,
            type,
            refresher,
            setter,
            name,
            flags);

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    if (need_lock)
        klass->lock_nodes (_provider);
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

    node = new_node (_provider, location, TRUE);
    if (!node)
        /* FIXME: set task error */
        return DONNA_TASK_FAILED;

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
    gchar                   *location;
    gchar                   *loc;
    GDir                    *dir;
    const gchar             *name;
    gboolean                 is_locked;
    gboolean                 match;
    GValue                  *value;
    GPtrArray               *arr;

    if (!(node_types & DONNA_NODE_ITEM || node_types & DONNA_NODE_CONTAINER))
        return DONNA_TASK_FAILED;

    donna_node_get (node, FALSE, "location", &location, NULL);
    dir = g_dir_open (location, 0, &err);
    if (err)
    {
        donna_task_take_error (task, err);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    loc = location;
    /* root is "/" so it would get us "//bin" */
    if (loc[0] == '/' && loc[1] == '\0')
        ++loc;

    if (get_children)
        arr = g_ptr_array_new_full (16, g_object_unref);

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    is_locked = match = FALSE;
    while ((name = g_dir_read_name (dir)))
    {
        gchar  buf[1024];
        gchar *b;

        if (donna_task_is_cancelling (task))
        {
            g_dir_close (dir);
            g_free (location);
            return DONNA_TASK_CANCELLED;
        }

        b = buf;
        if (g_snprintf (buf, 1024, "%s/%s", loc, name) >= 1024)
            b = g_strdup_printf ("%s/%s", loc, name);

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

        if (b != buf)
            g_free (b);

        if (match)
        {
            if (get_children)
            {
                DonnaNode *node;

                if (!is_locked)
                {
                    klass->lock_nodes (_provider);
                    is_locked = TRUE;
                }
                node = klass->get_cached_node (_provider, b);
                if (!node)
                    node = new_node (_provider, b, FALSE);
                if (node)
                    g_ptr_array_add (arr, node);
                else
                    g_warning ("Provider 'fs': Unable to create a node for '%s'",
                            b);
            }
            else
                break;
        }
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

    g_free (location);
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
