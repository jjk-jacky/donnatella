
#include <gtk/gtk.h>
#include "fstreeprovider.h"

/*** NODE ***/

static void
fstree_node_base_init (gpointer interface)
{
    static gboolean init = FALSE;

    if (!init)
    {
        g_signal_new ("destroy",
                TYPE_FSTREE_NODE,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (FsTreeNodeInterface, destroy),
                NULL,
                NULL,
                NULL, /* marshall_needed */
                G_TYPE_NONE,
                1,
                G_TYPE_POINTER);

        init = TRUE;
    }
}

static void
fstree_node_class_init (gpointer g_class, gpointer class_data)
{
    g_object_interface_install_property (g_class,
            g_param_spec_pointer (
                "provider",
                "provider",
                "Provider handling this node",
                G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property (g_class,
            g_param_spec_string (
                "location",
                "location",
                "Location this node represents (for its provider)",
                NULL,
                G_PARAM_READABLE | G_PARAM_CONSTRUCT));

    g_object_interface_install_property (g_class,
            g_param_spec_string (
                "name",
                "name",
                "Name to be used when displaying the node (in tree)",
                NULL,
                G_PARAM_READABLE | G_PARAM_CONSTRUCT));
}

GType
fstree_node_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        const GTypeInfo type_info =
        {
            sizeof (FsTreeNodeInterface),       /* class_size */
            fstree_node_base_init,              /* base_init */
            NULL,                               /* base_finalize */
            fstree_node_class_init,             /* class_init */
            NULL,                               /* class_finalize */
            NULL,                               /* class_data */
            0,                                  /* instance_size */
            0,                                  /* n_preallocs */
            NULL,                               /* instance_init */
            NULL                                /* value_table */
        };

        type = g_type_register_static (G_TYPE_INTERFACE, "FsTreeNode",
                &type_info, 0);
        g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
    }

    return type;
}

gboolean
fstree_node_set_location (FsTreeNode     *node,
                          const gchar    *location,
                          GError        **error)
{
    FsTreeNodeInterface *interface;

    g_return_val_if_fail (IS_FSTREE_NODE (node), FALSE);
    g_return_val_if_fail (location != NULL, FALSE);

    interface = FSTREE_NODE_GET_INTERFACE (node);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_location != NULL, FALSE);

    return (*interface->set_location) (node, location, error);
}

gboolean
fstree_node_set_name (FsTreeNode     *node,
                      const gchar    *name,
                      GError        **error)
{
    FsTreeNodeInterface *interface;

    g_return_val_if_fail (IS_FSTREE_NODE (node), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    interface = FSTREE_NODE_GET_INTERFACE (node);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_name != NULL, FALSE);

    return (*interface->set_name) (node, name, error);
}

FsTreeNode **
fstree_node_get_children (FsTreeNode *node, GError **error)
{
    FsTreeNodeInterface *interface;
    FsTreeProvider *provider;

    g_return_val_if_fail (IS_FSTREE_NODE (node), NULL);

    g_object_get (G_OBJECT (node), "provider", &provider, NULL);
    return fstree_provider_get_children (provider, node, error);
}

gboolean
fstree_node_add_iter (FsTreeNode *node, GtkTreeIter *iter)
{
    FsTreeNodeInterface *interface;

    g_return_val_if_fail (IS_FSTREE_NODE (node), FALSE);

    interface = FSTREE_NODE_GET_INTERFACE (node);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->add_iter != NULL, FALSE);

    return (*interface->add_iter) (node, iter);
}

gboolean
fstree_node_remove_iter (FsTreeNode *node, GtkTreeIter *iter)
{
    FsTreeNodeInterface *interface;

    g_return_val_if_fail (IS_FSTREE_NODE (node), FALSE);

    interface = FSTREE_NODE_GET_INTERFACE (node);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->remove_iter != NULL, FALSE);

    return (*interface->remove_iter) (node, iter);
}

/*** PROVIDER ***/

static void
fstree_provider_base_init (gpointer interface)
{
    static gboolean init = FALSE;

    if (!init)
    {
        g_signal_new ("node-created",
                TYPE_FSTREE_PROVIDER,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (FsTreeProviderInterface, node_created),
                NULL,
                NULL,
                NULL, /* marshall_needed */
                G_TYPE_NONE,
                1,
                G_TYPE_POINTER);

        init = TRUE;
    }
}

GType
fstree_provider_get_type (void)
{
    static GType type = 0;

    if (!type)
    {
        const GTypeInfo type_info =
        {
            sizeof (FsTreeProviderInterface),   /* class_size */
            fstree_provider_base_init,          /* base_init */
            NULL,                               /* base_finalize */
            NULL,                               /* class_init */
            NULL,                               /* class_finalize */
            NULL,                               /* class_data */
            0,                                  /* instance_size */
            0,                                  /* n_preallocs */
            NULL,                               /* instance_init */
            NULL                                /* value_table */
        };

        type = g_type_register_static (G_TYPE_INTERFACE, "FsTreeProvider",
                &type_info, 0);
    }

    return type;
}

FsTreeNode *
fstree_provider_get_node (FsTreeProvider *provider)
{
    FsTreeProviderInterface *interface;

    g_return_val_if_fail (IS_FSTREE_PROVIDER (provider), NULL);

    interface = FSTREE_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node != NULL, NULL);

    return (*interface->get_node) (provider);
}

FsTreeNode **
fstree_provider_get_children (FsTreeProvider *provider,
                              FsTreeNode     *node,
                              GError        **error)
{
    FsTreeProviderInterface *interface;

    g_return_val_if_fail (IS_FSTREE_PROVIDER (provider), NULL);
    g_return_val_if_fail (IS_FSTREE_NODE (node), NULL);

    interface = FSTREE_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_children != NULL, NULL);

    return (*interface->get_children) (provider, node, error);
}
