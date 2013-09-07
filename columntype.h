
#ifndef __DONNA_COLUMNTYPE_H__
#define __DONNA_COLUMNTYPE_H__

#include <gtk/gtk.h>
#include "common.h"
#include "conf.h"

G_BEGIN_DECLS

typedef struct _DonnaColumnType             DonnaColumnType; /* dummy typedef */
typedef struct _DonnaColumnTypeInterface    DonnaColumnTypeInterface;

#define DONNA_TYPE_COLUMNTYPE               (donna_columntype_get_type ())
#define DONNA_COLUMNTYPE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMNTYPE, DonnaColumnType))
#define DONNA_IS_COLUMNTYPE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMNTYPE))
#define DONNA_COLUMNTYPE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_COLUMNTYPE, DonnaColumnTypeInterface))

GType           donna_columntype_get_type   (void) G_GNUC_CONST;

#define DONNA_COLUMNTYPE_ERROR              g_quark_from_static_string ("DonnaColumnType-Error")
typedef enum
{
    DONNA_COLUMNTYPE_ERROR_INVALID_SYNTAX,
    DONNA_COLUMNTYPE_ERROR_NOT_SUPPORTED,
    DONNA_COLUMNTYPE_ERROR_NODE_NO_PROP,
    DONNA_COLUMNTYPE_ERROR_NODE_NOT_WRITABLE,
    DONNA_COLUMNTYPE_ERROR_OTHER,
} DonnaColumnTypeError;


typedef DonnaColumnType *   (*new_ct)       (DonnaConfig        *config);

#define DONNA_COLUMNTYPE_RENDERER_TEXT      't'
#define DONNA_COLUMNTYPE_RENDERER_PIXBUF    'p'
#define DONNA_COLUMNTYPE_RENDERER_PROGRESS  'P'
#define DONNA_COLUMNTYPE_RENDERER_COMBO     'c'
#define DONNA_COLUMNTYPE_RENDERER_TOGGLE    'T'
#define DONNA_COLUMNTYPE_RENDERER_SPINNER   'S'

typedef enum
{
    DONNA_COLUMNTYPE_NEED_NOTHING   = 0,
    DONNA_COLUMNTYPE_NEED_REDRAW    = (1 << 0),
    DONNA_COLUMNTYPE_NEED_RESORT    = (1 << 1)
} DonnaColumnTypeNeed;

typedef gboolean    (*renderer_edit_fn)     (GtkCellRenderer    *renderer,
                                             gpointer            data);

struct _DonnaColumnTypeInterface
{
    GTypeInterface parent;

    /* virtual table */
    gboolean            (*helper_can_edit)  (DonnaColumnType    *ct,
                                             const gchar        *property,
                                             DonnaNode          *node,
                                             GError            **error);

    const gchar *       (*get_name)         (DonnaColumnType    *ct);
    const gchar *       (*get_renderers)    (DonnaColumnType    *ct);
    DonnaColumnTypeNeed (*refresh_data)     (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             const gchar        *arr_name,
                                             gpointer           *data);
    void                (*free_data)        (DonnaColumnType    *ct,
                                             gpointer            data);
    GPtrArray *         (*get_props)        (DonnaColumnType    *ct,
                                             gpointer            data);
    GtkSortType         (*get_default_sort_order)
                                            (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             const gchar        *arr_name,
                                             gpointer            data);
    GtkMenu *           (*get_options_menu) (DonnaColumnType    *ct,
                                             gpointer            data);
    gboolean            (*can_edit)         (DonnaColumnType    *ct,
                                             gpointer            data,
                                             DonnaNode          *node,
                                             GError            **error);
    gboolean            (*edit)             (DonnaColumnType    *ct,
                                             gpointer            data,
                                             DonnaNode          *node,
                                             GtkCellRenderer   **renderers,
                                             renderer_edit_fn    renderer_edit,
                                             gpointer            re_data,
                                             DonnaTreeView      *treeview,
                                             GError            **error);
    GPtrArray *         (*render)           (DonnaColumnType    *ct,
                                             gpointer            data,
                                             guint               index,
                                             DonnaNode          *node,
                                             GtkCellRenderer    *renderer);
    gboolean            (*set_tooltip)      (DonnaColumnType    *ct,
                                             gpointer            data,
                                             guint               index,
                                             DonnaNode          *node,
                                             GtkTooltip         *tooltip);
    gint                (*node_cmp)         (DonnaColumnType    *ct,
                                             gpointer            data,
                                             DonnaNode          *node1,
                                             DonnaNode          *node2);
    gboolean            (*is_match_filter)  (DonnaColumnType    *ct,
                                             const gchar        *filter,
                                             gpointer           *filter_data,
                                             gpointer            data,
                                             DonnaNode          *node,
                                             GError            **error);
    void                (*free_filter_data) (DonnaColumnType    *ct,
                                             gpointer            filter_data);
};

const gchar *   donna_columntype_get_name       (DonnaColumnType    *ct);
const gchar *   donna_columntype_get_renderers  (DonnaColumnType    *ct);
DonnaColumnTypeNeed donna_columntype_refresh_data (DonnaColumnType  *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 const gchar        *arr_name,
                                                 gpointer           *data);
void            donna_columntype_free_data      (DonnaColumnType    *ct,
                                                 gpointer            data);
GPtrArray *     donna_columntype_get_props      (DonnaColumnType    *ct,
                                                 gpointer            data);
GtkSortType     donna_columntype_get_default_sort_order
                                                (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 const gchar        *arr_name,
                                                 gpointer            data);
GtkMenu *       donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                                 gpointer            data);
gboolean        donna_columntype_can_edit       (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 DonnaNode          *node,
                                                 GError            **error);
gboolean        donna_columntype_edit           (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 DonnaNode          *node,
                                                 GtkCellRenderer   **renderers,
                                                 renderer_edit_fn    renderer_edit,
                                                 gpointer            re_data,
                                                 DonnaTreeView      *treeview,
                                                 GError            **error);
GPtrArray *     donna_columntype_render         (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 guint               index,
                                                 DonnaNode          *node,
                                                 GtkCellRenderer    *renderer);
gboolean        donna_columntype_set_tooltip    (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 guint               index,
                                                 DonnaNode          *node,
                                                 GtkTooltip         *tooltip);
gint            donna_columntype_node_cmp       (DonnaColumnType    *ct,
                                                 gpointer            data,
                                                 DonnaNode          *node1,
                                                 DonnaNode          *node2);
gboolean        donna_columntype_is_match_filter(DonnaColumnType    *ct,
                                                 const gchar        *filter,
                                                 gpointer           *filter_data,
                                                 gpointer            data,
                                                 DonnaNode          *node,
                                                 GError            **error);
void            donna_columntype_free_filter_data(DonnaColumnType   *ct,
                                                 gpointer            filter_data);


inline GtkWindow * donna_columntype_new_floating_window (
                                                 DonnaTreeView      *tree,
                                                 gboolean            destroy_on_sel_changed);

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_H__ */
