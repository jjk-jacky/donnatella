
#ifndef __FSTREE_H__
#define __FSTREE_H__

G_BEGIN_DECLS

#define TYPE_FSTREE             (fstree_get_type ())
#define FSTREE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSTREE, FsTree))
#define FSTREE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FSTREE, FsTreeClass))
#define IS_FSTREE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSTREE))
#define IS_FSTREE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((obj), TYPE_FSTREE))
#define FSTREE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FSTREE, FsTreeClass))

typedef struct _FsTree          FsTree;
typedef struct _FsTreePrivate   FsTreePrivate;
typedef struct _FsTreeClass     FsTreeClass;

typedef struct _FsTreeNode          FsTreeNode;
typedef struct _FsTreeNodeFolder    FsTreeNodeFolder;
typedef gboolean              (*has_children_fn)    (const FsTreeNode  *node);
typedef FsTreeNode **         (*get_children_fn)    (const FsTreeNode  *node,
                                                     GError           **error);

#define FST_ERROR       g_quark_from_static_string ("FsTree-Error")
enum
{
    FST_ERROR_NOMEM
} fst_error_t;

struct _FsTreeNode
{
    char                *key;
    char                *name;
    char                *tooltip;
    has_children_fn      has_children;
    get_children_fn     get_children;
};

struct _FsTreeNodeFolder
{
    FsTreeNode  parent;
};

#define FSTREE_NODE(n)          (&(n)->parent)
#define FSTREE_NODE_FOLDER(n)   ((FsTreeNodeFolder *) n)

typedef enum
{
    FST_EXPAND_NEVER = 0,
    FST_EXPAND_PARTIAL,
    FST_EXPAND_FULL
} expand_state_t;

enum
{
    FST_COL_NODE = 0,
    FST_COL_EXPAND_STATE,
    FST_NB_COLS
};

struct _FsTree
{
    /*< private >*/
    GtkTreeView treeview;

    FsTreePrivate *priv;
};

struct _FsTreeClass
{
    GtkTreeViewClass parent_class;
};

GType           fstree_get_type                         (void) G_GNUC_CONST;

GtkWidget *     fstree_new                              (FsTreeNode *node);
gboolean        fstree_add_root                         (FsTree *fstree,
                                                         FsTreeNode *node);
gboolean        fstree_set_root                         (FsTree *fstree,
                                                         FsTreeNode *node);
FsTreeNode *    fstree_node_new_folder                  (const gchar *path);
void            fstree_free_node_folder                 (FsTreeNode *node);
gboolean        fstree_set_show_hidden                  (FsTree *fstree,
                                                         gboolean show_hidden);
gboolean        fstree_get_show_hidden                  (FsTree *fstree,
                                                         gboolean *show_hidden);

G_END_DECLS

#endif /* __FSTREE_H__ */
