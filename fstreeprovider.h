
#ifndef __FSTREE_PROVIDER_H__
#define __FSTREE_PROVIDER_H__

G_BEGIN_DECLS

#define TYPE_FSTREE_NODE                (fstree_node_get_type ())
#define FSTREE_NODE(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSTREE_NODE, FsTreeNode))
#define IS_FSTREE_NODE(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSTREE_NODE))
#define FSTREE_NODE_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), TYPE_FSTREE_NODE, FsTreeNodeInterface))

typedef struct _FsTreeNode              FsTreeNode; /* dummy typedef */
typedef struct _FsTreeNodeInterface     FsTreeNodeInterface;


#define TYPE_FSTREE_PROVIDER                (fstree_provider_get_type ())
#define FSTREE_PROVIDER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSTREE_PROVIDER, FsTreeProvider))
#define IS_FSTREE_PROVIDER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSTREE_PROVIDER))
#define FSTREE_PROVIDER_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), TYPE_FSTREE_PROVIDER, FsTreeProviderInterface))

typedef struct _FsTreeProvider              FsTreeProvider; /* dummy typedef */
typedef struct _FsTreeProviderInterface     FsTreeProviderInterface;


struct _FsTreeNodeInterface
{
    GTypeInterface parent;

    /* signals */
    void                (*destroy)          (FsTreeNode  *node);

    /* virtual table */
    gboolean            (*set_location)     (FsTreeNode  *node,
                                             const gchar *location,
                                             GError     **error);
    gboolean            (*set_name)         (FsTreeNode  *node,
                                             const gchar *name,
                                             GError     **error);
    gboolean            (*add_iter)         (FsTreeNode  *node,
                                             GtkTreeIter *iter);
    gboolean            (*remove_iter)      (FsTreeNode  *node,
                                             GtkTreeIter *iter);
};

GType               fstree_node_get_type        (void) G_GNUC_CONST;

gboolean            fstree_node_set_location    (FsTreeNode  *node,
                                                 const gchar *location,
                                                 GError     **error);
gboolean            fstree_node_set_name        (FsTreeNode  *node,
                                                 const gchar *name,
                                                 GError     **error);
FsTreeNode **       fstree_node_get_children    (FsTreeNode  *node,
                                                 GError     **error);
gboolean            fstree_node_add_iter        (FsTreeNode  *node,
                                                 GtkTreeIter *iter);
gboolean            fstree_node_remove_iter     (FsTreeNode  *node,
                                                 GtkTreeIter *iter);


struct _FsTreeProviderInterface
{
    GTypeInterface parent;

    /* signals */
    void            (*node_created)         (FsTreeProvider  *provider,
                                             FsTreeNode      *node);

    /* virtual table */
    FsTreeNode *    (*get_node)             (FsTreeProvider  *provider);
    FsTreeNode **   (*get_children)         (FsTreeProvider  *provider,
                                             FsTreeNode      *node,
                                             GError         **error);
};

GType           fstree_provider_get_type        (void) G_GNUC_CONST;

FsTreeNode *    fstree_provider_get_node        (FsTreeProvider  *provider);
FsTreeNode **   fstree_provider_get_children    (FsTreeProvider  *provider,
                                                 FsTreeNode      *node,
                                                 GError         **error);

G_END_DECLS

#endif /* __FSTREE_PROVIDER_H__ */
