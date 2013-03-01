
#ifndef __DONNA_COMMON_H__
#define __DONNA_COMMON_H__

G_BEGIN_DECLS

typedef struct _DonnaNode               DonnaNode;
typedef struct _DonnaNodeClass          DonnaNodeClass;
typedef struct _DonnaNodePrivate        DonnaNodePrivate;

#define DONNA_TYPE_NODE                 (donna_node_get_type ())
#define DONNA_NODE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_NODE, DonnaNode))
#define DONNA_NODE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_NODE, DonnaNodeClass))
#define DONNA_IS_NODE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_NODE))
#define DONNA_IS_NODE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_NODE))
#define DONNA_NODE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_NODE, DonnaNodeClass))

GType           donna_node_get_type         (void) G_GNUC_CONST;


typedef struct _DonnaTask               DonnaTask;
typedef struct _DonnaTaskClass          DonnaTaskClass;
typedef struct _DonnaTaskPrivate        DonnaTaskPrivate;

#define DONNA_TYPE_TASK                 (donna_task_get_type ())
#define DONNA_TASK(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TASK, DonnaTask))
#define DONNA_TASK_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TASK, DonnaTaskClass))
#define DONNA_IS_TASK(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TASK))
#define DONNA_IS_TASK_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_TASK))
#define DONNA_TASK_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TASK, DonnaTaskClass))

GType           donna_task_get_type     (void) G_GNUC_CONST;


typedef struct _DonnaProvider               DonnaProvider; /* dummy typedef */
typedef struct _DonnaProviderInterface      DonnaProviderInterface;

#define DONNA_TYPE_PROVIDER                 (donna_provider_get_type ())
#define DONNA_PROVIDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER, DonnaProvider))
#define DONNA_IS_PROVIDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER))
#define DONNA_PROVIDER_GET_INTERFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_PROVIDER, DonnaProviderInterface))

GType           donna_provider_get_type     (void) G_GNUC_CONST;


typedef struct _DonnaTreeView           DonnaTreeView;
typedef struct _DonnaTreeViewPrivate    DonnaTreeViewPrivate;
typedef struct _DonnaTreeViewClass      DonnaTreeViewClass;

#define DONNA_TYPE_TREE_VIEW            (donna_tree_view_get_type ())
#define DONNA_TREE_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TREE_VIEW, DonnaTreeView))
#define DONNA_TREE_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TREE_VIEW, DonnaTreeViewClass))
#define DONNA_IS_TREE_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TREE_VIEW))
#define DONNA_IS_TREE_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_TREE_VIEW))
#define DONNA_TREE_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TREE_VIEW, DonnaTreeViewClass))

GType           donna_tree_view_get_type        (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COMMON_H__ */
