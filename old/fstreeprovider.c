
#include <gtk/gtk.h>
#include "fstreeprovider.h"

/*** NODE ***/

static void
fstree_node_default_init (FsTreeNodeInterface *klass)
{
    g_object_interface_install_property (klass,
            g_param_spec_pointer (
                "provider",
                "provider",
                "Provider handling this node",
                G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY));

    g_object_interface_install_property (klass,
            g_param_spec_string (
                "location",
                "location",
                "Location this node represents (for its provider)",
                NULL,
                G_PARAM_READABLE | G_PARAM_CONSTRUCT));

    g_object_interface_install_property (klass,
            g_param_spec_string (
                "name",
                "name",
                "Name to be used when displaying the node (in tree)",
                NULL,
                G_PARAM_READABLE | G_PARAM_CONSTRUCT));

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
}

G_DEFINE_INTERFACE (FsTreeNode, fstree_node, G_TYPE_OBJECT)

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
fstree_provider_default_init (FsTreeProviderInterface *klass)
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
}

G_DEFINE_INTERFACE (FsTreeProvider, fstree_provider, G_TYPE_INITIALLY_UNOWNED)

FsTreeNode *
fstree_provider_get_node (FsTreeProvider *provider,
                          const gchar    *location,
                          GError        **error)
{
    FsTreeProviderInterface *interface;

    g_return_val_if_fail (IS_FSTREE_PROVIDER (provider), NULL);

    interface = FSTREE_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node != NULL, NULL);

    return (*interface->get_node) (provider, location, error);
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
