/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * util.c
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "util.h"

gsize
donna_print_size (gchar       *str,
                  gsize        max,
                  const gchar *fmt,
                  guint64      size,
                  gint         digits,
                  gboolean     long_unit)
{
    const gchar *s_unit[] = { "B", "K",   "M",   "G",   "T"   };
    const gchar *l_unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    const gchar **unit = (long_unit) ? l_unit : s_unit;
    gint nb_unit = sizeof (s_unit) / sizeof (s_unit[0]);
    gint u = 0;
    gssize need;
    gsize total = 0;

    while (*fmt != '\0')
    {
        if (*fmt == '%')
        {
            gdouble dbl;

            switch (fmt[1])
            {
                case 'r':
                    need = snprintf (str, max, "%" G_GUINT64_FORMAT, size);
                    break;
                case 'b':
                    need = snprintf (str, max, "%'" G_GUINT64_FORMAT, size);
                    break;
                case 'B':
                    need = snprintf (str, max, "%'" G_GUINT64_FORMAT " %s", size, unit[0]);
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
                case '%':
                    need = 1;
                    if (max > 0)
                        *str = '%';
                    break;
                default:
                    need = 0;
                    break;
            }
            /* it was a known modifier */
            if (need > 0)
            {
                if ((gsize) need < max)
                {
                    max -= (gsize) need;
                    str += (gsize) need;
                }
                else
                    max = 0;
                fmt += 2;
                total += (gsize) need;
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

static inline gint
get_nb_from_dates (GDateTime **d1,
                   GDateTime  *d2,
                   GDateTime  *(add_fn) (GDateTime *, gint))
{
    GDateTime *_d1, *_d;
    gint nb = 0;

    _d = *d1;
    for (;;)
    {
        _d1 = add_fn (_d, 1);
        if (g_date_time_compare (_d1, d2) != -1)
        {
            g_date_time_unref (_d1);
            *d1 = _d;
            break;
        }
        ++nb;
        g_date_time_unref (_d);
        _d = _d1;
    }
    return nb;
}

gchar *
donna_print_time (guint64 ts, const gchar *fmt, DonnaTimeOptions *options)
{
    GDateTime *now = NULL;
    GDateTime *dt;
    gchar *ret;
    const gchar *f = fmt;
    gchar *s;
    gchar *age = NULL;
    gsize l = 0;
    gchar *tmp = NULL;
    gchar *t;
    gsize  alloc = 0;
    gsize  avail = 0;
    gsize  extra = 0;

    dt = g_date_time_new_from_unix_local ((time_t) ts);
    while ((s = strchr (f, '%')))
    {
        if (s[1] == 'f')
        {
            gint day_dt, day_now;
            gchar buf[32], *b = buf;
            const gchar *time_fmt;
            const gchar *date_fmt;

            if (!now)
                now = g_date_time_new_now_local ();

            if (options->fluid_time_format)
                time_fmt = options->fluid_time_format;
            else
                time_fmt = "%X";

            if (options->fluid_date_format)
                date_fmt = options->fluid_date_format;
            else
                date_fmt = "%x";

            if (g_date_time_get_year (dt) == g_date_time_get_year (now))
            {
                day_dt = g_date_time_get_day_of_year (dt);
                day_now = g_date_time_get_day_of_year (now);

                if (day_dt == day_now)
                {
                    /* same day: just time */
                    age = g_date_time_format (dt, time_fmt);
                    l = strlen (age);
                    goto age_done;
                }
            }
            else if (g_date_time_get_year (dt) == g_date_time_get_year (now) - 1)
            {
                gint year;

                /* dt is from the year before, but it might also be e.g. the day
                 * before */

                day_dt = g_date_time_get_day_of_year (dt);
                day_now = g_date_time_get_day_of_year (now);

                /* make day_now in the "same year" as day_dt */
                year = g_date_time_get_year (dt);
                /* leap year? */
                if (((year % 4) == 0) && (!(((year % 100) == 0) && ((year % 400) != 0))))
                    day_now += 366;
                else
                    day_now += 365;
            }
            else
                day_now = -1;

            if (day_now > -1 && day_dt == day_now - 1)
            {
                /* yesterday */
                if (G_UNLIKELY (snprintf (buf, 32, "Yesterday %s", time_fmt) >= 32))
                    b = g_strconcat ("Yesterday ", time_fmt, NULL);
            }
            else if (day_now > -1 && day_dt > day_now - 7)
            {
                /* weekday */
                if (G_UNLIKELY (snprintf (buf, 32, "%%%c %s",
                                (options->fluid_short_weekday) ? 'a' : 'A',
                                time_fmt) >= 32))
                    b = g_strconcat (
                            (options->fluid_short_weekday) ? "%a " : "%A ",
                            time_fmt,
                            NULL);
            }
            else
            {
                /* full date time */
                if (G_UNLIKELY (snprintf (buf, 32, "%s", date_fmt) >= 32))
                    b = g_strdup (date_fmt);
            }

            age = g_date_time_format (dt, b);
            l = strlen (age);

            if (b != buf)
                g_free (b);
            goto age_done;
        }
        else if (s[1] == 'o' || s[1] == 'O')
        {
            if (!age)
            {
                GDateTime *d1, *d2;
                gint count = 0;
                gint nb[2];
                const gchar *unit[2];
                gint cmp;

                if (!now)
                    now = g_date_time_new_now_local ();
                cmp = g_date_time_compare (dt, now);
                if (cmp == 0)
                {
                    d1 = g_date_time_ref (dt);
                    d2 = g_date_time_ref (now);
                }
                else
                {
                    GTimeSpan timespan;
                    gint day1, month1, year1;
                    gint day2, month2, year2;
                    gint n;

                    if (cmp == -1)
                    {
                        d1 = g_date_time_ref (dt);
                        d2 = g_date_time_ref (now);
                    }
                    else
                    {
                        d1 = g_date_time_ref (now);
                        d2 = g_date_time_ref (dt);
                    }

                    if (s[1] == 'O' && options && options->age_fallback_format)
                    {
                        GDateTime *d;

                        d = g_date_time_add_seconds (d1, options->age_span_seconds);
                        if (g_date_time_compare (d, d2) == -1)
                        {
                            /* not in span, use fallback format */
                            age = g_date_time_format (dt, options->age_fallback_format);
                            l = strlen (age);
                            g_date_time_unref (d);
                            goto age_done;
                        }
                        g_date_time_unref (d);
                    }

                    g_date_time_get_ymd (d1, &year1, &month1, &day1);
                    g_date_time_get_ymd (d2, &year2, &month2, &day2);

                    /* more than a year? */
                    if (year2 > year1 + 1 || (year2 == year1 + 1
                                && (month2 > month1
                                    || (month2 == month1 && day2 > day1))))
                    {
                        n = get_nb_from_dates (&d1, d2, g_date_time_add_years);
                        if (n > 0)
                        {
                            nb[count] = n;
                            unit[count] = "y";
                            g_date_time_get_ymd (d1, &year1, &month1, &day1);
                            ++count;
                        }
                    }

                    /* more than a month? */
                    if ((month1 == 12 && (year2 == year1 + 1
                                    && (month2 > 1 || day2 > day1)))
                            || year2 > year1
                            || month2 > month1 + 1
                            || (month2 == month1 + 1 && day2 > day1))
                    {
                        n = get_nb_from_dates (&d1, d2, g_date_time_add_months);
                        if (n > 0)
                        {
                            nb[count] = n;
                            unit[count] = "M";
                            g_date_time_get_ymd (d1, &year1, &month1, &day1);
                            ++count;
                        }
                    }

                    if (count == 2)
                        goto printing;

                    /* week */
                    n = get_nb_from_dates (&d1, d2, g_date_time_add_weeks);
                    if (n > 0)
                    {
                        nb[count] = n;
                        unit[count] = "w";
                        g_date_time_get_ymd (d1, &year1, &month1, &day1);
                        ++count;
                    }

                    if (count == 2)
                        goto printing;

                    /* day */
                    n = get_nb_from_dates (&d1, d2, g_date_time_add_days);
                    if (n > 0)
                    {
                        nb[count] = n;
                        unit[count] = "d";
                        g_date_time_get_ymd (d1, &year1, &month1, &day1);
                        ++count;
                    }

                    if (count == 2)
                        goto printing;

                    /* from now on we can simply use timespan. We couldn't
                     * before because there aren't ones for years, months or
                     * weeks. Plus those, and days even with daylight savings,
                     * aren't a fixed amount of whatever.
                     * Hours/minutes OTOH... */

                    timespan = g_date_time_difference (d2, d1);

                    if (timespan >= G_TIME_SPAN_HOUR)
                    {
                        nb[count] = (gint) (timespan / G_TIME_SPAN_HOUR);
                        unit[count] = "h";
                        timespan -= (nb[count] * G_TIME_SPAN_HOUR);
                        ++count;
                    }

                    if (count == 2)
                        goto printing;

                    if (timespan >= G_TIME_SPAN_MINUTE)
                    {
                        nb[count] = (gint) (timespan / G_TIME_SPAN_MINUTE);
                        unit[count] = "m";
                        timespan -= (nb[count] * G_TIME_SPAN_MINUTE);
                        ++count;
                    }

                    if (count == 2)
                        goto printing;

                    if (timespan >= G_TIME_SPAN_SECOND)
                    {
                        nb[count] = (gint) (timespan / G_TIME_SPAN_SECOND);
                        unit[count] = "s";
                        ++count;
                    }
                }
printing:
                g_date_time_unref (d1);
                g_date_time_unref (d2);

                if (count == 0)
                    age = g_strdup_printf ("just now");
                else
                {
                    gint max = 255;
                    gchar buf[max];
                    gint need;

                    need = g_snprintf (buf, (gsize) max, "%d%s", nb[0], unit[0]);
                    if (need < 255 && count > 1)
                        need += g_snprintf (buf + need, (gsize) (max - need),
                                " %d%s", nb[1], unit[1]);
                    if (need < 255)
                    {
                        if (cmp == -1)
                            age = g_strdup_printf ("%s ago", buf);
                        else
                            age = g_strdup_printf ("in %s", buf);
                    }
                }
                l = strlen (age);
            }
age_done:

            if (avail <= l)
            {
                gboolean first = (alloc == 0);
                if (first)
                    alloc = strlen (fmt);
                alloc += l - avail + 255;
                avail += l - avail + 255;
                tmp = g_renew (gchar, tmp, alloc);
                if (first)
                    strcpy (tmp, fmt);
            }

            /* points to s but in tmp */
            t = tmp + extra + (s - fmt);
            /* move the part after to the right (i.e. make space for age) */
            if (G_LIKELY (l > 2))
                memmove (t + l, t + 2, strlen (t + 2) + 1 /* include NUL */);
            /* put age in */
            memcpy (t, age, l);
            /* update difference between tmp & fmt */
            extra += l - 2;
            /* update how much space is left in tmp */
            avail -= l - 2;
        }
        f = s + 1;
    }
    ret = g_date_time_format (dt, (tmp) ? tmp : fmt);
    if (tmp)
        g_free (tmp);
    if (age)
        g_free (age);
    g_date_time_unref (dt);
    if (now)
        g_date_time_unref (now);
    return ret;
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

gboolean
donna_g_ptr_array_contains (GPtrArray          *arr,
                            gpointer            value,
                            GCompareFunc        cmp)
{
    guint i;

    g_return_val_if_fail (arr != NULL, FALSE);

    for (i = 0; i < arr->len; ++i)
    {
        if (cmp)
        {
            if (cmp (arr->pdata[i], value) == 0)
                return TRUE;
        }
        else if (arr->pdata[i] == value)
            return TRUE;
    }

    return FALSE;
}

void
donna_g_string_append_quoted (GString            *str,
                              const gchar        *s,
                              gboolean            double_percent)
{
    g_return_if_fail (s != NULL);

    g_string_append_c (str, '"');
    for ( ; *s != '\0'; ++s)
    {
        if (*s == '"' || *s == '\\')
            g_string_append_c (str, '\\');
        else if (double_percent && *s == '%')
            g_string_append_c (str, '%');

        g_string_append_c (str, *s);
    }
    g_string_append_c (str, '"');
}

void
donna_g_string_append_concat (GString            *str,
                              const gchar        *string,
                              ...)
{
    va_list va_args;

    if (G_UNLIKELY (!string))
        return;

    g_string_append (str, string);

    va_start (va_args, string);
    for (;;)
    {
        const gchar *s;

        s = va_arg (va_args, const gchar *);
        if (s)
            g_string_append (str, s);
        else
            break;
    }
    va_end (va_args);
}

/**
 * donna_unquote_string:
 * @string: String to unquote
 *
 * Unquotes @string
 *
 * Unquoting means taking care of unescaping any escaped character and putting a
 * NUL at the end, which will either be the ending quote, or before in case some
 * unescaping was done.
 *
 * @str will be moved to right after the original ending quote (which may or may
 * not have been turned into a NUL).
 *
 * Note that in case of error (i.e. no ending quote) the string might be in a
 * modified state, as some unescaping might have been done.
 *
 * Returns: %TRUE was on sucessfull unquoting, %FALSE on error (no ending quote)
 * or if the string wasn't quoted
 */
gboolean
donna_unquote_string (gchar **str)
{
    gchar *end;
    guint i = 0;

    if (G_UNLIKELY (**str != '"'))
        return FALSE;

    for (end = ++*str; ; ++end)
    {
        if (end[i] == '\\')
        {
            *end = end[++i];
            continue;
        }
        *end = end[i];
        if (*end == '"')
            break;
        else if (*end == '\0')
            return FALSE;
    }
    /* turn the ending quote in to NUL, or put it somewhat before if we did some
     * unescaping */
    *end = '\0';
    /* move str past the original ending quote */
    *str = end + i + 1;

    return TRUE;
}

void
donna_g_object_unref (gpointer object)
{
    if (object)
        g_object_unref (object);
}

gboolean
donna_main_loop_quit_return_false (GMainLoop *loop)
{
    g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

static gboolean
dispatch (GSource *source, GSourceFunc callback, gpointer data)
{
    return callback (data);
}

static GSourceFuncs funcs = {
    .prepare = NULL,
    .check = NULL,
    .dispatch = dispatch,
    .finalize = NULL
};

GSource *
donna_fd_source_new (gint                fd,
                     GSourceFunc         callback,
                     gpointer            data,
                     GDestroyNotify      destroy)
{
    GSource *source;

    g_return_val_if_fail (fd >= 0, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    source = g_source_new (&funcs, sizeof (GSource));
    g_source_add_unix_fd (source, fd, G_IO_IN);
    g_source_set_callback (source, callback, data, destroy);

    return source;
}

guint
donna_fd_add_source (gint                fd,
                     GSourceFunc         callback,
                     gpointer            data,
                     GDestroyNotify      destroy)
{
    GSource *source;
    guint id;

    source = donna_fd_source_new (fd, callback, data, destroy);
    id = g_source_attach (source, NULL);
    g_source_unref (source);

    return id;
}
