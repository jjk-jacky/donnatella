
#ifndef __DONNA_PROVIDER_BASE_H__
#define __DONNA_PROVIDER_BASE_H__

#include "common.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_BASE            (donna_provider_base_get_type ())
#define DONNA_PROVIDER_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_BASE, DonnaProviderBase))
#define DONNA_PROVIDER_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_BASE, DonnaProviderBaseClass))
#define DONNA_IS_PROVIDER_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_BASE))
#define DONNA_IS_PROVIDER_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_BASE))
#define DONNA_PROVIDER_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_BASE, DonnaProviderBaseClass))

typedef struct _DonnaProviderBase           DonnaProviderBase;
typedef struct _DonnaProviderBaseClass      DonnaProviderBaseClass;
typedef struct _DonnaProviderBasePrivate    DonnaProviderBasePrivate;

struct _DonnaProviderBase
{
    GObject parent;

    DonnaApp *app;
    DonnaProviderBasePrivate *priv;
};

struct _DonnaProviderBaseClass
{
    GObjectClass parent;

    /* visiblities for the different tasks */
    struct
    {
        DonnaTaskVisibility new_node;
        DonnaTaskVisibility has_children;
        DonnaTaskVisibility get_children;
        DonnaTaskVisibility trigger_node;
        DonnaTaskVisibility io;
        DonnaTaskVisibility new_child;
        DonnaTaskVisibility remove_from;
    } task_visiblity;

    /* virtual table */
    void            (*lock_nodes)           (DonnaProviderBase  *provider);
    void            (*unlock_nodes)         (DonnaProviderBase  *provider);
    DonnaNode *     (*get_cached_node)      (DonnaProviderBase  *provider,
                                             const gchar        *location);
    void            (*add_node_to_cache)    (DonnaProviderBase  *provider,
                                             DonnaNode          *node);

    DonnaTaskState  (*new_node)             (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             const gchar        *location);
    void            (*unref_node)           (DonnaProviderBase  *provider,
                                             DonnaNode          *node);
    DonnaTaskState  (*has_children)         (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             DonnaNode          *node,
                                             DonnaNodeType       node_types);
    DonnaTaskState  (*get_children)         (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             DonnaNode          *node,
                                             DonnaNodeType       node_types);
    DonnaTaskState  (*trigger_node)         (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             DonnaNode          *node);
    gboolean        (*support_io)           (DonnaProviderBase  *provider,
                                             DonnaIoType         type,
                                             gboolean            is_source,
                                             GPtrArray          *sources,
                                             DonnaNode          *dest,
                                             const gchar        *new_name,
                                             GError            **error);
    DonnaTaskState  (*io)                   (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             DonnaIoType         type,
                                             gboolean            is_source,
                                             GPtrArray          *sources,
                                             DonnaNode          *dest,
                                             const gchar        *new_name);
    DonnaTaskState  (*new_child)            (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             DonnaNode          *parent,
                                             DonnaNodeType       type,
                                             const gchar        *name);
    DonnaTaskState  (*remove_from)          (DonnaProviderBase  *provider,
                                             DonnaTask          *task,
                                             GPtrArray          *nodes,
                                             DonnaNode          *source);
};

GType           donna_provider_base_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_BASE_H__ */
