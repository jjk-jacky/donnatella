/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * pattern.c
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

#include <string.h>
#include "pattern.h"
#include "macros.h"

/**
 * SECTION:pattern
 * @Short_description: Matching strings against patterns of different types
 *
 * #DonnaPattern allows to match strings against a given pattern, using
 * different types: exact match, glob-like patterns, Perl-compatible regex, etc
 *
 * See donna_pattern_new() for more
 */

/* patterns cannot start with one of those. Maybe some could be used later for
 * new modes, others simply cannot be used:
 * - '!' : isn't used for NOT in boolean (filters) but might added be later on
 * - '@' : could be confusing in commands (i.e. require quoting to not be
 *   processed as a "subcommand")
 * - '(' : would mix up w/ boolean parsing on filters
 * - '<' : could be confusing in commands (intrefs use <XXXX> format)
 */
#define FORBIDDEN_FIRST_CHARS       "!@()[]{}-+:%<"

enum type
{
    TYPE_PATTERN,
    TYPE_SEARCH,
    TYPE_BEGIN,
    TYPE_END,
    TYPE_INSENSITIVE_MATCH,
    TYPE_SENSITIVE_MATCH,
    TYPE_REGEX
};

struct pattern
{
    enum type            type;
    union {
        GPatternSpec    *pspec;
        GRegex          *regex;
        gchar           *string;
    };
    gsize                len;
};

struct _DonnaPattern
{
    gint                 ref_count;
    toggle_ref_cb        toggle_ref;
    gpointer             toggle_ref_data;
    GDestroyNotify       toggle_ref_destroy;
    GArray              *arr;
};

static void
free_pattern (struct pattern *p)
{
    if (p->type == TYPE_REGEX)
        g_regex_unref (p->regex);
    else if (p->type == TYPE_PATTERN)
        g_pattern_spec_free (p->pspec);
    else
        g_free (p->string);
}

static gboolean
init_new_pattern (const gchar       *string,
                  struct pattern    *p,
                  GError           **error)
{
    switch (*string)
    {
        case '^':
            p->type = TYPE_BEGIN;
            p->string = g_strdup (string + 1);
            break;
        case '$':
            p->type = TYPE_END;
            p->string = g_strdup (string + 1);
            p->len = strlen (p->string);
            break;
        case '~':
            p->type = TYPE_INSENSITIVE_MATCH;
            p->string = g_strdup (string + 1);
            break;
        case '=':
            p->type = TYPE_SENSITIVE_MATCH;
            p->string = g_strdup (string + 1);
            break;
        case '>':
            p->type = TYPE_REGEX;
            p->regex = g_regex_new (string + 1, G_REGEX_OPTIMIZE, 0, error);
            if (!p->regex)
                return FALSE;
            break;
        case '"':
            p->type = TYPE_PATTERN;
            ++string;
            break;
        case '\'':
            p->type = TYPE_SEARCH;
            ++string;
            break;
        default:
            if (!strchr (string, '*') && !strchr (string, '?'))
                p->type = TYPE_SEARCH;
            else
                p->type = TYPE_PATTERN;
            break;
    }

    if (p->type == TYPE_PATTERN)
        p->pspec = g_pattern_spec_new (string);
    else if (p->type == TYPE_SEARCH)
        p->string = g_strdup (string);

    return TRUE;
}

/**
 * donna_pattern_new:
 * @string: The string of the pattern to create
 * @toggle_ref: (allow-none): A callback to handle toggle reference (called when
 * the reference count on the #DonnaPattern goes to 1 or 2)
 * @data: (allow-none): User data for @toggle_ref
 * @destroy: (allow-none): Function called to free @data when the #DonnaPattern
 * memory is freed (i.e. no more references are held after a call to
 * donna_pattern_unref())
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Creates a new #DonnaPattern that can be later matched against strings using
 * donna_pattern_is_match()
 *
 * The given @string can start with one of the following character to specify
 * which type of matching should be performed:
 *
 * - a double-quote (<systemitem>"</systemitem>) for pattern mode: the given
 *   string can use '*' and '?' as wildchars with similar semantics as the
 *   standard glob() function. See #GPatternSpec for more
 * - a single-quote (<systemitem>'</systemitem>) for search mode: the given
 *   string will be searched (case sensitive) in the matched against string
 * - a caret (<systemitem>^</systemitem>) for begin mode: the matched against
 *   string must begin with the given string to match
 * - a dollar sign (<systemitem>$</systemitem>) for end mode: the matched
 *   against string must end with the given string to match
 * - a tilde sign (<systemitem>~</systemitem>) for case-insensitive matching
 *   mode: the matched against string and the given string must be the same
 *   (case insensitively)
 * - an equal sign (<systemitem>=</systemitem>) for case-sensitive matching
 *   mode: the matched against string and the given string must be the exact
 *   same
 * - a greater than sign (<systemitem>&gt;</systemitem>) for regex mode: the
 *   given string must be Perl-compatible regular expression to be matched
 *   against. See #GRegex for more
 *
 * If @string doesn't start with any of those, search mode will be used unless
 * there is at least one wildchars, then pattern mode is used.
 *
 * Note that @string cannot start with one of
 * <systemitem>!@()[]{}-+:%<</systemitem>
 *
 * Additionally, if @string first starts with a pipe character
 * (<systemitem>|</systemitem>) then any other pipe character will be used as a
 * separator (i.e. it cannot be used in any pattern definition), allowing you to
 * specify more than one possible patterns to match.
 *
 * When calling donna_pattern_is_match() each of them will be tried, in the
 * same order as they were specifed in, until the first match (if any).
 *
 * Note that each time the prefix rule apply, e.g. to match strings that end
 * either with "foo" or "bar" use "|$foo|$bar"
 * Of course you can use different types, e.g:
 * "|this file|*.pdf|>report [0-9]{4}\.xml"
 *
 * Returns: Newly-allocated #DonnaPattern (with a ref_count of 1); or %NULL
 */
DonnaPattern *
donna_pattern_new (const gchar    *string,
                   toggle_ref_cb   toggle_ref,
                   gpointer        data,
                   GDestroyNotify  destroy,
                   GError        **error)
{
    DonnaPattern *pattern;
    struct pattern p;

    g_return_val_if_fail (string != NULL, NULL);

    if (*string == '\0')
    {
        g_set_error (error, DONNA_PATTERN_ERROR, DONNA_PATTERN_ERROR_EMPTY,
                "Cannot create pattern for empty string");
        return NULL;
    }
    else if (strchr (FORBIDDEN_FIRST_CHARS, *string))
    {
        g_set_error (error, DONNA_PATTERN_ERROR,
                DONNA_PATTERN_ERROR_INVALID_FIRST_CHAR,
                "Patterns cannot start with one of the following: %s",
                FORBIDDEN_FIRST_CHARS);
        return NULL;
    }

    pattern = g_slice_new (DonnaPattern);
    pattern->ref_count          = 1;
    pattern->toggle_ref         = toggle_ref;
    pattern->toggle_ref_data    = data;
    pattern->toggle_ref_destroy = destroy;

    if (*string == '|')
    {
        gchar *s;

        s = strchrnul (++string, '|');
        pattern->arr = g_array_sized_new (FALSE, TRUE, sizeof (struct pattern),
                (*s == '|') ? 2 : 1);
        g_array_set_clear_func (pattern->arr, (GDestroyNotify) free_pattern);
        for (;;)
        {
            if (*s == '|')
            {
                if (s - string < 255)
                {
                    gchar buf[255];
                    strncpy (buf, string, (size_t) (s - string));
                    buf[s - string] = '\0';
                    if (!init_new_pattern (buf, &p, error))
                    {
                        g_array_free (pattern->arr, TRUE);
                        g_slice_free (DonnaPattern, pattern);
                        return NULL;
                    }
                    g_array_append_val (pattern->arr, p);
                }
                else
                {
                    gchar *b;
                    b = g_strndup (string, (gsize) (s - string));
                    if (!init_new_pattern (b, &p, error))
                    {
                        g_array_free (pattern->arr, TRUE);
                        g_slice_free (DonnaPattern, pattern);
                        return NULL;
                    }
                    g_array_append_val (pattern->arr, p);
                    g_free (b);
                }
                string = s + 1;
                s = strchrnul (string, '|');
            }
            else
            {
                if (!init_new_pattern (string, &p, error))
                {
                    g_array_free (pattern->arr, TRUE);
                    g_slice_free (DonnaPattern, pattern);
                    return NULL;
                }
                g_array_append_val (pattern->arr, p);
                break;
            }
        }
    }
    else
    {
        if (!init_new_pattern (string, &p, error))
        {
            g_slice_free (DonnaPattern, pattern);
            return NULL;
        }
        pattern->arr = g_array_sized_new (FALSE, TRUE, sizeof (struct pattern), 1);
        g_array_set_clear_func (pattern->arr, (GDestroyNotify) free_pattern);
        g_array_append_val (pattern->arr, p);
    }

    return pattern;
}

/**
 * donna_pattern_ref:
 * @pattern: A #DonnaPattern
 *
 * Adds a reference on @pattern. If the reference count goes to 2 and a
 * toggle_ref_cb was provided on donna_pattern_new() it will be triggered.
 *
 * Returns: @pattern (with an added reference)
 */
DonnaPattern *
donna_pattern_ref (DonnaPattern   *pattern)
{
    gint old_ref_count;

    g_return_val_if_fail (pattern != NULL, NULL);

    old_ref_count = g_atomic_int_add (&pattern->ref_count, 1);
    if (old_ref_count == 1 && pattern->toggle_ref)
        pattern->toggle_ref (pattern, FALSE, pattern->toggle_ref_data);

    return pattern;
}

/**
 * donna_pattern_unref:
 * @pattern: A #DonnaPattern
 *
 * Removes your reference on @pattern. If the reference count drops to 1 and a
 * toggle_ref_cb was provided on donna_pattern_new() it will be triggered. If
 * the reference count drops to zero the memory will be freed.
 */
void
donna_pattern_unref (DonnaPattern   *pattern)
{
    gint old_ref_count;

    g_return_if_fail (pattern != NULL);

    old_ref_count = g_atomic_int_get (&pattern->ref_count);
    while (!g_atomic_int_compare_and_exchange (&pattern->ref_count,
                old_ref_count, old_ref_count - 1))
        old_ref_count = g_atomic_int_get (&pattern->ref_count);

    if (old_ref_count == 2)
    {
        if (pattern->toggle_ref)
            pattern->toggle_ref (pattern, TRUE, pattern->toggle_ref_data);
    }
    else if (old_ref_count == 1)
    {
        g_array_free (pattern->arr, TRUE);

        if (pattern->toggle_ref_destroy && pattern->toggle_ref_data)
            pattern->toggle_ref_destroy (pattern->toggle_ref_data);

        g_slice_free (DonnaPattern, pattern);
    }
}

/**
 * donna_pattern_get_ref_count:
 * @pattern: A #DonnaPattern
 *
 * Returns the reference count for @pattern. This is only intended to be used by
 * the toggle_ref_cb provided in donna_pattern_new()
 *
 * Returns: The reference count on @pattern
 */
gint
donna_pattern_get_ref_count (DonnaPattern   *pattern)
{
    g_return_val_if_fail (pattern != NULL, -1);
    return g_atomic_int_get (&pattern->ref_count);
}

/**
 * donna_pattern_is_match:
 * @pattern: A #DonnaPattern
 * @string: (allow-none): The string to match against @pattern
 *
 * Checks whether @string matches against @pattern
 *
 * Returns: %TRUE if it matches, else %FALSE
 */
gboolean
donna_pattern_is_match (DonnaPattern   *pattern,
                        const gchar    *string)
{
    gboolean match = FALSE;
    guint i;

    g_return_val_if_fail (pattern != NULL, FALSE);

    if (G_UNLIKELY (!string || *string == '\0'))
        return FALSE;

    for (i = 0; i < pattern->arr->len; ++i)
    {
        struct pattern *p = &g_array_index (pattern->arr, struct pattern, i);

        switch (p->type)
        {
            case TYPE_PATTERN:
                match = g_pattern_match_string (p->pspec, string);
                break;

            case TYPE_SEARCH:
                match = strstr (string, p->string) != NULL;
                break;

            case TYPE_BEGIN:
                match = streqn (p->string, string, strlen (p->string));
                break;

            case TYPE_END:
                {
                    gsize len;

                    len = strlen (string);
                    match = len >= p->len
                        && streq (p->string, string + len - p->len);
                    break;
                }

            case TYPE_INSENSITIVE_MATCH:
                match = strcaseeq (p->string, string);
                break;

            case TYPE_SENSITIVE_MATCH:
                match = streq (p->string, string);
                break;

            case TYPE_REGEX:
                match = g_regex_match (p->regex, string, 0, NULL);
                break;
        }

        if (match)
            break;
    }

    return match;
}
