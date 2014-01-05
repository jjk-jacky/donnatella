
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
    /*< private >*/
    GObject parent;

    DonnaApp *app;
    DonnaProviderBasePrivate *priv;
};

/**
 * DonnaProviderBaseClass:
 * @parent: Parent class
 * @task_visibility: Define the different visibility for #DonnaTask<!-- -->s that
 * will be created
 * @lock_nodes: Call this to lock the hashmap of nodes. Must always be paired
 * with a call to @unlock_nodes<!-- -->()
 * @unlock_nodes: Call this to unlock the hashmap of nodes
 * @get_cached_node: Returns the #DonnaNode for @location from the hashmap, or
 * %NULL if there's none. Must be called after a call to @lock_nodes<!-- -->()
 * Note that this adds a reference to the returned node
 * @add_node_to_cache: Add @node to the cache/hashmap. Must be called after a
 * call to @lock_nodes<!-- -->() Note that this adds a reference on @node
 * @new_node: Task worker that create a new #DonnaNode for @location and set it
 * as #DonnaTask:return-value of @task, then returning %DONNA_TASK_DONE
 * It should also make sure to lock, check the node wasn't created meanwhile,
 * add the node to the hashmap and unlock.
 * Note that if visibility for new_node was %DONNA_TASK_VISIBILITY_INTERNAL_FAST
 * then the worker will be called & handled directly, so
 * donna_provider_get_node() returns the #DonnaNode directly (thus avoiding the
 * need for a #GMainLoop)
 * @unref_node: Called whenever the last reference on a node is removed, and the
 * node is removed from the hashmap/finalized. This only needs to be implemented
 * if additional cleaning is required.
 * @has_children: Task worker for the task returned by
 * donna_provider_has_node_children_task()
 * @get_children: Task worker for the task returned by
 * donna_provider_get_node_children_task()
 * @trigger_node: Task worker for the task returned by
 * donna_provider_trigger_node_task()
 * @support_io: When donna_provider_io_task() is called, this will be called to
 * determine if this kind of IO operation can be performed. If not, if should
 * set @error accordingly. Lack of implementation will cause a
 * %DONNA_PROVIDER_ERROR_NOT_SUPPORTED to be set.
 * @io: Task worker for the task returned by donna_provider_io_task()
 * @new_child: Task worker for the task returned by
 * donna_provider_new_child_task()
 * @remove_from: Task worker for the task returned by
 * donna_provider_remove_from_task()
 *
 * All task visibilities are set to %DONNA_TASK_VISIBILITY_INTERNAL by default.
 * Make sure to change those as needed in @task_visibility
 */
struct _DonnaProviderBaseClass
{
    GObjectClass parent;

    /* visibilities for the different tasks */
    struct
    {
        DonnaTaskVisibility new_node;
        DonnaTaskVisibility has_children;
        DonnaTaskVisibility get_children;
        DonnaTaskVisibility trigger_node;
        DonnaTaskVisibility io;
        DonnaTaskVisibility new_child;
        DonnaTaskVisibility remove_from;
    } task_visibility;

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
