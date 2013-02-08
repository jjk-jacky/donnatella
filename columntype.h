
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

/* name of the "fake" groups in which options will be placed in data, to allow
 * easy parsing using g_key_file_* API */
#define DONNA_COLUMNTYPE_OPTIONS_GROUP      "options"

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
    gpointer            (*parse_options)    (DonnaColumnType    *ct,
                                             gchar              *data);
    void                (*free_options)     (DonnaColumnType    *ct,
                                             gpointer            options);
    gint                (*get_renderers)    (DonnaColumnType    *ct,
                                             gpointer            options,
                                             DonnaRenderer     **renderers);
    void                (*render)           (DonnaColumnType    *ct,
                                             gpointer            options,
                                             DonnaNode          *node,
                                             gpointer            data,
                                             GtkCellRenderer    *renderer);
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
void            donna_columntype_free_options   (DonnaColumnType    *ct,
                                                 gpointer            options);
gint            donna_columntype_get_renderers  (DonnaColumnType    *ct,
                                                 gpointer            options,
                                                 DonnaRenderer     **renderers);
void            donna_columntype_render         (DonnaColumnType    *ct,
                                                 gpointer            options,
                                                 DonnaNode          *node,
                                                 gpointer            data,
                                                 GtkCellRenderer    *renderer);
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
