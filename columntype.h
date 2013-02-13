
#ifndef __DONNA_COLUMNTYPE_H__
#define __DONNA_COLUMNTYPE_H__

#include <gtk/gtk.h>
#include "common.h"

G_BEGIN_DECLS

typedef struct _DonnaColumnType             DonnaColumnType; /* dummy typedef */
typedef struct _DonnaColumnTypeInterface    DonnaColumnTypeInterface;

#define DONNA_TYPE_COLUMNTYPE               (donna_columntype_get_type ())
#define DONNA_COLUMNTYPE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMNTYPE, DonnaColumnType))
#define DONNA_IS_COLUMNTYPE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMNTYPE))
#define DONNA_COLUMNTYPE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_COLUMNTYPE, DonnaColumnTypeInterface))

GType           donna_columntype_get_type   (void) G_GNUC_CONST;

typedef struct
{
    gchar           type;
    gpointer        data;
    GDestroyNotify  destroy;
} DonnaRenderer;

#define DONNA_COLUMNTYPE_RENDERER_TEXT      't'
#define DONNA_COLUMNTYPE_RENDERER_PIXBUF    'p'
#define DONNA_COLUMNTYPE_RENDERER_PROGRESS  'P'
#define DONNA_COLUMNTYPE_RENDERER_COMBO     'c'
#define DONNA_COLUMNTYPE_RENDERER_TOGGLE    'T'

struct _DonnaColumnTypeInterface
{
    GTypeInterface parent;

    /* virtual table */
    gint                (*get_renderers)    (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             DonnaRenderer     **renderers);
    void                (*render)           (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             DonnaNode          *node,
                                             gpointer            data,
                                             GtkCellRenderer    *renderer);
    GtkMenu *           (*get_options_menu) (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name);
    gboolean            (*handle_context)   (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             DonnaNode          *node);
    gboolean            (*set_tooltip)      (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             DonnaNode          *node,
                                             gpointer            data,
                                             GtkTooltip         *tooltip);
    gint                (*node_cmp)         (DonnaColumnType    *ct,
                                             const gchar        *tv_name,
                                             const gchar        *col_name,
                                             DonnaNode          *node1,
                                             DonnaNode          *node2);
};

gint            donna_columntype_get_renderers  (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 DonnaRenderer     **renderers);
void            donna_columntype_render         (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 DonnaNode          *node,
                                                 gpointer            data,
                                                 GtkCellRenderer    *renderer);
GtkMenu *       donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name);
gboolean        donna_columntype_handle_context (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 DonnaNode          *node);
gboolean        donna_columntype_set_tooltip    (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 DonnaNode          *node,
                                                 gpointer            data,
                                                 GtkTooltip         *tooltip);
gint            donna_columntype_node_cmp       (DonnaColumnType    *ct,
                                                 const gchar        *tv_name,
                                                 const gchar        *col_name,
                                                 DonnaNode          *node1,
                                                 DonnaNode          *node2);

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_H__ */
