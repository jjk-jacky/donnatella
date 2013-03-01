
#ifndef __DONNA_TREE_VIEW_H__
#define __DONNA_TREE_VIEW_H__

#include "common.h"
#include "columntype.h"
#include "conf.h"

G_BEGIN_DECLS

#define DONNA_TREE_VIEW_ERROR            g_quark_from_static_string ("DonnaTreeView-Error")
enum
{
    DONNA_TREE_VIEW_ERROR_NOMEM
} DonnaTreeViewError;

typedef void                (*run_task_fn)          (DonnaTask      *task,
                                                     gpointer        data);
typedef DonnaSharedString * (*get_arrangement_fn)   (DonnaNode      *node,
                                                     gpointer        data);
typedef DonnaColumnType *   (*get_column_type_fn)   (const gchar    *type);

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

GtkWidget *     donna_tree_view_new             (DonnaConfig        *config,
                                                 const gchar        *name,
                                                 get_column_type_fn  get_ct);
gboolean        donna_tree_view_set_task_runner (DonnaTreeView      *tree,
                                                 run_task_fn         task_runner,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
gboolean        donna_tree_view_set_arrangement_selector (
                                                 DonnaTreeView      *tree,
                                                 get_arrangement_fn  arrgmt_sel,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
void            donna_tree_view_build_arrangement (
                                                 DonnaTreeView      *tree,
                                                 gboolean            force);
/* both modes */
gboolean        donna_tree_view_set_node_property (DonnaTreeView    *tree,
                                                 DonnaNode          *node,
                                                 DonnaSharedString  *prop,
                                                 const GValue       *value);
/* mode Tree */
gboolean        donna_tree_view_load_tree       (DonnaTreeView      *tree,
                                                 const gchar        *data);
gchar *         donna_tree_view_export_tree     (DonnaTreeView      *tree);
gboolean        donna_tree_view_add_root        (DonnaTreeView      *tree,
                                                 DonnaNode          *node);
/* Mode List */
gboolean        donna_tree_view_set_location    (DonnaTreeView      *list,
                                                 DonnaNode          *node);

G_END_DECLS

#endif /* __DONNA_TREE_VIEW_H__ */
