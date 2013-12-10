
#ifndef __DONNA_CELL_RENDERER_TEXT_H__
#define __DONNA_CELL_RENDERER_TEXT_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _DonnaCellRendererText           DonnaCellRendererText;
typedef struct _DonnaCellRendererTextPrivate    DonnaCellRendererTextPrivate;
typedef struct _DonnaCellRendererTextClass      DonnaCellRendererTextClass;

#define DONNA_TYPE_CELL_RENDERER_TEXT           (donna_cell_renderer_text_get_type ())
#define DONNA_CELL_RENDERER_TEXT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_CELL_RENDERER_TEXT, DonnaCellRendererText))
#define DONNA_CELL_RENDERER_TEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_CELL_RENDERER_TEXT, DonnaCellRendererTextClass))
#define DONNA_IS_CELL_RENDERER_TEXT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_CELL_RENDERER_TEXT))
#define DONNA_IS_CELL_RENDERER_TEXT_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_CELL_RENDERER_TEXT))
#define DONNA_CELL_RENDERER_TEXT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_CELL_RENDERER_TEXT, DonnaCellRendererTextClass))

GType   donna_cell_renderer_text_get_type       (void) G_GNUC_CONST;

struct _DonnaCellRendererText
{
    /*< private >*/
    GtkCellRendererText              renderer;
    DonnaCellRendererTextPrivate    *priv;
};

struct _DonnaCellRendererTextClass
{
    GtkCellRendererTextClass parent_class;
};

GtkCellRenderer *   donna_cell_renderer_text_new    (void);

G_END_DECLS

#endif /* __DONNA_CELL_RENDERER_TEXT_H__ */
