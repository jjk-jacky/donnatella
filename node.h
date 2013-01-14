
#ifndef __DONNA_NODE_H__
#define __DONNA_NODE_H__

#include "common.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_NODE_ERROR            g_quark_from_static_string ("DonnaNode-Error")
typedef enum
{
    DONNA_NODE_ERROR_NOMEM,
    DONNA_NODE_ERROR_ALREADY_EXISTS,
    DONNA_NODE_ERROR_NOT_FOUND,
    DONNA_NODE_ERROR_READ_ONLY,
    DONNA_NODE_ERROR_INVALID_TYPE,
    DONNA_NODE_ERROR_OTHER
} DonnaNodeError;

/* functions called by a node to get/set a property value */
typedef gboolean    (*get_value_fn) (DonnaTask      *task,
                                     DonnaNode      *node);
typedef gboolean    (*set_value_fn) (DonnaTask      *task,
                                     DonnaNode      *node,
                                     const GValue   *value);
struct _DonnaNode
{
    GObject parent;

    DonnaNodePrivate *priv;
};

struct _DonnaNodeClass
{
    GObjectClass parent;
};

DonnaNode *     donna_node_new              (DonnaProvider          *provider,
                                             const gchar            *location,
                                             get_value_fn            location_get,
                                             set_value_fn            location_set,
                                             const gchar            *name,
                                             get_value_fn            name_get,
                                             set_value_fn            name_set,
                                             gboolean                is_container,
                                             get_value_fn            is_container_get,
                                             set_value_fn            is_container_set,
                                             get_value_fn            has_children_get,
                                             set_value_fn            has_children_set);
DonnaNode *     donna_node_new_from_node    (DonnaProvider          *provider,
                                             const gchar            *location,
                                             get_value_fn            location_get,
                                             set_value_fn            location_set,
                                             const gchar            *name,
                                             get_value_fn            name_get,
                                             set_value_fn            name_set,
                                             gboolean                is_container,
                                             get_value_fn            is_container_get,
                                             set_value_fn            is_container_set,
                                             get_value_fn            has_children_get,
                                             set_value_fn            has_children_set,
                                             DonnaNode              *source_node);
gboolean        donna_node_add_property     (DonnaNode              *node,
                                             const gchar            *name,
                                             GType                   type,
                                             GValue                 *value,
                                             get_value_fn            get_value,
                                             set_value_fn            set_value,
                                             GError                **error);
DonnaTask *     donna_node_set_property     (DonnaNode              *node,
                                             const gchar            *name,
                                             GValue                 *value,
                                             task_callback_fn        callback,
                                             gpointer                callback_data,
                                             GDestroyNotify          callback_destroy,
                                             guint                   timeout,
                                             task_timeout_fn         timeout_fn,
                                             gpointer                timeout_data,
                                             GDestroyNotify          timeout_destroy,
                                             GError                **error);
void            donna_node_get              (DonnaNode              *node,
                                             const gchar            *first_name,
                                             ...);
DonnaTask *     donna_node_refresh          (DonnaNode              *node,
                                             task_callback_fn        callback,
                                             gpointer                callback_data,
                                             GDestroyNotify          callback_destroy,
                                             guint                   timeout,
                                             task_timeout_fn         timeout_callback,
                                             gpointer                timeout_data,
                                             GDestroyNotify          timeout_destroy,
                                             const gchar            *first_name,
                                             ...);
void            donna_node_set_property_value   (DonnaNode          *node,
                                                 const gchar        *name,
                                                 GValue             *value);
int             donna_node_inc_toggle_count (DonnaNode              *node);
int             donna_node_dec_toggle_count (DonnaNode              *node);

G_END_DECLS

#endif /* __DONNA_NODE_H__ */
