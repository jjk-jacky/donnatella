
#include "util.h"

gchar *
donna_print_time (time_t ts, const gchar *fmt)
{
    GDateTime *dt;
    gchar *s;

    dt = g_date_time_new_from_unix_local (ts);
    s = g_date_time_format (dt, fmt);
    g_date_time_unref (dt);
    return s;
}

GValue *
duplicate_gvalue (const GValue *src)
{
    GValue *dst;

    dst = g_slice_new0 (GValue);
    g_value_init (dst, G_VALUE_TYPE (src));
    g_value_copy (src, dst);

    return dst;
}
