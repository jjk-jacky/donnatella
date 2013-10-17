
#ifndef __DONNA_PROVIDER_H__
#define __DONNA_PROVIDER_H__

#include "common.h"
#include "node.h"
#include "task.h"
#include "contextmenu.h"

G_BEGIN_DECLS

#define DONNA_PROVIDER_ERROR        g_quark_from_static_string ("DonnaProvider-Error")
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

typedef enum
{
    DONNA_PROVIDER_FLAG_INVALID = (1 << 0),
    DONNA_PROVIDER_FLAG_FLAT    = (1 << 1),
} DonnaProviderFlags;

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
    gchar *             (*get_context_alias_new_nodes) (
                                                     DonnaProvider  *provider,
                                                     const gchar    *extra,
                                                     DonnaNode      *location,
                                                     const gchar    *prefix,
                                                     GError        **error);
    gboolean            (*get_context_item_info)    (DonnaProvider  *provider,
                                                     const gchar    *item,
                                                     const gchar    *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode      *node_ref,
                                                     tree_context_get_sel_fn get_sel,
                                                     gpointer        get_sel_data,
                                                     DonnaContextInfo *info,
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
gchar *     donna_provider_get_context_alias_new_nodes (
                                                     DonnaProvider  *provider,
                                                     const gchar    *extra,
                                                     DonnaNode      *location,
                                                     const gchar    *prefix,
                                                     GError        **error);
gboolean    donna_provider_get_context_item_info    (DonnaProvider  *provider,
                                                     const gchar    *item,
                                                     const gchar    *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode      *node_ref,
                                                     tree_context_get_sel_fn get_sel,
                                                     gpointer        get_sel_data,
                                                     DonnaContextInfo *info,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_H__ */
