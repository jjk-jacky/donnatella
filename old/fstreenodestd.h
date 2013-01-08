
#ifndef __FSTREE_NODE_STD_H__
#define __FSTREE_NODE_STD_H__

G_BEGIN_DECLS

#define TYPE_FSTREE_NODE_STD            (fstree_node_std_get_type ())
#define FSTREE_NODE_STD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSTREE_NODE_STD, FsTreeNodeStd))
#define FSTREE_NODE_STD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FSTREE_NODE_STD, FsTreeNodeStdClass))
#define IS_FSTREE_NODE_STD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSTREE_NODE_STD))
#define IS_FSTREE_NODE_STD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FSTREE_NODE_STD))
#define FSTREE_NODE_STD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FSTREE_NODE_STD, FsTreeNodeStdClass))

typedef struct _FsTreeNodeStd           FsTreeNodeStd;
typedef struct _FsTreeNodeStdClass      FsTreeNodeStdClass;
typedef struct _FsTreeNodeStdPrivate    FsTreeNodeStdPrivate;

struct _FsTreeNodeStd
{
    GObject parent;

    FsTreeNodeStdPrivate *priv;
};

struct _FsTreeNodeStdClass
{
    GObjectClass parent;
};

GType               fstree_node_std_get_type    (void) G_GNUC_CONST;

FsTreeNodeStd *     fstree_node_std_new         (FsTreeProvider *provider,
                                                 const gchar    *location,
                                                 const gchar    *name);

G_END_DECLS

#endif /* __FSTREE_NODE_STD_H__ */
