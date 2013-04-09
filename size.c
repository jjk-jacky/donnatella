
#include <glib.h>
#include <stdio.h>
#include "size.h"

void
donna_print_size (gchar         **str,
                  gssize          max,
                  off_t           size,
                  DonnaSizeFormat format,
                  gint            digits)
{
    gdouble dbl;

    g_return_if_fail (str != NULL);

    switch (format)
    {
        case DONNA_SIZE_FORMAT_RAW:
            if (snprintf (*str, max, "%li", size) >= max)
                *str = g_strdup_printf ("%li", size);
            break;

        case DONNA_SIZE_FORMAT_B_NO_UNIT:
            if (snprintf (*str, max, "%'li", size) >= max)
                *str = g_strdup_printf ("%'li", size);
            break;

        case DONNA_SIZE_FORMAT_B:
            if (snprintf (*str, max, "%'li B", size) >= max)
                *str = g_strdup_printf ("%'li B", size);
            break;

        case DONNA_SIZE_FORMAT_KB:
            dbl = (gdouble) size / 1024.0;
            if (snprintf (*str, max, "%'.*lf K", digits, dbl) >= max)
                *str = g_strdup_printf ("%'.*lf K", digits, dbl);
            break;

        case DONNA_SIZE_FORMAT_MB:
            dbl = (gdouble) size / (1024.0 * 1024.0);
            if (snprintf (*str, max, "%'.*lf M", digits, dbl) >= max)
                *str = g_strdup_printf ("%'.*lf M", digits, dbl);
            break;

        case DONNA_SIZE_FORMAT_ROUNDED:
            {
                const gchar unit[] = { 'B', 'K', 'M', 'G', 'T' };
                gint u = 0;
                gint max = sizeof (unit) / sizeof (unit[0]);

                dbl = (gdouble) size;
                while (dbl > 1024.0)
                {
                    if (++u >= max)
                        break;
                    dbl /= 1024.0;
                }
                if (snprintf (*str, max, "%'.*lf %c",
                            (u > 0) ? digits : 0, dbl, unit[u]) >= max)
                    *str = g_strdup_printf ("%'.*lf %c",
                            (u > 0) ? digits : 0, dbl, unit[u]);
                break;
            }
    }
}
