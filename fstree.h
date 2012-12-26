
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

enum
{
    FST_COL_FULL_NAME = 0,
    FST_COL_DISPLAY_NAME,
    FST_COL_WAS_EXPANDED,
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

GType       fstree_get_type                         (void) G_GNUC_CONST;

GtkWidget * fstree_new                              (const gchar *root);
gboolean    fstree_add_root                         (FsTree *fstree,
                                                     const gchar *root);
gboolean    fstree_set_root                         (FsTree *fstree,
                                                     const gchar *root);

G_END_DECLS

#endif /* __FSTREE_H__ */
