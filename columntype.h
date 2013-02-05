
#ifndef __DONNA_COLUMNTYPE_H__
#define __DONNA_COLUMNTYPE_H__

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
    gchar           *type;
    gpointer         data;
    GDestroyNotify   destroy;
} DonnaRenderer;

struct _DonnaColumnTypeInterface
{
    GTypeInterface parent;

    /* virtual table */
    gpointer            (*parse_options)    (DonnaColumnType    *ct,
                                             gchar              *data);
    DonnaRenderer **    (*get_renderers)    (DonnaColumnType    *ct,
                                             gpointer            options);
    void                (*render)           (DonnaColumnType    *ct,
                                             gpointer            options,
                                             DonnaNode          *node,
                                             GtkCellRenderer    *renderer,
                                             gpointer            data);
    GtkMenu *           (*get_options_menu) (DonnaColumnType    *ct,
                                             gpointer            options);
    gboolean            (*handle_context)   (DonnaColumnType    *ct,
                                             gpointer            options,
                                             DonnaNode          *node);
    gboolean            (*set_tooltip)      (DonnaColumnType    *ct,
                                             gpointer            options,
                                             DonnaNode          *node,
                                             gpointer            data,
                                             GtkTooltip         *tooltip);
    gint                (*node_cmp)         (DonnaColumnType    *ct,
                                             gpointer            options,
                                             DonnaNode          *node1,
                                             DonnaNode          *node2);
};

gpointer        donna_columntype_parse_options  (DonnaColumnType    *ct,
                                                 gchar              *data);
DonnaRenderer **donna_columntype_get_renderers  (DonnaColumnType    *ct,
                                                 gpointer            options);
void            donna_columntype_render         (DonnaColumnType    *ct,
                                                 gpointer            options,
                                                 DonnaNode          *node,
                                                 GtkCellRenderer    *renderer,
                                                 gpointer            data);
GtkMenu *       donna_columntype_get_options_menu (DonnaColumnType  *ct,
                                                 gpointer            options);
gboolean        donna_columntype_handle_context (DonnaColumnType    *ct,
                                                 gpointer            options,
                                                 DonnaNode          *node);
gboolean        donna_columntype_set_tooltip    (DonnaColumnType    *ct,
                                                 gpointer            options,
                                                 DonnaNode          *node,
                                                 gpointer            data,
                                                 GtkTooltip         *tooltip);
gint            donna_columntype_node_cmp       (DonnaColumnType    *ct,
                                                 gpointer            options,
                                                 DonnaNode          *node1,
                                                 DonnaNode          *node2);

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_H__ */
