/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider.h
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

#ifndef __DONNA_PROVIDER_H__
#define __DONNA_PROVIDER_H__

#include "common.h"
#include "node.h"
#include "task.h"
#include "contextmenu.h"

G_BEGIN_DECLS

#define DONNA_PROVIDER_ERROR        g_quark_from_static_string ("DonnaProvider-Error")
/**
 * DonnaProviderError:
 * @DONNA_PROVIDER_ERROR_WRONG_PROVIDER: Wrong provider for the node
 * @DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND: Location doesn't exist in domain
 * @DONNA_PROVIDER_ERROR_WRONG_NODE_TYPE: Invalid node-type; e.g. tried to get
 * children of a %DONNA_NODE_ITEM
 * @DONNA_PROVIDER_ERROR_INVALID_CALL: Invalid call. E.g. tried to trigger a
 * node that doesn't support triggering. This isn't meant for e.g. not fincing
 * the application to open a file with, but e.g. trying to trigger register:/
 * @DONNA_PROVIDER_ERROR_INVALID_NAME: Tried to set an invalid name to a node
 * @DONNA_PROVIDER_ERROR_INVALID_VALUE: Invalid value used
 * @DONNA_PROVIDER_ERROR_NOT_SUPPORTED: Operation not supported
 * @DONNA_PROVIDER_ERROR_ALREADY_EXIST: Something already exists
 * @DONNA_PROVIDER_ERROR_NOTHING_TO_DO: Nothing to do
 * @DONNA_PROVIDER_ERROR_OTHER: Other error
 *
 * FIXME: this is a mess.
 */
typedef enum
{
    DONNA_PROVIDER_ERROR_WRONG_PROVIDER,
    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
    DONNA_PROVIDER_ERROR_WRONG_NODE_TYPE,
    DONNA_PROVIDER_ERROR_INVALID_CALL,
    DONNA_PROVIDER_ERROR_INVALID_NAME,
    DONNA_PROVIDER_ERROR_INVALID_VALUE,
    DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
    DONNA_PROVIDER_ERROR_ALREADY_EXIST,
    DONNA_PROVIDER_ERROR_NOTHING_TO_DO,
    DONNA_PROVIDER_ERROR_OTHER,
} DonnaProviderError;

/**
 * DonnaProviderFlags:
 * @DONNA_PROVIDER_FLAG_FLAT: Domain is flat. Non-flat domain have a root under
 * location "/" and every children of a node has said node for parent, therefore
 * all children belongs to the same domain.
 * Flat domains on the other hand do not necesarilly have a root node, children
 * of a node might not have said node as parent, even not have a parent at all.
 * It is also possible for children to be from another domain. A simple example
 * of this is the domain "exec" where a node might represent an operation to
 * search for files; Children being the files found, which belong to "fs"
 */
typedef enum
{
    /*< private >*/
    DONNA_PROVIDER_FLAG_INVALID = (1 << 0),
    /*< public >*/
    DONNA_PROVIDER_FLAG_FLAT    = (1 << 1),
} DonnaProviderFlags;

/**
 * DonnaProviderInterface:
 * @parent: Parent type
 * @new_node: Signal #DonnaProvider::new-node
 * @node_updated: Signal #DonnaProvider::node-updated
 * @node_deleted: Signal #DonnaProvider::node-deleted
 * @node_children: Signal #DonnaProvider::node-children
 * @node_new_child: Signal #DonnaProvider::node-new-child
 * @node_removed_from: Signal #DonnaProvider::node-removed-from
 * @get_domain: Return the domain of the provider
 * @get_flags: Return the %DonnaProviderFlags for the provider
 * @get_node: When a node is asked. Either the node already exist or can be
 * created right away without any risk of blocking, then @is_node should be set
 * to %TRUE and @ret to the #DonnaNode. Else, @is_node must be set to %FALSE and
 * @ret to a #DonnaTask that will have the #DonnaNode set as
 * #DonnaTask:return-value See donna_provider_get_node()
 * @unref_node: Called when @node should be unreferenced (and removed from any
 * internal hashmap). This happens if after a #DonnaProvider::node-deleted not
 * all references were removed, so provider can let go before the node is marked
 * invalid (See donna_node_mark_invalid()).
 * @has_node_children_task: Return a task to determine whether or not @node
 * has children of type @node_types. See donna_provider_has_node_children_task()
 * @get_node_children_task: Return a task to get @node's children of type
 * @node_types. See donna_provider_get_node_children_task()
 * @trigger_node_task: Return a task to trigger @node. See
 * donna_provider_trigger_node_task()
 * @io_task: Return a task to perform the specified IO operation. See
 * donna_provider_io_task()
 * @new_child_task: Return a task to create a new child on @node. See
 * donna_provider_new_child_task() Lack of implementation will simply cause a
 * %DONNA_PROVIDER_ERROR_NOT_SUPPORTED to be set.
 * @remove_from_task: Return a task to remove @nodes from @source. See
 * donna_provider_remove_from_task() Lack of implementation will simply cause a
 * %DONNA_PROVIDER_ERROR_NOT_SUPPORTED to be set.
 * @get_context_alias: Resolve a contextual alias. See
 * donna_provider_get_context_alias() Lack of implementation will simply cause a
 * %DONNA_PROVIDER_ERROR_NOT_SUPPORTED to be set.
 * @get_context_item_info: Set information about a contextual item. See
 * donna_provider_get_context_item_info() Lack of implementation will simply
 * cause a %DONNA_PROVIDER_ERROR_NOT_SUPPORTED to be set.
 * @get_context_alias_new_nodes: Resolves special alias for node creation. See
 * donna_provider_get_context_alias_new_nodes() Lack of implementation will
 * simply return an empty string (i.e. doesn't resolve to anything, but no
 * failure)
 */
struct _DonnaProviderInterface
{
    GTypeInterface parent;

    /* signals */
    void                (*new_node)                 (DonnaProvider  *provider,
                                                     DonnaNode      *node);
    void                (*node_updated)             (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     const gchar    *name);
    void                (*node_deleted)             (DonnaProvider  *provider,
                                                     DonnaNode      *node);
    void                (*node_children)            (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GPtrArray      *children);
    void                (*node_new_child)           (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *child);
    void                (*node_removed_from)        (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *source);

    /* virtual table */
    const gchar *       (*get_domain)               (DonnaProvider  *provider);
    DonnaProviderFlags  (*get_flags)                (DonnaProvider  *provider);
    gboolean            (*get_node)                 (DonnaProvider  *provider,
                                                     const gchar    *location,
                                                     gboolean       *is_node,
                                                     gpointer       *ret,
                                                     GError        **error);
    void                (*unref_node)               (DonnaProvider  *provider,
                                                     DonnaNode      *node);
    DonnaTask *         (*has_node_children_task)   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GError        **error);
    DonnaTask *         (*get_node_children_task)   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GError        **error);
    DonnaTask *         (*trigger_node_task)        (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
    DonnaTask *         (*io_task)                  (DonnaProvider  *provider,
                                                     DonnaIoType     type,
                                                     gboolean        is_source,
                                                     GPtrArray      *sources,
                                                     DonnaNode      *dest,
                                                     const gchar    *new_name,
                                                     GError        **error);
    DonnaTask *         (*new_child_task)           (DonnaProvider  *provider,
                                                     DonnaNode      *parent,
                                                     DonnaNodeType   type,
                                                     const gchar    *name,
                                                     GError        **error);
    DonnaTask *         (*remove_from_task)         (DonnaProvider  *provider,
                                                     GPtrArray      *nodes,
                                                     DonnaNode      *source,
                                                     GError        **error);
    gchar *             (*get_context_alias)        (DonnaProvider  *provider,
                                                     const gchar    *alias,
                                                     const gchar    *extra,
                                                     DonnaContextReference reference,
                                                     const gchar    *prefix,
                                                     GError        **error);
    gboolean            (*get_context_item_info)    (DonnaProvider  *provider,
                                                     const gchar    *item,
                                                     const gchar    *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode      *node_ref,
                                                     get_sel_fn      get_sel,
                                                     gpointer        get_sel_data,
                                                     DonnaContextInfo *info,
                                                     GError        **error);
    gchar *             (*get_context_alias_new_nodes) (
                                                     DonnaProvider  *provider,
                                                     const gchar    *extra,
                                                     DonnaNode      *location,
                                                     const gchar    *prefix,
                                                     GError        **error);
};

/* signals */
void    donna_provider_new_node                     (DonnaProvider  *provider,
                                                     DonnaNode      *node);
void    donna_provider_node_updated                 (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     const gchar    *name);
void    donna_provider_node_deleted                 (DonnaProvider  *provider,
                                                     DonnaNode      *node);
void    donna_provider_node_children                (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GPtrArray      *children);
void    donna_provider_node_new_child               (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *child);
void    donna_provider_node_removed_from            (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *source);

/* API */
const gchar * donna_provider_get_domain             (DonnaProvider  *provider);
DonnaProviderFlags donna_provider_get_flags         (DonnaProvider  *provider);
DonnaNode * donna_provider_get_node                 (DonnaProvider  *provider,
                                                     const gchar    *location,
                                                     GError        **error);
DonnaTask * donna_provider_has_node_children_task   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GError        **error);
DonnaTask * donna_provider_get_node_children_task   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GError        **error);
DonnaTask * donna_provider_trigger_node_task        (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
DonnaTask * donna_provider_io_task                  (DonnaProvider  *provider,
                                                     DonnaIoType     type,
                                                     gboolean        is_source,
                                                     GPtrArray      *sources,
                                                     DonnaNode      *dest,
                                                     const gchar    *new_name,
                                                     GError        **error);
DonnaTask * donna_provider_new_child_task           (DonnaProvider  *provider,
                                                     DonnaNode      *parent,
                                                     DonnaNodeType   type,
                                                     const gchar    *name,
                                                     GError        **error);
DonnaTask * donna_provider_remove_from_task         (DonnaProvider  *provider,
                                                     GPtrArray      *nodes,
                                                     DonnaNode      *source,
                                                     GError        **error);
/* context related */
gchar *     donna_provider_get_context_alias        (DonnaProvider  *provider,
                                                     const gchar    *alias,
                                                     const gchar    *extra,
                                                     DonnaContextReference reference,
                                                     const gchar    *prefix,
                                                     GError        **error);
gboolean    donna_provider_get_context_item_info    (DonnaProvider  *provider,
                                                     const gchar    *item,
                                                     const gchar    *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode      *node_ref,
                                                     get_sel_fn      get_sel,
                                                     gpointer        get_sel_data,
                                                     DonnaContextInfo *info,
                                                     GError        **error);
gchar *     donna_provider_get_context_alias_new_nodes (
                                                     DonnaProvider  *provider,
                                                     const gchar    *extra,
                                                     DonnaNode      *location,
                                                     const gchar    *prefix,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_H__ */
