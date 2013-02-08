
#ifndef __DONNA_TREEVIEW_H__
#define __DONNA_TREEVIEW_H__

#include "common.h"
#include "columntype.h"

G_BEGIN_DECLS

#define DONNA_TYPE_TREEVIEW             (donna_treeview_get_type ())
#define DONNA_TREEVIEW(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TREEVIEW, DonnaTreeView))
#define DONNA_TREEVIEW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TREEVIEW, DonnaTreeViewClass))
#define DONNA_IS_TREEVIEW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TREEVIEW))
#define DONNA_IS_TREEVIEW_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_TREEVIEW))
#define DONNA_TREEVIEW_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TREEVIEW, DonnaTreeViewClass))

typedef struct _DonnaTreeView           DonnaTreeView;
typedef struct _DonnaTreeViewPrivate    DonnaTreeViewPrivate;
typedef struct _DonnaTreeViewClass      DonnaTreeViewClass;

typedef struct _DonnaColumn             DonnaColumn;
typedef struct _DonnaArrangement        DonnaArrangement;

#define DONNA_TREEVIEW_ERROR            g_quark_from_static_string ("DonnaTreeView-Error")
enum
{
    DONNA_TREEVIEW_ERROR_NOMEM
} DonnaTreeViewError;

typedef enum
{
    DONNA_TREEVIEW_MODE_TREE,
    DONNA_TREEVIEW_MODE_LIST
} DonnaTreeViewMode;

typedef enum
{
    DONNA_TREEVIEW_SYNC_NONE,
    DONNA_TREEVIEW_SYNC_NODES,
    DONNA_TREEVIEW_SYNC_NODES_CHILDREN,
    DONNA_TREEVIEW_SYNC_FULL
} DonnaTreeViewSync;

struct _DonnaColumn
{
    gchar           *name;
    gchar           *title;
    DonnaColumnType *ct;
    gpointer         options;
    gint             ref_count;
};

struct _DonnaArrangement
{
    gchar    *mask;
    gchar   **columns;
    gchar    *sort;
    gboolean  autosave;
    gint      ref_count;
};

struct _DonnaTreeView
{
    /*< private >*/
    GtkTreeView treeview;

    DonnaTreeViewPrivate *priv;
};

struct _DonnaTreeViewClass
{
    GtkTreeViewClass parent_class;
};

GType           donna_treeview_get_type         (void) G_GNUC_CONST;

GtkWidget *     donna_treeview_new              (DonnaTreeViewMode   mode);
gboolean        donna_treeview_set_show_hidden  (DonnaTreeView      *treeview,
                                                 gboolean            show_hidden);
gboolean        donna_treeview_get_show_hidden  (DonnaTreeView      *treeview);
/* mode Tree */
gboolean        donna_treeview_set_minitree     (DonnaTreeView      *tree,
                                                 gboolean            is_minitree);
gboolean        donna_treeview_set_sync         (DonnaTreeView      *tree,
                                                 DonnaTreeViewSync   sync);
gboolean        donna_treeview_get_minitree     (DonnaTreeView      *tree);
DonnaTreeViewSync donna_treeview_get_sync       (DonnaTreeView      *tree);
gboolean        donna_treeview_add_root         (DonnaTreeView      *tree,
                                                 DonnaNode          *node);
/* Mode List */

G_END_DECLS

#endif /* __DONNA_TREEVIEW_H__ */
