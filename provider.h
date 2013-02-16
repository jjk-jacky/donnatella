
#ifndef __DONNA_PROVIDER_H__
#define __DONNA_PROVIDER_H__

#include "common.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_PROVIDER_ERROR        g_quark_from_static_string ("DonnaProvider-Error")
typedef enum
{
    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
} DonnaProviderError;

struct _DonnaProviderInterface
{
    GTypeInterface parent;

    /* signals */
    void            (*new_node)                     (DonnaProvider  *provider,
                                                     DonnaNode      *node);
    void            (*node_updated)                 (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     const gchar    *name);
    void            (*node_removed)                 (DonnaProvider  *provider,
                                                     DonnaNode      *node);
    void            (*node_children)                (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types,
                                                     GPtrArray      *children);
    void            (*node_new_child)               (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNode      *child);

    /* virtual table */
    const gchar *   (*get_domain)                   (DonnaProvider  *provider);
    DonnaTask *     (*get_node_task)                (DonnaProvider  *provider,
                                                     const gchar    *location);
    DonnaTask *     (*has_node_children_task)       (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types);
    DonnaTask *     (*get_node_children_task)       (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types);
    DonnaTask *     (*remove_node_task)             (DonnaProvider  *provider,
                                                     DonnaNode      *node);
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
DonnaTask * donna_provider_get_node_task            (DonnaProvider  *provider,
                                                     const gchar    *location);
DonnaTask * donna_provider_has_node_children_task   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types);
DonnaTask * donna_provider_get_node_children_task   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     DonnaNodeType   node_types);
DonnaTask * donna_provider_remove_node_task         (DonnaProvider  *provider,
                                                     DonnaNode      *node);

G_END_DECLS

#endif /* __DONNA_PROVIDER_H__ */
