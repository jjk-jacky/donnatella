/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <glib-object.h>
#include "provider.h"
#include "node.h"
#include "app.h"
#include "util.h"
#include "closures.h"
#include "macros.h"

/**
 * SECTION:provider
 * @Short_description: Provider of a domain, handling its nodes
 * @See_also: #DonnaNode, #DonnaTask, #DonnaProviderBase
 *
 * In donnatella exists a notion of domain, each domain being handled by its
 * #DonnaProvider. A domain is an abstraction level, allowing interactions -
 * such as #DonnaTreeView browsing - without the need to know about the
 * inner-workings of said domain.
 *
 * The most common/obvious domain is "fs" which represents the filesystem.
 * Others can include virtual ones such as "config" for the current
 * configuration, or interfaces to features, e.g. "register" for registers.
 *
 * A provider will create the #DonnaNode<!-- -->s representing containers/items
 * of the domain, handling refreshing/setting its properties, as well as tasks
 * for IO operations.
 *
 * It is also where signals will be emitted, so it is only needed to connect one
 * single handler (on the provider) for all nodes of the domain.
 */

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

G_DEFINE_INTERFACE (DonnaProvider, donna_provider, G_TYPE_OBJECT)

static void
donna_provider_default_init (DonnaProviderInterface *interface)
{
    /**
     * DonnaProvider::new-node:
     * @provider: the #DonnaProvider of @node
     * @node: The #DonnaNode just created
     *
     * This signal is emitted when a new node is created by the provider, to
     * allow adding new properties to the node.
     *
     * It doesn't mean the item/container represented by the node was just
     * created, this is handled via #DonnaProvider::node-new-child
     */
    donna_provider_signals[NEW_NODE] =
        g_signal_new ("new-node",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, new_node),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_NODE);
    /**
     * DonnaProvider::node-updated:
     * @provider: the #DonnaProvider of @node
     * @node: The #DonnaNode on which property was updated
     * @property: The name of the property that was updated
     *
     * This is the equivalent of the #GObject::notify signal on #GObject and
     * similarily, it has a details set to the property's name; So one can
     * connect to "node-updated::name" to only have the handler called when
     * property "name" was updated.
     */
    donna_provider_signals[NODE_UPDATED] =
        g_signal_new ("node-updated",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_updated),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__OBJECT_STRING,
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            G_TYPE_STRING);
    /**
     * DonnaProvider::node-deleted:
     * @provider: the #DonnaProvider of @node
     * @node: The #DonnaNode that was just deleted
     *
     * The object behind @node was deleted, which also means that any & all
     * references taken on @node must be released. Much like #GtkWidget::destroy
     * for #GtkWidget, after the signal emission @node should be finalized.
     *
     * If it wasn't the case, the node will be "moved" to provider "invalid"
     * and trying to use it would only fail, since it represents a non-existing
     * item/container.
     */
    donna_provider_signals[NODE_DELETED] =
        g_signal_new ("node-deleted",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_deleted),
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            DONNA_TYPE_NODE);
    /**
     * DonnaProvider::node-children:
     * @provider: the #DonnaProvider of @node
     * @node: The #DonnaNode for which children were just listed
     * @node_types: The #DonnaNodeType<!-- -->s of the children listed
     * @nodes: (element-type DonnaNode): A #GPtrArray of #DonnaNode
     *
     * When children of a node are listed, this signal is emitted. It is
     * important to note that this doesn't mean all children are listed in
     * @nodes, but only those of @node_types
     */
    donna_provider_signals[NODE_CHILDREN] =
        g_signal_new ("node-children",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_children),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__OBJECT_UINT_OBJECT,
            G_TYPE_NONE,
            3,
            DONNA_TYPE_NODE,
            G_TYPE_UINT,
            G_TYPE_PTR_ARRAY);
    /**
     * DonnaProvider::node-new-child:
     * @provider: the #DonnaProvider of @node
     * @node: The #DonnaNode for which a new child was just created
     * @child: The newly-created child of @node
     *
     * This signal is emitted when a new child was created in @node. This could
     * be the result of an internal creation, or the provider was made aware of
     * it either as result of a task to get children, or via some auto-refresh
     * mechanism.
     */
    donna_provider_signals[NODE_NEW_CHILD] =
        g_signal_new ("node-new-child",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_new_child),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__OBJECT_OBJECT,
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);
    /**
     * DonnaProvider::node-removed-from:
     * @provider: the #DonnaProvider of @parent
     * @node: The #DonnaNode that was removed/deleted
     * @parent: The #DonnaNode from where @node was removed
     *
     * This signal is emitted when @node is removed from @parent, but @node was
     * not deleted. This applies to non-flat domains, e.g. when a node is
     * removed from a register.
     */
    donna_provider_signals[NODE_REMOVED_FROM] =
        g_signal_new ("node-removed-from",
            DONNA_TYPE_PROVIDER,
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET (DonnaProviderInterface, node_removed_from),
            NULL,
            NULL,
            g_cclosure_user_marshal_VOID__OBJECT_OBJECT,
            G_TYPE_NONE,
            2,
            DONNA_TYPE_NODE,
            DONNA_TYPE_NODE);

    /**
     * DonnaProvider:app:
     *
     * The #DonnaApp object
     */
    g_object_interface_install_property (interface,
            g_param_spec_object ("app", "app", "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

/* signals */

/**
 * donna_provider_new_node:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode just created
 *
 * Emits signal #DonnaProvider::new-node on @provider
 */
void
donna_provider_new_node (DonnaProvider  *provider,
                         DonnaNode      *node)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    g_signal_emit (provider, donna_provider_signals[NEW_NODE], 0, node);
}

/**
 * donna_provider_node_updated:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode on which property @name was updated
 * @name: The name of the updated property
 *
 * Emits signal #DonnaProvider::node-updated on @provider
 */
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

static gboolean
post_node_deleted (DonnaNode *node)
{
    DonnaProvider *provider = donna_node_peek_provider (node);
    DonnaProviderInterface *interface;
    DonnaApp *app;

    /* ideally, after the signal all references to the node were removed (maybe
     * in the thread UI, that's why we used an idle source, to let e.g.
     * treeviews remove their references to the node as well), so there should
     * only be 2 left: us, and the provider. */
    if (((GObject *) node)->ref_count <= 2)
        goto done;

    /* in case the node has already been marked invalid. This can happen when
     * e.g. a treeview triggered a refresh of all properties on a node, which
     * didn't exist anymore. Then all refresh attempts (one for each property)
     * will result in emitting a node-deleted */
    if (streq (donna_node_get_domain (node), "invalid"))
        goto done;

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);
    if (G_UNLIKELY (!interface || !interface->unref_node))
        g_warning ("Provider '%s': post_node_deleted(): unref_node not implemented",
                donna_provider_get_domain (provider));
    else
        (*interface->unref_node) (provider, node);

    /* Since there are still references to the node, we're gonna mark it invalid
     * and ask the provider to let it go */
    g_object_get (provider, "app", &app, NULL),
    donna_node_mark_invalid (node, donna_app_get_provider (app, "invalid"));
    g_object_unref (app);

done:
    g_object_unref (node);
    return FALSE;
}

/**
 * donna_provider_node_deleted:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode that was deleted
 *
 * Emits the signal #DonnaProvider::node-deleted on @provider
 *
 * This will also ensures that there are no more references on @node after the
 * emission, so that @node can be finalized as expected. If that isn't the case,
 * and to make sure @provider doesn't keep @node around, it will automatically
 * be "transfered" to provider "invalid" by calling donna_node_mark_invalid() on
 * @node after the provider's implementation of unref_node<!-- -->() was called.
 *
 * This allows providers not have to worry about it, even when they have a
 * toggle_ref on @node as well as keeping it in their internal hashmap.
 */
void
donna_provider_node_deleted (DonnaProvider  *provider,
                             DonnaNode      *node)
{
    g_return_if_fail (DONNA_IS_PROVIDER (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    g_signal_emit (provider, donna_provider_signals[NODE_DELETED], 0, node);
    g_idle_add ((GSourceFunc) post_node_deleted, g_object_ref (node));
}

/**
 * donna_provider_node_children:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode to which @children belong
 * @node_types: The #DonnaNodeType<!-- -->s of children in @children
 * @children: (element-type DonnaNode): A #GPtrArray of all children of @node
 * of type @node_types only
 *
 * Emits the signal #DonnaProvider::node-children on @provider
 */
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

/**
 * donna_provider_node_new_child:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode parent of @child
 * @child: The newly-created child of @node
 *
 * Emits signal #DonnaProvider::node-new-child on @provider
 */
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

/**
 * donna_provider_node_removed_from:
 * @provider: The #DonnaProvider of @source
 * @node: The #DonnaNode that was removed from @source
 * @source: The #DonnaNode from where @node was removed
 *
 * Emits signal #DonnaProvider::node-removed-from on @provider
 */
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

/**
 * donna_provider_get_domain:
 * @provider: A #DonnaProvider
 *
 * Return the domain of @provider
 */
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

/**
 * donna_provider_get_flags:
 * @provider: A #DonnaProvider
 *
 * Return the #DonnaProviderFlags for @provider
 */
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

/**
 * donna_provider_get_node:
 * @provider: A #DonnaProvider
 * @location: The location of the #DonnaNode wanted
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * This will return the #DonnaNode for @location. If the node already existed or
 * the provider could create it right away, the node is returned right away.
 *
 * However, if the node didn't yet exist and the provider might block (e.g. for
 * "fs" where filesystem calls (i.e. IO operations) are required), then:
 * - if in the main/UI thread, a #DonnaTask will be run while a new #GMainLoop
 *   is created while waiting for it
 * - else, the #DonnaTask is simply run blockingly
 *
 * This is important to remember when calling from the main/UI thread, since a
 * new main loop might have run for a little bit, and events/signals might have
 * been processed as a result.
 *
 * Note that you can also use helper donna_app_get_node() if you don't have the
 * #DonnaProvider but a full-location instead; or want to have a user-provided
 * full location go through "user-parsing" (prefixes, aliases, etc)
 *
 * Returns: The #DonnaNode for @location, or %NULL
 */
DonnaNode *
donna_provider_get_node (DonnaProvider    *provider,
                         const gchar      *location,
                         GError          **error)
{
    DonnaProviderInterface *interface;
    DonnaApp *app;
    DonnaTask *task;
    DonnaTaskState state;
    gboolean is_node;
    gpointer ret;

    g_return_val_if_fail (DONNA_IS_PROVIDER (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    interface = DONNA_PROVIDER_GET_INTERFACE (provider);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_node != NULL, NULL);

    if (!(*interface->get_node) (provider, location, &is_node, &ret, error))
        return NULL;

    if (is_node)
        return (DonnaNode *) ret;

    /* the provider gave us a task, which means it could take a little while to
     * get the actual node (e.g. requires access to (potentially slow)
     * filesystem), in which case we'll determine whether we're in the UI thead
     * or not.
     * If so, we start a new main loop while waiting for the task.
     * If not, we can just block the thread.
     * */

    g_object_get (provider, "app", &app, NULL);
    task = (DonnaTask *) g_object_ref_sink (ret);

    if (g_main_context_is_owner (g_main_context_default ()))
    {
        GMainLoop *loop;
        gint fd;

        fd = donna_task_get_wait_fd (task);
        if (G_UNLIKELY (fd < 0))
        {
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider '%s': Failed to get wait_fd from get_node task for location '%s'",
                    donna_provider_get_domain (provider), location);
            g_object_unref (app);
            g_object_unref (task);
            return NULL;
        }

        loop = g_main_loop_new (NULL, TRUE);
        donna_fd_add_source (fd, (GSourceFunc) donna_main_loop_quit_return_false, loop, NULL);
        donna_app_run_task (app, task);
        g_main_loop_run (loop);
    }
    else
    {
        donna_app_run_task (app, task);
        if (G_UNLIKELY (!donna_task_wait_for_it (task, NULL, error)))
        {
            g_object_unref (app);
            g_object_unref (task);
            return NULL;
        }
    }
    g_object_unref (app);

    state = donna_task_get_state (task);
    if (state != DONNA_TASK_DONE)
    {
        if (state == DONNA_TASK_CANCELLED)
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider '%s': Task get_node for '%s' cancelled",
                    donna_provider_get_domain (provider), location);
        else if (error)
        {
            const GError *err;

            err = donna_task_get_error (task);
            if (err)
            {
                *error = g_error_copy (err);
                g_prefix_error (error, "Provider '%s': Task get_node for '%s' failed: ",
                        donna_provider_get_domain (provider), location);
            }
            else
                g_set_error (error, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider '%s': Task get_node for '%s' failed without error message",
                        donna_provider_get_domain (provider), location);
        }

        g_object_unref (task);
        return NULL;
    }

    ret = g_value_dup_object (donna_task_get_return_value (task));
    g_object_unref (task);
    return (DonnaNode *) ret;
}

/**
 * donna_provider_has_node_children_task:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode to get children from
 * @node_types: The #DonnaNodeType<!-- -->s of children to get
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Creates a task that will check if @node has at least one child (of type
 * @node_types), and put a #gboolean as #DonnaTask:return-value
 *
 * Note that you can also use helper donna_node_has_children_task() if you have
 * @node but not its provider.
 *
 * Returns: (transfer floating): A #DonnaTask to get the children
 */
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

/**
 * donna_provider_get_node_children_task:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode to get children from
 * @node_types: The #DonnaNodeType<!-- -->s of children to get
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Creates a task that will get the children (of type @node_types only) and
 * put them in a #GPtrArray as #DonnaTask:return-value
 *
 * Note that you can also use helper donna_node_get_children_task() if you have
 * @node but not its provider.
 *
 * Returns: (transfer floating): A #DonnaTask to get the children
 */
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

/**
 * donna_provider_trigger_node_task:
 * @provider: The #DonnaProvider of @node
 * @node: The #DonnaNode to trigger (must be a %DONNA_NODE_ITEM)
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Create a task that will trigger @node -- what this actually means depends not
 * only on the node, but also the domain; E.g. in "fs" this will mean
 * execute/open the file.
 *
 * Note that you can also use helper donna_node_trigger_task() if you have
 * @node but not its provider.
 *
 * Returns: (transfer floating): A #DonnaTask to trigger @node
 */
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

/**
 * donna_provider_io_task:
 * @provider: A #DonnaProvider
 * @type: The type of IO operation to perform
 * @is_source: Whether @provider is provider of @sources, or @dest
 * @sources: (element-type DonnaNode): Array of #DonnaNode<!-- -->s to perform
 * the operation on
 * @dest: (allow-none): Destination of the operation
 * @new_name: (allow-none): New name to use in the operation
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Create a task to perform the specified operation. For %DONNA_IO_COPY and
 * %DONNA_IO_MOVE operations, @dest must be specified. For %DONNA_IO_DELETE
 * simply pass %NULL.
 *
 * @provider must be the provider of all nodes in @sources if @is_source is
 * %TRUE, else it must be provider of @dest.
 *
 * If specified, @new_name will be used if there's only one node in @sources,
 * as new name for the node copied/moved.
 *
 * For %DONNA_IO_COPY the newly created/copied nodes will be put in a #GPtrArray
 * as #DonnaTask:return-value.
 * For %DONNA_IO_MOVE the moved nodes will be put in a #GPtrArray as
 * #DonnaTask:return-value.
 * For %DONNA_IO_DELETE there are no return value set.
 *
 * Returns: (transfer floating): A #DonnaTask to perform the IO operation, or
 * %NULL
 */
DonnaTask *
donna_provider_io_task (DonnaProvider  *provider,
                        DonnaIoType     type,
                        gboolean        is_source,
                        GPtrArray      *sources,
                        DonnaNode      *dest,
                        const gchar    *new_name,
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

    return (*interface->io_task) (provider, type, is_source, sources,
            dest, new_name, error);
}

/**
 * donna_provider_new_child_task:
 * @provider: The #DonnaProvider of @parent
 * @parent: The #DonnaNode to create a new child in
 * @type: The type of node to create
 * @name: The name of the child to create
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Create a task to create a new child of type @type inside @parent (which must,
 * obviously, be a %DONNA_NODE_CONTAINER) by the name of @name.
 *
 * The newly-created node will be set as #DonnaTask:return-value of the task.
 *
 * Note that you can also use helper donna_node_new_child_task() if you have
 * @node but not its provider.
 *
 * Returns: (transfer floating): A #DonnaTask to create the child
 */
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

/**
 * donna_provider_remove_from_task:
 * @provider: The #DonnaProvider of @source
 * @nodes: (element-type DonnaNode): An array of #DonnaNode<!-- -->s
 * @source: The #DonnaNode to remove @nodes from
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Create a task to remove @nodes from @source. This is useful to remove nodes
 * from a node which isn't their parent, e.g. removing nodes from a register.
 *
 * This will automatically be converted into a %DONNA_IO_DELETE task if all
 * @nodes are children of @source.
 *
 * Returns: (transfer floating): A #DonnaTask to remove @nodes from @source
 */
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
            if (!streqn (location, s, len) || (len > 1 && s[len] != '/')
                    || strchr (s + len + 1, '/') != NULL)
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
                nodes, NULL, NULL, error);
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

/**
 * donna_provider_get_context_alias:
 * @provider: A #DonnaProvider
 * @alias: The alias to resolve
 * @extra: The extra for the alias
 * @reference: The contextual reference
 * @prefix: The prefix to use when resolving @alias
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Providers can provider alias and/or items to be used in context menus. This
 * will resolve @alias, prefixing all items with @prefix so they can be used
 * properly.
 *
 * Returns: Resolved @alias, or %NULL
 */
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

/**
 * donna_provider_get_context_item_info:
 * @provider: A #DonnaProvider
 * @item: The item to get info about
 * @extra: The extra for the item
 * @reference: The contextual reference
 * @node_ref: (allow-none): The #DonnaNode as reference, or %NULL
 * @get_sel: Function to get the selection
 * @get_sel_data: Data to provider @get_sel
 * @info: Location where to store info about @item
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Providers can provider alias and/or items to be used in context menus. This
 * will set info about @item into @info.
 *
 * Returns: %TRUE is info about @item were set in @info, else %FALSE
 */
gboolean
donna_provider_get_context_item_info (DonnaProvider             *provider,
                                      const gchar               *item,
                                      const gchar               *extra,
                                      DonnaContextReference      reference,
                                      DonnaNode                 *node_ref,
                                      get_sel_fn                 get_sel,
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

/**
 * donna_provider_get_context_alias_new_nodes:
 * @provider: A #DonnaProvider
 * @extra: The extra for the alias
 * @location: The #DonnaNode being the current location
 * @prefix: The prefix to use when resolving alias
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Providers can provider alias and/or items to be used in context menus. This
 * should resolve a generic alias from #DonnaTreeView which is meant to provide
 * items to create new nodes within @location
 *
 * If not implemented, an empty string will be returned.
 *
 * Returns: Resolved alias (empty string for nothing)
 */
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
        return (gchar *) "";

    return (*interface->get_context_alias_new_nodes) (provider, extra, location,
            prefix, error);
}
