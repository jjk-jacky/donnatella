/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * util.h
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

#ifndef __DONNA_UTIL_H__
#define __DONNA_UTIL_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct
{
    guint        age_span_seconds;
    const gchar *age_fallback_format;
    const gchar *fluid_time_format;
    const gchar *fluid_date_format;
    gboolean     fluid_short_weekday;
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
gboolean        donna_g_ptr_array_contains      (GPtrArray          *arr,
                                                 gpointer            value,
                                                 GCompareFunc        cmp);
void            donna_g_string_append_quoted    (GString            *str,
                                                 const gchar        *s,
                                                 gboolean            double_percent);
void            donna_g_string_append_concat    (GString            *str,
                                                 const gchar        *string,
                                                 ...);
gboolean        donna_unquote_string            (gchar             **str);
void            donna_g_object_unref            (gpointer            object);
gboolean        donna_on_fd_close_main_loop     (gint                fd,
                                                 GIOCondition        condition,
                                                 GMainLoop          *loop);

G_END_DECLS

#endif /* __DONNA_UTIL_H__ */
