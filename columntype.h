
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

struct _DonnaColumnTypeInterface
{
    GTypeInterface parent;

    /* virtual table */
    const gchar *       (*get_renderers)    (DonnaColumnType    *ct);
    GPtrArray *         (*get_props)        (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name);
    gpointer            (*get_data)         (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name);
    DonnaColumnTypeNeed (*refresh_data)     (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             gpointer            data);
    void                (*free_data)        (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             gpointer            data);
    GPtrArray *         (*render)           (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             gpointer            data,
                                             guint               index,
                                             DonnaNode          *node,
                                             GtkCellRenderer    *renderer);
    gint                (*node_cmp)         (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             gpointer            data,
                                             DonnaNode          *node1,
                                             DonnaNode          *node2);
    GtkMenu *           (*get_options_menu) (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name);
    gboolean            (*handle_context)   (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             DonnaNode          *node,
                                             DonnaTreeView      *treeview);
    gboolean            (*set_tooltip)      (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             gpointer            data,
                                             guint               index,
                                             DonnaNode          *node,
                                             GtkTooltip         *tooltip);
};

const gchar *   donna_columntype_get_renderers  (DonnaColumnType    *ct);
GPtrArray *     donna_columntype_get_props      (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name);
gpointer        donna_columntype_get_data       (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name);
DonnaColumnTypeNeed donna_columntype_refresh_data (DonnaColumnType  *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 gpointer            data);
void            donna_columntype_free_data      (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 gpointer            data);
GPtrArray *     donna_columntype_render         (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 gpointer            data,
                                                 guint               index,
                                                 DonnaNode          *node,
                                                 GtkCellRenderer    *renderer);
gint            donna_columntype_node_cmp       (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 gpointer            data,
                                                 DonnaNode          *node1,
                                                 DonnaNode          *node2);
GtkMenu *       donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name);
gboolean        donna_columntype_handle_context (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 DonnaNode          *node,
                                                 DonnaTreeView      *treeview);
gboolean        donna_columntype_set_tooltip    (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 gpointer            data,
                                                 guint               index,
                                                 DonnaNode          *node,
                                                 GtkTooltip         *tooltip);

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_H__ */
