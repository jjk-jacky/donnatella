
#include <glib-object.h>
#include "provider.h"
#include "node.h"
#include "closures.h"

enum
{
    NEW_NODE,
    NODE_UPDATED,
    NODE_REMOVED,
    NODE_CHILDREN,
    NODE_NEW_CHILD,
    NB_SIGNALS
};

static guint donna_provider_signals[NB_SIGNALS] = { 0 };

static void
donna_provider_default_init (DonnaProviderInterface *klass)
{
    donna_provider_signals[NEW_NODE] =
        g_signal_new ("new-node",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, new_node),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_NODE);
    donna_provider_signals[NODE_UPDATED] =
        g_signal_new ("node-updated",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_updated),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__BOXED_STRING,
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            G_TYPE_STRING);
    donna_provider_signals[NODE_REMOVED] =
        g_signal_new ("node-removed",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_removed),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_NODE);
    donna_provider_signals[NODE_CHILDREN] =
        g_signal_new ("node-children",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_children),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__BOXED_UINT_BOXED,
            G_TYPE_NONE,
            3,
            DONNA_TYPE_NODE,
            G_TYPE_UINT,
            G_TYPE_PTR_ARRAY);
    donna_provider_signals[NODE_NEW_CHILD] =
        g_signal_new ("node-new-child",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_new_child),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__BOXED_BOXED,
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);
}

G_DEFINE_INTERFACE (DonnaProvider, donna_provider, G_TYPE_OBJECT)

/* signals */

void
donna_provider_new_node (DonnaProvider  *provider,
                         DonnaNode      *node)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    g_signal_emit (provider, donna_provider_signals[NEW_NODE], 0, node);
}

void
donna_provider_node_updated (DonnaProvider  *provider,
                             DonnaNode      *node,
                             const gchar    *name)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (name != NULL);

    g_signal_emit (provider, donna_provider_signals[NODE_UPDATED],
            g_quark_from_string (name),
            node, name);
}

void
donna_provider_node_removed (DonnaProvider  *provider,
                             DonnaNode      *node)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    g_signal_emit (provider, donna_provider_signals[NODE_REMOVED], 0, node);
}

void
donna_provider_node_children (DonnaProvider  *provider,
                              DonnaNode      *node,
                              DonnaNodeType   node_types,
                              GPtrArray      *children)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (children != NULL);

    g_signal_emit (provider, donna_provider_signals[NODE_CHILDREN], 0,
            node, node_types, children);
}

void
donna_provider_node_new_child (DonnaProvider  *provider,
                               DonnaNode      *node,
                               DonnaNode      *child)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (DONNA_IS_NODE (child));

    g_signal_emit (provider, donna_provider_signals[NODE_NEW_CHILD], 0,
            node, child);
}


/* API */

const gchar *
donna_provider_get_domain (DonnaProvider  *provider)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_domain != NULL, NULL);

    return (*interface->get_domain) (provider);
}

DonnaProviderFlags
donna_provider_get_flags (DonnaProvider *provider)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), DONNA_PROVIDER_FLAG_INVALID);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, DONNA_PROVIDER_FLAG_INVALID);
    g_return_val_if_fail (interface->get_flags != NULL, DONNA_PROVIDER_FLAG_INVALID);

    return (*interface->get_flags) (provider);
}

DonnaTask *
donna_provider_get_node_task (DonnaProvider    *provider,
                              const gchar      *location)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_task != NULL, NULL);

    return (*interface->get_node_task) (provider, location);
}

DonnaTask *
donna_provider_has_node_children_task (DonnaProvider  *provider,
                                       DonnaNode      *node,
                                       DonnaNodeType   node_types)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;
    DonnaNodeType  node_type;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider, and can have children */
    donna_node_get (node, FALSE,
            "provider",  &p,
            "node-type", &node_type,
            NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);
    g_return_val_if_fail (node_type == DONNA_NODE_CONTAINER, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->has_node_children_task != NULL, NULL);

    return (*interface->has_node_children_task) (provider, node, node_types);
}

DonnaTask *
donna_provider_get_node_children_task (DonnaProvider  *provider,
                                       DonnaNode      *node,
                                       DonnaNodeType   node_types)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;
    DonnaNodeType  node_type;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider, and can have children */
    donna_node_get (node, FALSE,
            "provider",  &p,
            "node-type", &node_type,
            NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);
    g_return_val_if_fail (node_type == DONNA_NODE_CONTAINER, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_children_task != NULL, NULL);

    return (*interface->get_node_children_task) (provider, node, node_types);
}

DonnaTask *
donna_provider_remove_node_task (DonnaProvider  *provider,
                                 DonnaNode      *node)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider */
    donna_node_get (node, FALSE, "provider", &p, NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->remove_node_task != NULL, NULL);

    return (*interface->remove_node_task) (provider, node);
}

DonnaTask *
donna_provider_get_node_parent_task (DonnaProvider  *provider,
                                     DonnaNode      *node)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider */
    donna_node_get (node, FALSE, "provider", &p, NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_parent_task != NULL, NULL);

    return (*interface->get_node_parent_task) (provider, node);
}
