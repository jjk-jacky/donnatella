
#include <glib-object.h>
#include <string.h>                 /* strrchr() */
#include "provider-fs.h"
#include "provider-base.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "sharedstring.h"

struct _DonnaProviderFsPrivate
{
};

static void             provider_fs_finalize        (GObject *object);

/* DonnaProvider */
static const gchar *    provider_fs_get_domain      (DonnaProvider      *provider);
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

static DonnaTaskState
provider_fs_new_node (DonnaProviderBase  *_provider,
                      DonnaTask          *task,
                      const gchar        *location)
{
    DonnaProvider       *provider = DONNA_PROVIDER (_provider);
    DonnaNodeType        type;
    const gchar         *name;
    DonnaNodeFlags       flags;
    DonnaNode           *node;
    GValue              *value;

    if (!g_file_test (location, G_FILE_TEST_EXISTS))
        /* FIXME: set task error */
        return DONNA_TASK_FAILED;

    type = (g_file_test (location, G_FILE_TEST_IS_DIR))
        ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM;

    /* FIXME: g_display_name or something... */
    if (location[0] == '/' && location[1] == '\0')
        name = location;
    else
        /* we go past the last / */
        name = strrchr (location, '/') + 1;

    flags = DONNA_NODE_ALL_EXISTS | DONNA_NODE_NAME_WRITABLE;

    node = donna_node_new (provider, donna_shared_string_new_dup (location),
            type, refresher, setter, donna_shared_string_new_dup (name), flags);

    DONNA_PROVIDER_BASE_GET_CLASS (_provider)->add_node_to_cache (_provider,
            node);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    /* take_object to not increment the ref count, as it was already done for
     * this task when adding it to the cache (add_node_to_cache) */
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_fs_has_children (DonnaProviderBase  *_provider,
                          DonnaTask          *task,
                          DonnaNode          *node,
                          DonnaNodeType       node_types)
{
    GError            *err = NULL;
    DonnaSharedString *location;
    GDir              *dir;
    const gchar       *name;
    gboolean           has_children = FALSE;
    GValue            *value;

    if (!(node_types & DONNA_NODE_ITEM || node_types & DONNA_NODE_CONTAINER))
        return DONNA_TASK_FAILED;

    donna_node_get (node, FALSE, "location", &location, NULL);
    dir = g_dir_open (donna_shared_string (location), 0, &err);
    if (err)
    {
        donna_task_take_error (task, err);
        donna_shared_string_unref (location);
        return DONNA_TASK_FAILED;
    }

    while ((name = g_dir_read_name (dir)))
    {
        gchar  buf[1024];
        gchar *b;
        gboolean is_dir;

        if (donna_task_is_cancelling (task))
        {
            g_dir_close (dir);
            donna_shared_string_unref (location);
            return DONNA_TASK_CANCELLED;
        }

        b = buf;
        if (g_snprintf (buf, 1024, "%s/%s",
                    donna_shared_string (location), name) >= 1024)
            b = g_strdup_printf ("%s/%s", donna_shared_string (location), name);


        if (node_types & DONNA_NODE_CONTAINER && node_types & DONNA_NODE_ITEM)
            has_children = TRUE;
        else
        {
            if (g_file_test (b, G_FILE_TEST_IS_DIR))
            {
                if (node_types & DONNA_NODE_CONTAINER)
                    has_children = TRUE;
            }
            else
            {
                if (node_types & DONNA_NODE_ITEM)
                    has_children = TRUE;
            }
        }

        if (b != buf)
            g_free (b);

        if (has_children)
            break;
    }
    g_dir_close (dir);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, has_children);
    donna_task_release_return_value (task);

    donna_shared_string_unref (location);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_fs_get_children (DonnaProviderBase  *provider,
                          DonnaTask          *task,
                          DonnaNode          *node,
                          DonnaNodeType       node_types)
{
}

static DonnaTaskState
provider_fs_remove_node (DonnaProviderBase  *provider,
                         DonnaTask          *task,
                         DonnaNode          *node)
{
    /* TODO */
    return DONNA_TASK_FAILED;
}
