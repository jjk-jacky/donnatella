
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

typedef struct _DonnaArrangement        DonnaArrangement;

typedef enum
{
    DONNA_ARRANGEMENT_PRIORITY_LOW,
    DONNA_ARRANGEMENT_PRIORITY_NORMAL,
    DONNA_ARRANGEMENT_PRIORITY_HIGH,
    DONNA_ARRANGEMENT_PRIORITY_OVERRIDE
} DonnaArrangementPriority;

typedef enum
{
    DONNA_SORT_UNKNOWN = 0,
    DONNA_SORT_ASC,
    DONNA_SORT_DESC
} DonnaSortOrder;

typedef enum
{
    DONNA_SECOND_SORT_STICKY_UNKNOWN = 0,
    DONNA_SECOND_SORT_STICKY_ENABLED,
    DONNA_SECOND_SORT_STICKY_DISABLED
} DonnaSecondSortSticky;

typedef enum
{
    DONNA_ARRANGEMENT_HAS_COLUMNS               = (1 << 0),
    DONNA_ARRANGEMENT_HAS_SORT                  = (1 << 1),
    DONNA_ARRANGEMENT_HAS_SECOND_SORT           = (1 << 2),
    DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS       = (1 << 3),
    DONNA_ARRANGEMENT_HAS_COLOR_FILTERS         = (1 << 4),

    DONNA_ARRANGEMENT_COLUMNS_ALWAYS            = (1 << 10),
    DONNA_ARRANGEMENT_SORT_ALWAYS               = (1 << 11),
    DONNA_ARRANGEMENT_SECOND_SORT_ALWAYS        = (1 << 12),
    DONNA_ARRANGEMENT_COLUMNS_OPTIONS_ALWAYS    = (1 << 13),
    DONNA_ARRANGEMENT_COLOR_FILTERS_ALWAYS      = (1 << 14),

    DONNA_ARRANGEMENT_HAS_ALL           = DONNA_ARRANGEMENT_HAS_COLUMNS
        | DONNA_ARRANGEMENT_HAS_SORT | DONNA_ARRANGEMENT_HAS_SECOND_SORT
        | DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS
        | DONNA_ARRANGEMENT_HAS_COLOR_FILTERS
} DonnaArrangementFlags;

struct _DonnaArrangement
{
    DonnaArrangementPriority     priority;
    DonnaArrangementFlags        flags;
    gchar                       *columns;
    gchar                       *sort_column;
    DonnaSortOrder               sort_order;
    gchar                       *second_sort_column;
    DonnaSortOrder               second_sort_order;
    DonnaSecondSortSticky        second_sort_sticky;
    gchar                       *columns_options;
    GSList                      *color_filters;
};

struct _DonnaTreeView
{
    /*< private >*/
    GtkTreeView              treeview;
    DonnaTreeViewPrivate    *priv;
};

struct _DonnaTreeViewClass
{
    GtkTreeViewClass parent_class;

    DonnaArrangement *      (*select_arrangement)       (DonnaTreeView  *tree,
                                                         const gchar    *name,
                                                         DonnaNode      *node);
};

GtkWidget *     donna_tree_view_new             (DonnaApp           *app,
                                                 const gchar        *name);
const gchar *   donna_tree_view_get_name        (DonnaTreeView      *tree);
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
GPtrArray *     donna_tree_view_get_selected_nodes (DonnaTreeView   *tree);
/* mode Tree */
gboolean        donna_tree_view_load_tree       (DonnaTreeView      *tree,
                                                 const gchar        *data);
gchar *         donna_tree_view_export_tree     (DonnaTreeView      *tree);
gboolean        donna_tree_view_add_root        (DonnaTreeView      *tree,
                                                 DonnaNode          *node);
/* Mode List */
GPtrArray *     donna_tree_view_get_children    (DonnaTreeView      *tree,
                                                 DonnaNode          *node,
                                                 DonnaNodeType       node_types);

G_END_DECLS

#endif /* __DONNA_TREE_VIEW_H__ */
