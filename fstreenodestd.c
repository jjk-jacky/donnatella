
#include <gtk/gtk.h>
#include "fstreeprovider.h"
#include "fstreenodestd.h"

static enum
{
    PROP_0,
    PROP_PROVIDER,
    PROP_LOCATION,
    PROP_NAME,
    NB_PROPS
};

struct _FsTreeNodeStdPrivate
{
    FsTreeProvider *provider;
    gchar *location;
    gchar *name;
};

static gboolean
fstree_node_std_set_location (FsTreeNode  *node,
                              const gchar *location,
                              GError     **error)
{
}

static gboolean
fstree_node_std_set_name (FsTreeNode  *node,
                          const gchar *name,
                          GError     **error)
{
}

static gboolean
fstree_node_std_add_iter (FsTreeNode  *node,
                          GtkTreeIter *iter)
{
}

static gboolean
fstree_node_std_remove_iter (FsTreeNode  *node,
                             GtkTreeIter *iter)
{
}

static void
fstree_node_interface_init (gpointer g_interface, gpointer interface_data)
{
    FsTreeNodeInterface *interface;

    interface = (FsTreeNodeInterface *) g_interface;
    interface->set_location = fstree_node_std_set_location;
    interface->set_name     = fstree_node_std_set_name;
    interface->add_iter     = fstree_node_std_add_iter;
    interface->remove_iter  = fstree_node_std_remove_iter;
}

static void
fstree_node_std_set_property (GObject       *object,
                              guint          id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
    switch (id)
    {
        case PROP_PROVIDER:
            break;

        case PROP_LOCATION:
            break;

        case PROP_NAME:
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
            break;
    }
}

static void
fstree_node_std_get_property (GObject       *object,
                              guint          id,
                              GValue        *value,
                              GParamSpec    *pspec)
{
    FsTreeNodeStd *node;

    node = FSTREE_NODE_STD (object);
    switch (id)
    {
        case PROP_PROVIDER:
            g_value_set_pointer (value, node->priv->provider);
            break;

        case PROP_LOCATION:
            break;

        case PROP_NAME:
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
            break;
    }
}

static void
fstree_node_std_class_init (FsTreeNodeStdClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->set_property = fstree_node_std_set_property;
    o_class->get_property = fstree_node_std_get_property;
    g_object_class_override_property (o_class, PROP_PROVIDER, "provider");
    g_object_class_override_property (o_class, PROP_LOCATION, "location");
    g_object_class_override_property (o_class, PROP_NAME,     "name");
    g_type_class_add_private (klass, sizeof (FsTreeNodeStdPrivate));
}

static void
fstree_node_std_init (FsTreeNodeStd *node)
{
    node->priv = G_TYPE_INSTANCE_GET_PRIVATE (node,
            TYPE_FSTREE_NODE_STD,
            FsTreeNodeStdPrivate);
}

G_DEFINE_TYPE_WITH_CODE (FsTreeNodeStd, fstree_node_std, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (TYPE_FSTREE_NODE, fstree_node_interface_init))

FsTreeNodeStd *
fstree_node_std_new (FsTreeProvider *provider,
                     const gchar    *location,
                     const gchar    *name)
{
    FsTreeNodeStd *node;

    g_return_val_if_fail (IS_FSTREE_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    node = g_object_new (TYPE_FSTREE_NODE_STD, NULL);
    node->priv->provider = provider;
    node->priv->location = g_strdup (location);
    node->priv->name     = g_strdup (name);

    g_object_ref_sink (provider);

    return node;
}
