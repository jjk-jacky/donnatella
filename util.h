
#ifndef __DONNA_UTIL_H__
#define __DONNA_UTIL_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct
{
    guint        age_span_seconds;
    const gchar *age_fallback_fmt;
} DonnaTimeOptions;

gchar *         donna_print_time                (guint64             ts,
                                                 const gchar        *fmt,
                                                 DonnaTimeOptions   *options);
GValue *        duplicate_gvalue                (const GValue       *src);
void            donna_g_string_append_quoted    (GString            *str,
                                                 const gchar        *s);
inline void     donna_g_object_unref            (gpointer            object);

G_END_DECLS

#endif /* __DONNA_UTIL_H__ */
