
#ifndef __DONNA_TREE_H__
#define __DONNA_TREE_H__

#include "common.h"

G_BEGIN_DECLS

#define DONNA_TYPE_TREE             (donna_tree_get_type ())
#define DONNA_TREE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TREE, DonnaTree))
#define DONNA_TREE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TREE, DonnaTreeClass))
#define DONNA_IS_TREE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TREE))
#define DONNA_IS_TREE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_TREE))
#define DONNA_TREE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TREE, DonnaTreeClass))

typedef struct _DonnaTree           DonnaTree;
typedef struct _DonnaTreePrivate    DonnaTreePrivate;
typedef struct _DonnaTreeClass      DonnaTreeClass;

#define DONNA_TREE_ERROR            g_quark_from_static_string ("DonnaTree-Error")
enum
{
    DONNA_TREE_ERROR_NOMEM
} DonnaTreeError;

typedef enum
{
    DONNA_TREE_EXPAND_NEVER = 0,
    DONNA_TREE_EXPAND_WIP,
    DONNA_TREE_EXPAND_PARTIAL,
    DONNA_TREE_EXPAND_FULL
} DonnaTreeExpand;

enum
{
    DONNA_TREE_COL_NODE = 0,
    DONNA_TREE_COL_EXPAND_STATE,
    DONNA_TREE_NB_COLS
};

struct _DonnaTree
{
    /*< private >*/
    GtkTreeView treeview;

    DonnaTreePrivate *priv;
};

struct _DonnaTreeClass
{
    GtkTreeViewClass parent_class;
};

GType           donna_tree_get_type                 (void) G_GNUC_CONST;

GtkWidget *     donna_tree_new                      (DonnaNode      *node);
gboolean        donna_tree_add_root                 (DonnaTree      *tree,
                                                     DonnaNode      *node);
gboolean        donna_tree_set_root                 (DonnaTree      *tree,
                                                     DonnaNode      *node);
gboolean        donna_tree_set_show_hidden          (DonnaTree      *tree,
                                                     gboolean        show_hidden);
gboolean        donna_tree_get_show_hidden          (DonnaTree      *tree,
                                                     gboolean       *show_hidden);

G_END_DECLS

#endif /* __DONNA_TREE_H__ */
