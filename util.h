
#ifndef __DONNA_UTIL_H__
#define __DONNA_UTIL_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

gchar *         donna_print_time            (time_t ts, const gchar *fmt);
GValue *        duplicate_gvalue            (const GValue *src);

G_END_DECLS

#endif /* __DONNA_UTIL_H__ */
