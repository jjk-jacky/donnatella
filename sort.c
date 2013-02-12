
#include <glib.h>
#include <string.h>     /* strlen() */
#include "sort.h"

#define COLLATION_SENTINEL  "\1\1\1"

gchar *
utf8_collate_key (const gchar   *str,
                  gssize         len,
                  gboolean       dot_first,
                  gboolean       special_first,
                  gboolean       natural_order)
{
    GString *result;
    GString *append;
    const gchar *p;
    const gchar *prev;
    const gchar *end;
    gchar *collate_key;
    gint digits;
    gint leading_zeros;

    if (len < 0)
        len = strlen (str);

    result = g_string_sized_new (len * 2);
    append = g_string_sized_new (0);

    end = str + len;
    p = str;

    if (special_first)
    {
        const gchar *s = str;
        gboolean prefix = FALSE;

        while ((s = g_utf8_find_next_char (s, end)))
        {
            gunichar c;

            c = g_utf8_get_char (s);
            if (!g_unichar_isalnum (c))
            {
                if (!prefix)
                    prefix = TRUE;
            }
            else
            {
                if (prefix)
                {
                    /* adding the string itself and not a collate_key
                     * so that ! comes before - */
                    g_string_append_len (result, str, s - str);
                    g_string_append (result, COLLATION_SENTINEL "\1");
                    p += s - str;
                }
                break;
            }
        }
    }

    /* No need to use utf8 functions, since we're only looking for ascii chars */
    for (prev = p; p < end; ++p)
    {
        switch (*p)
        {
            case '.':
                if (!dot_first && p == str)
                    break;

                if (prev != p)
                {
                    collate_key = g_utf8_collate_key (prev, p - prev);
                    g_string_append (result, collate_key);
                    g_free (collate_key);
                }

                g_string_append (result, COLLATION_SENTINEL "\1");

                /* skip the dot */
                prev = p + 1;
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (!natural_order)
                    break;

                if (prev != p)
                {
                    collate_key = g_utf8_collate_key (prev, p - prev);
                    g_string_append (result, collate_key);
                    g_free (collate_key);
                }

                g_string_append (result, COLLATION_SENTINEL "\2");

                prev = p;

                /* write d-1 colons */
                if (*p == '0')
                {
                    leading_zeros = 1;
                    digits = 0;
                }
                else
                {
                    leading_zeros = 0;
                    digits = 1;
                }

                while (++p < end)
                {
                    if (*p == '0' && !digits)
                        ++leading_zeros;
                    else if (g_ascii_isdigit (*p))
                        ++digits;
                    else
                    {
                        /* count an all-zero sequence as one digit plus leading
                         * zeros */
                        if (!digits)
                        {
                            ++digits;
                            --leading_zeros;
                        }
                        break;
                    }
                }

                while (digits > 1)
                {
                    g_string_append_c (result, ':');
                    --digits;
                }

                if (leading_zeros > 0)
                {
                    g_string_append_c (append, (char) leading_zeros);
                    prev += leading_zeros;
                }

                /* write the number itself */
                g_string_append_len (result, prev, p - prev);

                prev = p;
                --p; /* go one step back to avoid disturbing outer loop */
                break;

            default:
                /* other characters just accumulate */
                break;
        }
    }

    if (prev != p)
    {
        collate_key = g_utf8_collate_key (prev, p - prev);
        g_string_append (result, collate_key);
        g_free (collate_key);
    }

    g_string_append (result, append->str);
    g_string_free (append, TRUE);

    return g_string_free (result, FALSE);
}
