/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * pattern.h
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

#ifndef __DONNA_PATTERN_H__
#define __DONNA_PATTERN_H__

#include <glib.h>

G_BEGIN_DECLS

#define DONNA_PATTERN_ERROR             g_quark_from_static_string ("DonnaPattern-Error")
/**
 * DonnaPatternError:
 * @DONNA_PATTERN_ERROR_INVALID_FIRST_CHAR: First character not allowed
 * @DONNA_PATTERN_ERROR_EMPTY: No pattern was given
 *
 * Other than that, possible errors are from #GRegexError
 */
typedef enum
{
    DONNA_PATTERN_ERROR_INVALID_FIRST_CHAR,
    DONNA_PATTERN_ERROR_EMPTY
} DonnaPatternError;

typedef struct _DonnaPattern            DonnaPattern;

/**
 * toggle_ref_cb:
 * @pattern: The #DonnaPattern for which the toggle_ref was triggered
 * @is_last: %TRUE when there's only one reference left, %FALSE when another
 * reference has been added
 * @data: user-data provided on donna_pattern_new()
 *
 * Callback used when the reference count on a #DonnaPattern either drops to 1,
 * or gets to 2
 */
typedef void (*toggle_ref_cb)                       (DonnaPattern   *pattern,
                                                     gboolean        is_last,
                                                     gpointer        data);

DonnaPattern *      donna_pattern_new               (const gchar    *string,
                                                     toggle_ref_cb   toggle_ref,
                                                     gpointer        data,
                                                     GDestroyNotify  destroy,
                                                     GError        **error);
DonnaPattern *      donna_pattern_ref               (DonnaPattern   *pattern);
void                donna_pattern_unref             (DonnaPattern   *pattern);
gint                donna_pattern_get_ref_count     (DonnaPattern   *pattern);
gboolean            donna_pattern_is_match          (DonnaPattern   *pattern,
                                                     const gchar    *string);

G_END_DECLS

#endif /* __DONNA_PATTERN_H__ */
