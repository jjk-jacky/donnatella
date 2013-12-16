
#ifndef __DONNA_UTIL_H__
#define __DONNA_UTIL_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct
{
    guint        age_span_seconds;
    const gchar *age_fallback_format;
} DonnaTimeOptions;

gsize           donna_print_size                (gchar              *str,
                                                 gsize               max,
                                                 const gchar        *fmt,
                                                 guint64             size,
                                                 gint                digits,
                                                 gboolean            long_unit);
gchar *         donna_print_time                (guint64             ts,
                                                 const gchar        *fmt,
                                                 DonnaTimeOptions   *options);
GValue *        duplicate_gvalue                (const GValue       *src);
void            donna_g_string_append_quoted    (GString            *str,
                                                 const gchar        *s,
                                                 gboolean            double_percent);
void            donna_g_string_append_concat    (GString            *str,
                                                 const gchar        *string,
                                                 ...);
inline void     donna_g_object_unref            (gpointer            object);
GSource *       donna_fd_source_new             (gint                fd,
                                                 GSourceFunc         callback,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);
guint           donna_fd_add_source             (gint                fd,
                                                 GSourceFunc         callback,
                                                 gpointer            data,
                                                 GDestroyNotify      destroy);

G_END_DECLS

#endif /* __DONNA_UTIL_H__ */
