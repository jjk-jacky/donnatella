
#include <glib.h>
#include <string.h>     /* strlen() */
#include <stdlib.h>     /* mbstowcs() */
#include <wctype.h>     /* iswdigit(), iswspace(), iswpunct() */
#include <wchar.h>      /* wcstoul() */
#include <errno.h>
#include "sort.h"

static wchar_t *
_mbstowcs (wchar_t *dest, const gchar *src)
{
    size_t len;

    len = mbstowcs (dest, src, 255);
    if (len == (size_t) -1)
        /* invalid mb sequence */
        return NULL;
    else if (len >= 255)
    {
        /* we need more than 255 wide chars */
        len = mbstowcs (NULL, src, 0);
        dest = g_new (wchar_t, len + 1);
        len = mbstowcs (dest, src, len);
        if (len == (size_t) -1)
        {
            /* invalid mb seq */
            g_free (dest);
            return NULL;
        }
    }

    return dest;
}

gint
strcmp_ext (const gchar *s1, const gchar *s2, DonnaSortOptions options)
{
    wchar_t  wb1[256], *_ws1 = wb1;
    wchar_t  wb2[256], *_ws2 = wb2;
    wchar_t  *ws1, *ws2;
    gboolean is_string = TRUE;
    gint     res_fb = 0; /* fallback */
    gint     res_cs = 0; /* case-sensitive */
    gint     res = 0;

    /* if at least one string if NULL or empty, we have a result */
    if (!s1 || *s1 == '\0')
    {
        if (s2 && *s2 != '\0')
            return -1;
        else
            return 0;
    }
    else if (!s2 || *s2 == '\0')
        return 1;

    if (options & DONNA_SORT_DOT_FIRST)
    {
        if (*s1 == '.')
        {
            if (*s2 != '.')
                /* only s1 is dotted, it comes first */
                return -1;
            else
            {
                /* both are dotted, skip the dot */
                ++s1;
                ++s2;
            }
        }
        else if (*s2 == '.')
            /* only s2 is dotted, it comes first */
            return 1;
    }
    else if (options & DONNA_SORT_DOT_MIXED)
    {
        if (*s1 == '.')
            ++s1;
        if (*s2 == '.')
            ++s2;
    }

    /* to continue we'll need the wide-char strings */
    _ws1 = _mbstowcs (wb1, s1);
    if (!_ws1)
        /* convertion failed, fallback to strcmp() */
        return strcmp (s1, s2);
    _ws2 = _mbstowcs (wb2, s2);
    if (!_ws2)
    {
        /* convertion failed, fallback to strcmp() */
        if (_ws1 != wb1)
            g_free (_ws1);
        return strcmp (s1, s2);
    }

    ws1 = _ws1;
    ws2 = _ws2;
    for (;;)
    {
        wint_t wc1, wc2;

        wc1 = *ws1;
        wc2 = *ws2;

        if (is_string)
        {
            /* is at least one string over? */
            if (wc1 == L'\0')
            {
                if (wc2 == L'\0')
                    /* looks like strings are the same. We fallback to case
                     * sensitive result, so in case insensitive mode we still
                     * have an order */
                    res = res_cs;
                else
                    /* shorter first */
                    res = -1;
                goto done;
            }
            else if (wc2 == L'\0')
            {
                /* shorter first */
                res = 1;
                goto done;
            }

            if (options & DONNA_SORT_IGNORE_SPUNCT)
            {
                while (iswspace (wc1) || iswpunct (wc1))
                    wc1 = *++ws1;
                while (iswspace (wc2) || iswpunct (wc2))
                    wc2 = *++ws2;
            }

            /* is at least one string a number? */
            if (iswdigit (wc1))
            {
                if (iswdigit (wc2))
                {
                    if (options & DONNA_SORT_NATURAL_ORDER)
                    {
                        /* switch to number comparison */
                        is_string = FALSE;
                        continue;
                    }
                }
                else
                {
                    /* number first */
                    res = -1;
                    goto done;
                }
            }
            else if (iswdigit (wc2))
            {
                /* number first */
                res = 1;
                goto done;
            }

            /* compare chars */
            if (wc1 > wc2)
                res_cs = 1;
            else if (wc1 < wc2)
                res_cs = -1;

            if (options & DONNA_SORT_CASE_INSENSITIVE)
            {
                /* compare uppper chars */
                wc1 = towupper (wc1);
                wc2 = towupper (wc2);

                if (wc1 > wc2)
                {
                    res = 1;
                    goto done;
                }
                else if (wc1 < wc2)
                {
                    res = -1;
                    goto done;
                }
            }
            /* do we have a res_cs yet? */
            else if (res_cs != 0)
            {
                res = res_cs;
                goto done;
            }

            /* next chars */
            ++ws1;
            ++ws2;
        }
        /* mode number */
        else
        {
            unsigned long n1, n2;

            if (res_fb == 0)
            {
                /* count number of leading zeros */
                for (n1 = 0; ws1[n1] == L'0'; ++n1)
                    ;
                for (n2 = 0; ws2[n2] == L'0'; ++n2)
                    ;
                /* try to set a fallback to put less leading zeros first */
                if (n1 > n2)
                    res_fb = 1;
                else if (n1 < n2)
                    res_fb = -1;
            }

            errno = 0;
            n1 = wcstoul (ws1, &ws1, 10);
            if (errno != 0)
            {
                /* failed to get number -- fallback to strcmp() */
                res = strcmp (s1, s2);
                goto done;
            }
            n2 = wcstoul (ws2, &ws2, 10);
            if (errno != 0)
            {
                /* failed to get number -- fallback to strcmp() */
                res = strcmp (s1, s2);
                goto done;
            }

            if (n1 > n2)
            {
                res = 1;
                goto done;
            }
            else if (n1 < n2)
            {
                res = -1;
                goto done;
            }

            /* back to string comparison */
            is_string = TRUE;
        }
    }

done:
    if (_ws1 != wb1)
        g_free (_ws1);
    if (_ws2 != wb2)
        g_free (_ws2);
    return (res != 0) ? res : res_fb;
}

enum
{
    SORT_DOT_FIRST      = (1 << 0),
    SORT_SPECIAL_FIRST  = (1 << 1),
    SORT_NATURAL_ORDER  = (1 << 2),
};

#define COLLATION_SENTINEL  "\1\1\1"

gchar
sort_get_options_char (gboolean dot_first,
                       gboolean special_first,
                       gboolean natural_order)
{
    gchar c;

    c = 0;
    if (dot_first)
        c |= SORT_DOT_FIRST;
    if (special_first)
        c |= SORT_SPECIAL_FIRST;
    if (natural_order)
        c |= SORT_NATURAL_ORDER;

    return c;
}

gchar *
sort_get_utf8_collate_key (const gchar   *str,
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
    gchar c;
    gint digits;
    gint leading_zeros;

    if (len < 0)
        len = strlen (str);

    result = g_string_sized_new (len * 2);
    append = g_string_sized_new (0);

    end = str + len;
    p = str;

    /* store a character so we can check/invalidate the key if options change */
    c = sort_get_options_char (dot_first, special_first, natural_order);
    g_string_append_c (result, c);

    if (special_first)
    {
        const gchar *s = str;
        gboolean prefix = FALSE;

        for ( ; s < end; s = g_utf8_next_char (s))
        {
            gunichar c;

            c = g_utf8_get_char (s);
            if (!g_unichar_isalnum (c))
            {
                if (!prefix && *s != '.')
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
