
#include <glib-object.h>
#include "provider.h"
#include "node.h"

enum
{
    NODE_CREATED,
    NODE_REMOVED,
    NODE_UPDATED,
    NODE_CHILDREN,
    NODE_NEW_CHILD,
    NODE_NEW_CONTENT,
    NB_SIGNALS
};

static guint donna_provider_signals[NB_SIGNALS] = { 0 };

static void
donna_provider_default_init (DonnaProviderInterface *klass)
{
    g_object_interface_install_property (klass,
            g_param_spec_string (
                "domain",
                "domain",
                "Domain handled by the provider",
                NULL,
                G_PARAM_READABLE | G_PARAM_CONSTRUCT_ONLY));

    donna_provider_signals[NODE_CREATED] =
        g_signal_new ("node-created",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_created),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_NODE);
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
    donna_provider_signals[NODE_UPDATED] =
        g_signal_new ("node-updated",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_updated),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _STRING_BOXED */
            G_TYPE_NONE,
            3,
            DONNA_TYPE_NODE,
            G_TYPE_STRING,
            G_TYPE_BOXED);
    donna_provider_signals[NODE_CHILDREN] =
        g_signal_new ("node-children",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_children),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _BOXED_ARRAY */
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);
    donna_provider_signals[NODE_NEW_CHILD] =
        g_signal_new ("node-new-child",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_new_child),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _BOXED */
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);
    donna_provider_signals[NODE_NEW_CONTENT] =
        g_signal_new ("node-new-content",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_new_content),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__BOXED, /* FIXME: _BOXED */
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);
}

G_DEFINE_INTERFACE (DonnaProvider, donna_provider, G_TYPE_OBJECT)

/* signals */

void
donna_provider_node_created (DonnaProvider  *provider,
                             DonnaNode      *node)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    g_signal_emit (provider, donna_provider_signals[NODE_CREATED], 0, node);
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
donna_provider_node_updated (DonnaProvider  *provider,
                             DonnaNode      *node,
                             const gchar    *name,
                             const GValue   *value)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (name != NULL);

    g_signal_emit (provider, donna_provider_signals[NODE_UPDATED],
            g_quark_from_string (name),
            node, name, value);
}

void
donna_provider_node_children (DonnaProvider  *provider,
                              DonnaNode      *node,
                              DonnaNode     **children)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (DONNA_IS_NODE (*children));

    g_signal_emit (provider, donna_provider_signals[NODE_CREATED], 0,
            node, children);
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

void
donna_provider_node_new_content (DonnaProvider  *provider,
                                 DonnaNode      *node,
                                 DonnaNode      *content)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (DONNA_IS_NODE (content));

    g_signal_emit (provider, donna_provider_signals[NODE_NEW_CONTENT], 0,
            node, content);
}


/* API */

DonnaTask *
donna_provider_get_node (DonnaProvider    *provider,
                         const gchar      *location,
                         task_callback_fn callback,
                         gpointer         callback_data,
                         GDestroyNotify   callback_destroy,
                         guint            timeout,
                         task_timeout_fn  timeout_callback,
                         gpointer         timeout_data,
                         GDestroyNotify   timeout_destroy,
                         GError         **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node!= NULL, NULL);

    return (*interface->get_node) (provider, location,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy,
            error);
}

DonnaTask *
donna_provider_get_content (DonnaProvider    *provider,
                            DonnaNode        *node,
                            task_callback_fn  callback,
                            gpointer          callback_data,
                            GDestroyNotify    callback_destroy,
                            guint             timeout,
                            task_timeout_fn   timeout_callback,
                            gpointer          timeout_data,
                            GDestroyNotify    timeout_destroy,
                            GError          **error)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider */
    donna_node_get (node, "provider", &p, NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_content != NULL, NULL);

    return (*interface->get_content) (provider, node,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy,
            error);
}

DonnaTask *
donna_provider_get_children (DonnaProvider   *provider,
                             DonnaNode       *node,
                             task_callback_fn callback,
                             gpointer         callback_data,
                             GDestroyNotify   callback_destroy,
                             guint            timeout,
                             task_timeout_fn  timeout_callback,
                             gpointer         timeout_data,
                             GDestroyNotify   timeout_destroy,
                             GError         **error)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider */
    donna_node_get (node, "provider", &p, NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_children != NULL, NULL);

    return (*interface->get_children) (provider, node,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy,
            error);
}

DonnaTask *
donna_provider_remove_node (DonnaProvider   *provider,
                            DonnaNode       *node,
                            task_callback_fn callback,
                            gpointer         callback_data,
                            GDestroyNotify   callback_destroy,
                            guint            timeout,
                            task_timeout_fn  timeout_callback,
                            gpointer         timeout_data,
                            GDestroyNotify   timeout_destroy,
                            GError         **error)
{
    DonnaProviderInterface *interface;
    DonnaProvider *p;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    /* make sure the provider is the node's provider */
    donna_node_get (node, "provider", &p, NULL);
    g_object_unref (p);
    g_return_val_if_fail (p == provider, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->remove_node != NULL, FALSE);

    return (*interface->remove_node) (provider, node,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy,
            error);
}
