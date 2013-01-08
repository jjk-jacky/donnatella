
#include "util.h"

GValue *
duplicate_gvalue (const GValue *src)
{
    GValue *dst;

    dst = g_slice_new0 (GValue);
    g_value_init (dst, G_VALUE_TYPE (src));
    g_value_copy (src, dst);

    return dst;
}
