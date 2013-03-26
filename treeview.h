
#ifndef __DONNA_TREE_VIEW_H__
#define __DONNA_TREE_VIEW_H__

#include "common.h"
#include "app.h"
#include "columntype.h"
#include "conf.h"
#include "node.h"

G_BEGIN_DECLS

#define DONNA_TREE_VIEW_ERROR            g_quark_from_static_string ("DonnaTreeView-Error")
enum
{
    DONNA_TREE_VIEW_ERROR_NOMEM,
    DONNA_TREE_VIEW_ERROR_NOT_FOUND,
    DONNA_TREE_VIEW_ERROR_CANNOT_ADD_NODE,
    DONNA_TREE_VIEW_ERROR_OTHER,
} DonnaTreeViewError;

struct _DonnaTreeView
{
    /*< private >*/
    GtkTreeView              treeview;
    DonnaTreeViewPrivate    *priv;
};

struct _DonnaTreeViewClass
{
    GtkTreeViewClass parent_class;
};

GtkWidget *     donna_tree_view_new             (DonnaApp           *app,
                                                 const gchar        *name);
void            donna_tree_view_build_arrangement (
                                                 DonnaTreeView      *tree,
                                                 gboolean            force);
/* both modes */
gboolean        donna_tree_view_set_node_property (DonnaTreeView    *tree,
                                                 DonnaNode          *node,
                                                 const gchar        *prop,
                                                 const GValue       *value,
                                                 GError            **error);
gboolean        donna_tree_view_set_location    (DonnaTreeView      *tree,
                                                 DonnaNode          *node,
                                                 GError            **error);
DonnaNode *     donna_tree_view_get_location    (DonnaTreeView      *tree);
/* mode Tree */
gboolean        donna_tree_view_load_tree       (DonnaTreeView      *tree,
                                                 const gchar        *data);
gchar *         donna_tree_view_export_tree     (DonnaTreeView      *tree);
gboolean        donna_tree_view_add_root        (DonnaTreeView      *tree,
                                                 DonnaNode          *node);
/* Mode List */
GPtrArray *     donna_tree_view_get_children    (DonnaTreeView      *tree,
                                                 DonnaNodeType       node_types);

G_END_DECLS

#endif /* __DONNA_TREE_VIEW_H__ */
