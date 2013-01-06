
typedef struct _FmNode  FmNode;
#define TYPE_FMNODE             (G_TYPE_OBJECT)
#define FMNODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FMNODE, FmNode))
#define FMNODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FMNODE, FmNodeClass))
#define IS_FMNODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FMNODE))
#define IS_FMNODE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FMNODE))
#define FMNODE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FMNODE, FmNodeClass))

#include <gtk/gtk.h>
#include "fmprovider.h"

enum
{
    NODE_CREATED,
    NODE_REMOVED,
    NODE_LOCATION_UPDATED,
    NODE_UPDATED,
    NODE_CHILDREN,
    NODE_NEW_CHILD,
    NODE_NEW_CONTENT,
    NB_SIGNALS
};

static guint fmprovider_signals[NB_SIGNALS] = { 0 };

static void
fmprovider_default_init (FmProviderInterface *klass)
{
    g_object_interface_install_property (klass,
            g_param_spec_string (
                "domain",
                "domain",
                "Domain handled by the provider",
                NULL,
                G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY));

    fmprovider_signals[NODE_CREATED] =
        g_signal_new ("node-created",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (FmProviderInterface, node_created),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE,
            1,
            TYPE_FMNODE);
    fmprovider_signals[NODE_REMOVED] =
        g_signal_new ("node-removed",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (FmProviderInterface, node_removed),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE,
            1,
            TYPE_FMNODE);
    fmprovider_signals[NODE_LOCATION_UPDATED] =
        g_signal_new ("node-location-updated",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (FmProviderInterface, node_location_updated),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _STRING */
            G_TYPE_NONE,
            2,
            TYPE_FMNODE,
            G_TYPE_STRING);
    fmprovider_signals[NODE_UPDATED] =
        g_signal_new ("node-updated",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
            G_STRUCT_OFFSET (FmProviderInterface, node_updated),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _STRING */
            G_TYPE_NONE,
            2,
            TYPE_FMNODE,
            G_TYPE_STRING);
    fmprovider_signals[NODE_CHILDREN] =
        g_signal_new ("node-children",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (FmProviderInterface, node_children),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _BOXED_ARRAY */
            G_TYPE_NONE,
            2,
            TYPE_FMNODE,
            TYPE_FMNODE);
    fmprovider_signals[NODE_NEW_CHILD] =
        g_signal_new ("node-new-child",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (FmProviderInterface, node_new_child),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _BOXED */
            G_TYPE_NONE,
            2,
            TYPE_FMNODE,
            TYPE_FMNODE);
    fmprovider_signals[NODE_NEW_CONTENT] =
        g_signal_new ("node-new-content",
            TYPE_FMPROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (FmProviderInterface, node_new_content),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _BOXED */
            G_TYPE_NONE,
            2,
            TYPE_FMNODE,
            TYPE_FMNODE);
}

G_DEFINE_INTERFACE (FmProvider, fmprovider, G_TYPE_OBJECT)

void
fmprovider_node_created (FmProvider  *provider,
                         FmNode      *node)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));

    g_signal_emit (provider, fmprovider_signals[NODE_CREATED], 0, node);
}

void
fmprovider_node_removed (FmProvider  *provider,
                         FmNode      *node)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));

    g_signal_emit (provider, fmprovider_signals[NODE_REMOVED], 0, node);
}

void
fmprovider_node_location_updated (FmProvider  *provider,
                                  FmNode      *node,
                                  const gchar *old_location)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));
    g_return_if_fail (old_location != NULL);

    g_signal_emit (provider, fmprovider_signals[NODE_LOCATION_UPDATED], 0,
            node, old_location);
}

void
fmprovider_node_updated (FmProvider  *provider,
                         FmNode      *node,
                         const gchar *name)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));
    g_return_if_fail (name != NULL);

    g_signal_emit (provider, fmprovider_signals[NODE_UPDATED],
            g_quark_from_string (name),
            node, name);
}

void
fmprovider_node_children (FmProvider  *provider,
                          FmNode      *node,
                          FmNode     **children)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));
    g_return_if_fail (IS_FMNODE (*children));

    g_signal_emit (provider, fmprovider_signals[NODE_CREATED], 0,
            node, children);
}

void
fmprovider_node_new_child (FmProvider  *provider,
                           FmNode      *node,
                           FmNode      *child)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));
    g_return_if_fail (IS_FMNODE (child));

    g_signal_emit (provider, fmprovider_signals[NODE_NEW_CHILD], 0,
            node, child);
}

void
fmprovider_node_new_content (FmProvider  *provider,
                             FmNode      *node,
                             FmNode      *content)
{
    g_return_if_fail (IS_FMPROVIDER (provider));
    g_return_if_fail (IS_FMNODE (node));
    g_return_if_fail (IS_FMNODE (content));

    g_signal_emit (provider, fmprovider_signals[NODE_NEW_CONTENT], 0,
            node, content);
}


FmNode *
fmprovider_get_node (FmProvider  *provider,
                     const gchar *location,
                     gboolean     is_container,
                     GError     **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node != NULL, NULL);

    return (*interface->get_node) (provider, location, is_container, error);
}

/*************
FmTask *
fmprovider_get_node_task (FmProvider     *provider,
                          const gchar    *location,
                          gboolean        is_container,
                          GCallback       callback,
                          gpointer        callback_data,
                          GError        **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_task != NULL, NULL);

    return (*interface->get_node_task) (provider, location, is_container,
            callback, callback_data, error);
}
*********/

FmNode **
fmprovider_get_content (FmProvider   *provider,
                        FmNode       *node,
                        GError      **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (IS_FMNODE (node), NULL);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_content != NULL, NULL);

    return (*interface->get_content) (provider, node, error);
}

/**********
FmTask *
fmprovider_get_content_task (FmProvider  *provider,
                             FmNode      *node,
                             GCallback    callback,
                             gpointer     callback_data,
                             GError     **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (IS_FMNODE (node), NULL);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_content_task != NULL, NULL);

    return (*interface->get_content_task) (provider, node,
            callback, callback_data, error);
}
*********/

FmNode **
fmprovider_get_children (FmProvider  *provider,
                         FmNode      *node,
                         GError     **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (IS_FMNODE (node), NULL);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_children != NULL, NULL);

    return (*interface->get_children) (provider, node, error);
}

/***********
FmTask *
fmprovider_get_children_task (FmProvider     *provider,
                              FmNode         *node,
                              GCallback       callback,
                              gpointer        callback_data,
                              GError        **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), NULL);
    g_return_val_if_fail (IS_FMNODE (node), NULL);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_children_task != NULL, NULL);

    return (*interface->get_children_task) (provider, node,
            callback, callback_data, error);
}
**********/

gboolean
fmprovider_remove_node (FmProvider   *provider,
                        FmNode       *node,
                        GError      **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), FALSE);
    g_return_val_if_fail (IS_FMNODE (node), FALSE);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->remove_node != NULL, FALSE);

    return (*interface->remove_node) (provider, node, error);
}

/************
FmTask *
fmprovider_remove_node_task (FmProvider  *provider,
                             FmNode      *node,
                             GCallback    callback,
                             gpointer     callback_data,
                             GError     **error)
{
    FmProviderInterface *interface;

    g_return_val_if_fail (IS_FMPROVIDER (provider), FALSE);
    g_return_val_if_fail (IS_FMNODE (node), FALSE);

    interface = FMPROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->remove_node_task != NULL, FALSE);

    return (*interface->remove_node_task) (provider, node,
            callback, callback_data, error);
}
*********/
