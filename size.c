
#include <glib.h>
#include <stdio.h>
#include "size.h"

gssize
donna_print_size (gchar       *str,
                  gssize       max,
                  const gchar *fmt,
                  off_t        size,
                  gint         digits,
                  gboolean     long_unit)
{
    const gchar *s_unit[] = { "B", "K",   "M",   "G",   "T"   };
    const gchar *l_unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    const gchar **unit = (long_unit) ? l_unit : s_unit;
    gint nb_unit = sizeof (s_unit) / sizeof (s_unit[0]);
    gint u = 0;
    gssize need;
    gssize total = 0;

    while (*fmt != '\0')
    {
        if (*fmt == '%')
        {
            gdouble dbl;

            switch (fmt[1])
            {
                case 'r':
                    need = snprintf (str, max, "%li", size);
                    break;
                case 'b':
                    need = snprintf (str, max, "%'li", size);
                    break;
                case 'B':
                    need = snprintf (str, max, "%'li %s", size, unit[0]);
                    break;
                case 'k':
                    dbl = (gdouble) size / 1024.0;
                    need = snprintf (str, max, "%'.*lf", digits, dbl);
                    break;
                case 'K':
                    dbl = (gdouble) size / 1024.0;
                    need = snprintf (str, max, "%'.*lf %s", digits, dbl, unit[1]);
                    break;
                case 'm':
                    dbl = (gdouble) size / (1024.0 * 1024.0);
                    need = snprintf (str, max, "%'.*lf", digits, dbl);
                    break;
                case 'M':
                    dbl = (gdouble) size / (1024.0 * 1024.0);
                    need = snprintf (str, max, "%'.*lf %s", digits, dbl, unit[2]);
                    break;
                case 'R':
                    dbl = (gdouble) size;
                    u = 0;
                    while (dbl > 1024.0)
                    {
                        if (++u >= nb_unit)
                            break;
                        dbl /= 1024.0;
                    }
                    need = snprintf (str, max, "%'.*lf %s",
                            (u > 0) ? digits : 0, dbl, unit[u]);
                    break;
                default:
                    need = 0;
                    break;
            }
            /* it was a known modifier */
            if (need > 0)
            {
                if (need < max)
                {
                    max -= need;
                    str += need;
                }
                else
                    max = 0;
                fmt += 2;
                total += need;
                continue;
            }
        }

        /* we keep one more for NUL */
        if (max >= 2)
        {
            *str++ = *fmt++;
            --max;
        }
        else
            ++fmt;
        total += 1;
    }
    if (max > 0)
        *str = '\0';
    return total;
}
