
#include <glib-object.h>
#include "provider.h"
#include "node.h"
#include "closures.h"
#include "macros.h"

enum
{
    NEW_NODE,
    NODE_UPDATED,
    NODE_DELETED,
    NODE_CHILDREN,
    NODE_NEW_CHILD,
    NODE_REMOVED_FROM,
    NB_SIGNALS
};

static guint donna_provider_signals[NB_SIGNALS] = { 0 };

static void
donna_provider_default_init (DonnaProviderInterface *interface)
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
    donna_provider_signals[NODE_DELETED] =
        g_signal_new ("node-deleted",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_deleted),
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
    donna_provider_signals[NODE_REMOVED_FROM] =
        g_signal_new ("node-removed-from",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_removed_from),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__BOXED_BOXED,
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);

    g_object_interface_install_property (interface,
            g_param_spec_object ("app", "app", "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
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
donna_provider_node_deleted (DonnaProvider  *provider,
                             DonnaNode      *node)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    g_signal_emit (provider, donna_provider_signals[NODE_DELETED], 0, node);
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

void
donna_provider_node_removed_from (DonnaProvider  *provider,
                                  DonnaNode      *node,
                                  DonnaNode      *source)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (DONNA_IS_NODE (source));

    g_signal_emit (provider, donna_provider_signals[NODE_REMOVED_FROM], 0,
            node, source);
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
                              const gchar      *location,
                              GError          **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_task != NULL, NULL);

    return (*interface->get_node_task) (provider, location, error);
}

DonnaTask *
donna_provider_has_node_children_task (DonnaProvider  *provider,
                                       DonnaNode      *node,
                                       DonnaNodeType   node_types,
                                       GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider, and can have children */
    g_return_val_if_fail (donna_node_peek_provider (node) == provider, NULL);
    g_return_val_if_fail (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->has_node_children_task != NULL, NULL);

    return (*interface->has_node_children_task) (provider, node, node_types, error);
}

DonnaTask *
donna_provider_get_node_children_task (DonnaProvider  *provider,
                                       DonnaNode      *node,
                                       DonnaNodeType   node_types,
                                       GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider, and can have children */
    g_return_val_if_fail (donna_node_peek_provider (node) == provider, NULL);
    g_return_val_if_fail (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_children_task != NULL, NULL);

    return (*interface->get_node_children_task) (provider, node, node_types, error);
}

DonnaTask *
donna_provider_get_node_parent_task (DonnaProvider  *provider,
                                     DonnaNode      *node,
                                     GError        **error)
{
    DonnaProviderInterface *interface;
    DonnaProviderFlags flags;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider */
    g_return_val_if_fail (donna_node_peek_provider (node) == provider, NULL);

    flags = donna_provider_get_flags (provider);
    if (flags & DONNA_PROVIDER_FLAG_INVALID || flags & DONNA_PROVIDER_FLAG_FLAT)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR, DONNA_PROVIDER_ERROR_INVALID_CALL,
                "Provider '%s' is flat: impossible to get a node's parent",
                donna_provider_get_domain (provider));
        return NULL;
    }

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node_parent_task != NULL, NULL);

    return (*interface->get_node_parent_task) (provider, node, error);
}

DonnaTask *
donna_provider_trigger_node_task (DonnaProvider  *provider,
                                  DonnaNode      *node,
                                  GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    /* make sure the provider is the node's provider */
    g_return_val_if_fail (donna_node_peek_provider (node) == provider, NULL);

    /* only works on ITEM */
    if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_WRONG_NODE_TYPE,
                "Provider '%s': trigger_node() is only supported on ITEM, not CONTAINER",
                donna_provider_get_domain (provider));
        return NULL;
    }

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->trigger_node_task != NULL, NULL);

    return (*interface->trigger_node_task) (provider, node, error);
}

DonnaTask *
donna_provider_io_task (DonnaProvider  *provider,
                        DonnaIoType     type,
                        gboolean        is_source,
                        GPtrArray      *sources,
                        DonnaNode      *dest,
                        GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (type == DONNA_IO_COPY || type == DONNA_IO_MOVE
            || type == DONNA_IO_DELETE, NULL);
    g_return_val_if_fail (sources != NULL, NULL);
    if (type == DONNA_IO_DELETE)
        g_return_val_if_fail (is_source == TRUE, NULL);
    else
        g_return_val_if_fail (DONNA_IS_NODE (dest), NULL);

    if (is_source)
        /* FIXME should we check all sources are within the same provider? */
        g_return_val_if_fail (donna_node_peek_provider (sources->pdata[0]) == provider, NULL);
    else if (type != DONNA_IO_DELETE)
        g_return_val_if_fail (donna_node_peek_provider (dest) == provider, NULL);

    if (G_UNLIKELY (sources->len <= 0))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOTHING_TO_DO,
                "Provider '%s': Cannot perform IO operation, no nodes given",
                donna_provider_get_domain (provider));
        return NULL;
    }

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->io_task == NULL)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': No support of IO operations",
                donna_provider_get_domain (provider));
        return NULL;
    }

    return (*interface->io_task) (provider, type, is_source, sources, dest, error);
}

DonnaTask *
donna_provider_new_child_task (DonnaProvider  *provider,
                               DonnaNode      *parent,
                               DonnaNodeType   type,
                               const gchar    *name,
                               GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (parent), NULL);
    g_return_val_if_fail (donna_node_peek_provider (parent) == provider, NULL);
    g_return_val_if_fail (type == DONNA_NODE_CONTAINER
            || type == DONNA_NODE_ITEM, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->new_child_task == NULL)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': No support of node creation",
                donna_provider_get_domain (provider));
        return NULL;
    }

    return (*interface->new_child_task) (provider, parent, type, name, error);
}

DonnaTask *
donna_provider_remove_from_task (DonnaProvider  *provider,
                                 GPtrArray      *nodes,
                                 DonnaNode      *source,
                                 GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (nodes != NULL, NULL);
    g_return_val_if_fail (DONNA_IS_NODE (source), NULL);
    g_return_val_if_fail (donna_node_get_node_type (source) == DONNA_NODE_CONTAINER, NULL);
    g_return_val_if_fail (donna_node_peek_provider (source) == provider, NULL);

    if (G_UNLIKELY (nodes->len <= 0))
    {
        gchar *fl = donna_node_get_full_location (source);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOTHING_TO_DO,
                "Provider '%s': Cannot remove nodes from '%s', no nodes given",
                donna_provider_get_domain (provider), fl);
        g_free (fl);
        return NULL;
    }

    if (!(donna_provider_get_flags (provider) & DONNA_PROVIDER_FLAG_FLAT))
    {
        gchar *location = donna_node_get_location (source);
        gsize len = strlen (location);
        guint i;
        gboolean can_convert = TRUE;

        for (i = 0; i < nodes->len; ++i)
        {
            DonnaNode *node = nodes->pdata[i];
            gchar *s;

            if (donna_node_peek_provider (node) != provider)
            {
                can_convert = FALSE;
                break;
            }

            s = donna_node_get_location (node);
            if (!streqn (location, s, len) || (len > 1 && s[len] != '/'))
            {
                can_convert = FALSE;
                g_free (s);
                break;
            }

            g_free (s);
        }
        g_free (location);

        if (!can_convert)
        {
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                    "Provider '%s': Provider isn't flat, cannot remove nodes. "
                    "You might wanna use an IO_DELETE operation instead.",
                    donna_provider_get_domain (provider));
            return NULL;
        }

        return donna_provider_io_task (provider, DONNA_IO_DELETE, TRUE,
                nodes, NULL, error);
    }

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->remove_from_task == NULL)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': No support of node removal",
                donna_provider_get_domain (provider));
        return NULL;
    }

    return (*interface->remove_from_task) (provider, nodes, source, error);
}

gchar *
donna_provider_get_context_alias (DonnaProvider         *provider,
                                  const gchar           *alias,
                                  const gchar           *extra,
                                  DonnaContextReference  reference,
                                  const gchar           *prefix,
                                  GError               **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (alias != NULL, NULL);
    g_return_val_if_fail (prefix != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->get_context_alias == NULL)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "Provider '%s': No context alias supported",
                donna_provider_get_domain (provider));
        return NULL;
    }

    return (*interface->get_context_alias) (provider, alias, extra, reference,
            prefix, error);
}

gchar *
donna_provider_get_context_alias_new_nodes (DonnaProvider  *provider,
                                            const gchar    *extra,
                                            DonnaNode      *location,
                                            const gchar    *prefix,
                                            GError        **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (location), NULL);
    g_return_val_if_fail (donna_node_peek_provider (location) == provider, NULL);
    g_return_val_if_fail (prefix != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->get_context_alias_new_nodes == NULL)
        /* if not implemented we just don't have anything, but the alias must
         * always exist/be valid */
        return "";

    return (*interface->get_context_alias_new_nodes) (provider, extra, location,
            prefix, error);
}

gboolean
donna_provider_get_context_item_info (DonnaProvider             *provider,
                                      const gchar               *item,
                                      const gchar               *extra,
                                      DonnaContextReference      reference,
                                      DonnaNode                 *node_ref,
                                      tree_context_get_sel_fn    get_sel,
                                      gpointer                   get_sel_data,
                                      DonnaContextInfo          *info,
                                      GError                   **error)
{
    DonnaProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), FALSE);
    g_return_val_if_fail (item != NULL, FALSE);
    g_return_val_if_fail (info != NULL, FALSE);
    g_return_val_if_fail (node_ref == NULL || DONNA_IS_NODE (node_ref), FALSE);
    g_return_val_if_fail (get_sel != NULL, FALSE);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, FALSE);

    if (interface->get_context_item_info == NULL)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "Provider '%s': No context item supported",
                donna_provider_get_domain (provider));
        return FALSE;
    }

    return (*interface->get_context_item_info) (provider, item, extra,
            reference, node_ref, get_sel, get_sel_data, info, error);
}
