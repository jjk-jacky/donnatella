
#ifndef __DONNA_SORT_H__
#define __DONNA_SORT_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
    DONNA_SORT_NATURAL_ORDER    = (1 << 0),
    DONNA_SORT_CASE_INSENSITIVE = (1 << 1),
    DONNA_SORT_DOT_FIRST        = (1 << 2),
    DONNA_SORT_DOT_MIXED        = (1 << 3),
    DONNA_SORT_IGNORE_SPUNCT    = (1 << 4),
} DonnaSortOptions;

gint
donna_strcmp (const gchar *s1, const gchar *s2, DonnaSortOptions options);

gchar
donna_sort_get_options_char (gboolean dot_first,
                             gboolean special_first,
                             gboolean natural_order);

gchar *
donna_sort_get_utf8_collate_key (const gchar   *str,
                                 gssize         len,
                                 gboolean       dot_first,
                                 gboolean       special_first,
                                 gboolean       natural_order);

G_END_DECLS

#endif /* __DONNA_SORT_H__ */
