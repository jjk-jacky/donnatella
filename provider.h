
#ifndef __DONNA_PROVIDER_H__
#define __DONNA_PROVIDER_H__

#include "common.h"
#include "task.h"

G_BEGIN_DECLS

struct _DonnaProviderInterface
{
    GTypeInterface parent;

    /* signals */
    void            (*node_created)         (DonnaProvider   *provider,
                                             DonnaNode       *node);
    void            (*node_removed)         (DonnaProvider   *provider,
                                             DonnaNode       *node);
    void            (*node_updated)         (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             const gchar     *name,
                                             const GValue    *old_value);
    void            (*node_children)        (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             DonnaNode      **children);
    void            (*node_new_child)       (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             DonnaNode       *child);
    void            (*node_new_content)     (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             DonnaNode       *content);

    /* virtual table */
    DonnaTask *     (*get_node)             (DonnaProvider   *provider,
                                             const gchar     *location,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
    DonnaTask *     (*get_content)          (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
    DonnaTask *     (*get_children)         (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
    DonnaTask *     (*remove_node)          (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
};

/* signals */
void    donna_provider_node_created         (DonnaProvider   *provider,
                                             DonnaNode       *node);
void    donna_provider_node_removed         (DonnaProvider   *provider,
                                             DonnaNode       *node);
void    donna_provider_node_updated         (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             const gchar     *name,
                                             const GValue    *old_value);
void    donna_provider_node_children        (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             DonnaNode      **children);
void    donna_provider_node_new_child       (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             DonnaNode       *child);
void    donna_provider_node_new_content     (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             DonnaNode       *content);

/* API */
DonnaTask * donna_provider_get_node         (DonnaProvider   *provider,
                                             const gchar     *location,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
DonnaTask * donna_provider_get_content      (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
DonnaTask * donna_provider_get_children     (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);
DonnaTask * donna_provider_remove_node      (DonnaProvider   *provider,
                                             DonnaNode       *node,
                                             task_callback_fn callback,
                                             gpointer         callback_data,
                                             GDestroyNotify   callback_destroy,
                                             guint            timeout,
                                             task_timeout_fn  timeout_callback,
                                             gpointer         timeout_data,
                                             GDestroyNotify   timeout_destroy,
                                             GError         **error);


G_END_DECLS

#endif /* __DONNA_PROVIDER_H__ */
