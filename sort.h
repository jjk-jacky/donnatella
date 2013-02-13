
#ifndef __DONNA_SORT_H__
#define __DONNA_SORT_H__

#include <glib.h>

G_BEGIN_DECLS

gchar
get_options_char (gboolean dot_first,
                  gboolean special_first,
                  gboolean natural_order);

gchar *
utf8_collate_key (const gchar   *str,
                  gssize         len,
                  gboolean       dot_first,
                  gboolean       special_first,
                  gboolean       natural_order);

G_END_DECLS

#endif /* __DONNA_SORT_H__ */
