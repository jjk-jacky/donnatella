
#ifndef __DONNA_SIZE_H__
#define __DONNA_SIZE_H__

#include <glib.h>

G_BEGIN_DECLS

gssize
donna_print_size (gchar         *str,
                  gssize         max,
                  const gchar   *fmt,
                  guint64        size,
                  gint           digits,
                  gboolean       long_unit);

G_END_DECLS

#endif /* __DONNA_SIZE_H__ */
