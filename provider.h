
#ifndef __DONNA_PROVIDER_H__
#define __DONNA_PROVIDER_H__

#include "common.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_PROVIDER_ERROR        g_quark_from_static_string ("DonnaProvider-Error")
typedef enum
{
    DONNA_PROVIDER_ERROR_WRONG_PROVIDER,
    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
    DONNA_PROVIDER_ERROR_WRONG_NODE_TYPE,
    DONNA_PROVIDER_ERROR_INVALID_CALL,
    DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
    DONNA_PROVIDER_ERROR_ALREADY_EXIST,
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
    void                (*node_removed)             (DonnaProvider  *provider,
                                                     DonnaNode      *node);
    void                (*node_children)            (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GPtrArray      *children);
    void                (*node_new_child)           (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *child);

    /* virtual table */
    const gchar *       (*get_domain)               (DonnaProvider  *provider);
    DonnaProviderFlags  (*get_flags)                (DonnaProvider  *provider);
    DonnaTask *         (*get_node_task)            (DonnaProvider  *provider,
                                                     const gchar    *location,
                                                     GError        **error);
    DonnaTask *         (*has_node_children_task)   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GError        **error);
    DonnaTask *         (*get_node_children_task)   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GError        **error);
    DonnaTask *         (*get_node_parent_task)     (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
    DonnaTask *         (*trigger_node_task)        (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
    DonnaTask *         (*io_task)                  (DonnaProvider  *provider,
                                                     DonnaIoType     type,
                                                     gboolean        is_source,
                                                     GPtrArray      *sources,
                                                     DonnaNode      *dest,
                                                     GError        **error);
    DonnaTask *         (*new_child_task)           (DonnaProvider  *provider,
                                                     DonnaNode      *parent,
                                                     DonnaNodeType   type,
                                                     const gchar    *name,
                                                     GError        **error);
};

/* signals */
void    donna_provider_new_node                     (DonnaProvider  *provider,
                                                     DonnaNode      *node);
void    donna_provider_node_updated                 (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     const gchar    *name);
void    donna_provider_node_removed                 (DonnaProvider  *provider,
                                                     DonnaNode      *node);
void    donna_provider_node_children                (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GPtrArray      *children);
void    donna_provider_node_new_child               (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *child);

/* API */
const gchar * donna_provider_get_domain             (DonnaProvider  *provider);
DonnaProviderFlags donna_provider_get_flags         (DonnaProvider  *provider);
DonnaTask * donna_provider_get_node_task            (DonnaProvider  *provider,
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
DonnaTask * donna_provider_remove_node_task         (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
DonnaTask * donna_provider_get_node_parent_task     (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
DonnaTask * donna_provider_trigger_node_task        (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GError        **error);
DonnaTask * donna_provider_io_task                  (DonnaProvider  *provider,
                                                     DonnaIoType     type,
                                                     gboolean        is_source,
                                                     GPtrArray      *sources,
                                                     DonnaNode      *dest,
                                                     GError        **error);
DonnaTask * donna_provider_new_child_task           (DonnaProvider  *provider,
                                                     DonnaNode      *parent,
                                                     DonnaNodeType   type,
                                                     const gchar    *name,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_H__ */
