
#ifndef __DONNA_RENDERER_H__
#define __DONNA_RENDERER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

void            donna_renderer_set              (GtkCellRenderer    *renderer,
                                                 const gchar        *first_prop,
                                                 ...);

G_END_DECLS

#endif /* __DONNA_RENDERER_H__ */
