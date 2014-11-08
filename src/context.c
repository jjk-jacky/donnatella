/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * context.c
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

#include <gtk/gtk.h>
#include <string.h>
#include "context.h"
#include "util.h"
#include "treeview.h"
#include "node.h"
#include "terminal.h"
#include "app.h"
#include "macros.h"

/**
 * donna_context_parse:
 * @context: The context to use for parsing
 * @options: The options for parsing
 * @app: The #DonnaApp (required to create intrefs)
 * @fmt: The string to parse
 * @str: (out): Location of a #GString (or %NULL) that will be used for parsing.
 * If @str points to %NULL the #GString will be created only when & if needed
 * @intrefs: (allow-none) (out): Return location for any intrefs created
 *
 * Performs contextual parsing of @fmt via @context
 *
 * Contextual parsing happens e.g. on actions, when certain variables (e.g. \%o,
 * etc) can be used in the full location/trigger, and need to be parsed before
 * processing.
 *
 * When processing such variables, it should be known that by default so-called
 * "intrefs" (for internal references) can be used; For example, if a variable
 * points to a node, an intref will be used. An intref is simply a string
 * referencing said node in memory.
 *
 * It is possible to "dereference" a variable, so that instead of using an
 * intref, the full location of the node will be used. This is done by using a
 * star after the percent sign, e.g. `\%*n`
 * This can be useful if it isn't meant to be used as a command argument, but
 * e.g. to be used as part of a string or something.
 * Additionally, you can also use a special dereferencing, using a colon
 * instead, e.g. `\%:n`
 * This will use the location for nodes in "fs", and skip/use empty string for
 * any node in another domain; Particularly useful for use in command line of
 * external process.
 *
 * If intrefs were created during said parsing (see donna_app_new_int_ref()) and
 * @intrefs is not %NULL, A #GPtrArray will be created and filled with string
 * representations of intrefs. This is intended to be then used by
 * donna_app_parse_fl() and then donna_app_trigger_fl() so intrefs are freed
 * afterwards.
 *
 * @options can be used to specified a default dereferencing mode. If more than
 * one is specified, #DONNA_CONTEXT_DEREFERENCE_FULL takes precedence over the
 * others, and #DONNA_CONTEXT_DEREFERENCE_FS takes precedence over
 * #DONNA_CONTEXT_DEREFERENCE_NONE. If none are specified,
 * #DONNA_CONTEXT_DEREFERENCE_NONE is used.
 *
 * If @context allows extra, between the percent sign and the variable there can
 * be a quote in between braquet, e.g: \%{foo}v
 * This would resolve as variable 'v' with "foo" as extra. Note that inside an
 * extra it is required to escape with a backslash any backslashe or closing
 * braquets, e.g. to use "foo}bar" as extra, use: \%{foo\}bar}v
 *
 * @str can point either to an existing #GString, or %NULL. In the former case,
 * it will be used as is, adding to it. If nothing needed to be done (e.g. no
 * variable used in @fmt) then @fmt will be added to the #GString.
 * In the later case, a #GString will only be created when & if needed, so if
 * nothing needed to be done it will still point to %NULL (indicating @fmt can
 * be used as is).
 *
 * In addition to the supported variable by @context, the percent sign can be
 * obtained by doubling it (i.e. using "\%\%"). Anything not supported will
 * simply be left as is, percent sign included.
 * Should resolving a variable fail, it will simply resolve to nothing/be
 * removed.
 */
void
donna_context_parse (DonnaContext       *context,
                     DonnaContextOptions options,
                     DonnaApp           *app,
                     const gchar        *fmt,
                     GString           **_str,
                     GPtrArray         **intrefs)
{
    GString *str = *_str;
    const gchar *s;
    guint dereference_default;
    gboolean moved = FALSE;

    if (options & DONNA_CONTEXT_DEREFERENCE_FULL)
        dereference_default = DONNA_CONTEXT_DEREFERENCE_FULL;
    else if (options & DONNA_CONTEXT_DEREFERENCE_FS)
        dereference_default = DONNA_CONTEXT_DEREFERENCE_FS;
    else
        dereference_default = DONNA_CONTEXT_DEREFERENCE_NONE;

    s = fmt;
    while ((s = strchr (s, '%')))
    {
        guint dereference;
        gint pos;
        gboolean match;
        gchar *extra = NULL;
        const gchar *e = s;

        if (s[1] == '*')
        {
            dereference = DONNA_CONTEXT_DEREFERENCE_FULL;
            pos = 1;
        }
        else if (s[1] == ':')
        {
            dereference = DONNA_CONTEXT_DEREFERENCE_FS;
            pos = 1;
        }
        else
        {
            dereference = dereference_default;
            pos = 0;
        }

        if (pos == 0 && context->allow_extra && s[1] == '{')
        {
            gboolean need_escaping = FALSE;

            for (e = s + 2; *e; ++e)
            {
                if (*e == '\\')
                {
                    need_escaping = TRUE;
                    ++e;
                    continue;
                }
                else if (*e == '}')
                    break;
            }
            if (*e != '}')
                e = NULL;

            if (e)
            {
                extra = g_strndup (s + 2, (gsize) (e - s - 2));
                if (need_escaping)
                {
                    gchar *_e;
                    guint i = 0;

                    for (_e = extra; _e[i] != '\0'; ++_e)
                    {
                        if (_e[i] == '\\')
                        {
                            *_e = _e[++i];
                            continue;
                        }
                        else if (i > 0)
                            *_e = _e[i];
                    }
                    *_e = '\0';
                }
            }
            else
                e = s;
        }

        match = e[1 + pos] != '\0' && strchr (context->flags, e[1 + pos]) != NULL;
        if (match)
        {
            DonnaArgType type;
            gpointer ptr;
            GDestroyNotify destroy = NULL;

            if (!str)
                *_str = str = g_string_new (NULL);
            moved = TRUE;
            g_string_append_len (str, fmt, s - fmt);

            if (e != s)
                /* adjust for extra */
                s = e;
            else
                /* adjust for dereference operator, if any */
                s += pos;

            /* it can be FALSE when the variable doesn't actually resolve to
             * anything, e.g. it's for a current location and there are none */
            if (!context->conv (s[1], extra, &type, &ptr, &destroy, context->data))
            {
                s += 2;
                fmt = s;
                g_free (extra);
                continue;
            }

            /* we don't need to test for all possible types, only those can make
             * sense. That is, it could be a ROW, but not a ROW_ID (or PATH)
             * since those only make sense the other way around (or as type of
             * ROW_ID) */

            if (type & DONNA_ARG_TYPE_TREE_VIEW)
                g_string_append (str, donna_tree_view_get_name ((DonnaTreeView *) ptr));
            else if (type & DONNA_ARG_TYPE_ROW)
            {
                DonnaRow *row = (DonnaRow *) ptr;
                if (dereference != DONNA_CONTEXT_DEREFERENCE_NONE)
                {
                    gchar *l = NULL;

                    if (dereference == DONNA_CONTEXT_DEREFERENCE_FULL)
                        /* FULL = full location */
                        l = donna_node_get_full_location (row->node);
                    else if (streq (donna_node_get_domain (row->node), "fs"))
                        /* FS && domain "fs" = location */
                        l = donna_node_get_location (row->node);
                    else if (!(options & DONNA_CONTEXT_NO_QUOTES))
                    {
                        /* FS && another domain == empty string */
                        g_string_append_c (str, '"');
                        g_string_append_c (str, '"');
                    }

                    if (l)
                    {
                        if (options & DONNA_CONTEXT_NO_QUOTES)
                            g_string_append (str, l);
                        else
                            donna_g_string_append_quoted (str, l, FALSE);
                        g_free (l);
                    }
                }
                else
                    g_string_append_printf (str, "[%p;%p]", row->node, row->iter);
            }
            /* this will do nodes, array of nodes, array of strings */
            else if (type & (DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY))
            {
                if (dereference != DONNA_CONTEXT_DEREFERENCE_NONE)
                {
                    if (type & DONNA_ARG_IS_ARRAY)
                    {
                        GPtrArray *arr = ptr;
                        GString *string;
                        GString *str_arr = NULL;
                        gchar sep;
                        guint i;

                        if (dereference == DONNA_CONTEXT_DEREFERENCE_FS)
                        {
                            string = str;
                            sep = ' ';
                        }
                        else
                        {
                            /* NO_QUOTES only means no quotes around the array
                             * itself, but each element will still be quoted.
                             * So, for DEREF_FULL and NO_QUOTES, we can add
                             * directly into str */
                            if (options & DONNA_CONTEXT_NO_QUOTES)
                                string = str;
                            else
                                string = str_arr = g_string_new (NULL);
                            sep = ',';
                        }

                        if (type & DONNA_ARG_TYPE_NODE)
                            for (i = 0; i < arr->len; ++i)
                            {
                                DonnaNode *node = arr->pdata[i];
                                gchar *l = NULL;

                                if (dereference == DONNA_CONTEXT_DEREFERENCE_FULL)
                                    l = donna_node_get_full_location (node);
                                else if (streq (donna_node_get_domain (node), "fs"))
                                    l = donna_node_get_location (node);
                                /* no need to add a bunch of empty strings here */

                                if (l)
                                {
                                    /* we always quote here.
                                     * DONNA_CONTEXT_NO_QUOTES will only affect
                                     * the quotes around the array as a whole */
                                    donna_g_string_append_quoted (string, l, FALSE);
                                    g_string_append_c (string, sep);
                                    g_free (l);
                                }
                            }
                        else
                            for (i = 0; i < arr->len; ++i)
                            {
                                /* we always quote here.
                                 * DONNA_CONTEXT_NO_QUOTES will only affect the
                                 * quotes around the array as a whole */
                                donna_g_string_append_quoted (string,
                                        (gchar *) arr->pdata[i], FALSE);
                                g_string_append_c (string, sep);
                            }

                        /* remove last sep */
                        g_string_truncate (string, string->len - 1);

                        /* DEREF_FULL && !NO_QUOTES */
                        if (string != str)
                        {
                            /* str_arr is a list of quoted strings/FL, but we
                             * also need to quote the list itself */
                            donna_g_string_append_quoted (str, str_arr->str, FALSE);
                            g_string_free (str_arr, TRUE);
                        }
                    }
                    else
                    {
                        DonnaNode *node = ptr;
                        gchar *l = NULL;

                        if (dereference == DONNA_CONTEXT_DEREFERENCE_FULL)
                            l = donna_node_get_full_location (node);
                        else if (streq (donna_node_get_domain (node), "fs"))
                            l = donna_node_get_location (node);
                        else if (!(options & DONNA_CONTEXT_NO_QUOTES))
                        {
                            g_string_append_c (str, '"');
                            g_string_append_c (str, '"');
                        }

                        if (l)
                        {
                            if (options & DONNA_CONTEXT_NO_QUOTES)
                                g_string_append (str, l);
                            else
                                donna_g_string_append_quoted (str, l, FALSE);
                            g_free (l);
                        }
                    }
                }
                else
                {
                    gchar *ir = donna_app_new_int_ref (app, type, ptr);
                    g_string_append (str, ir);
                    if (intrefs)
                    {
                        if (!*intrefs)
                            *intrefs = g_ptr_array_new_with_free_func (g_free);
                        g_ptr_array_add (*intrefs, ir);
                    }
                    else
                        g_free (ir);
                }
            }
            else if (type & DONNA_ARG_TYPE_TERMINAL)
                g_string_append (str, donna_terminal_get_name ((DonnaTerminal *) ptr));
            else if (type & DONNA_ARG_TYPE_STRING)
            {
                if (options & DONNA_CONTEXT_NO_QUOTES)
                    g_string_append (str, (gchar *) ptr);
                else
                    donna_g_string_append_quoted (str, (gchar *) ptr, FALSE);
            }
            else if (type & DONNA_ARG_TYPE_INT)
                g_string_append_printf (str, "%d", * (gint *) ptr);
            else if (type & _DONNA_ARG_TYPE_CUSTOM)
                ((conv_custom_fn) ptr) (s[1], extra, options, str, context->data);

            if (destroy)
                destroy (ptr);

            s += 2;
            fmt = s;
        }
        else if (s[1] == '%')
        {
            /* "%%" -> "%" */
            if (!str)
                *_str = str = g_string_new (NULL);
            moved = TRUE;
            g_string_append_len (str, fmt, s - fmt);
            fmt = ++s;
            ++s;
        }
        else if (s[1] == '\0')
        {
            g_free (extra);
            break;
        }
        else
            /* any unknown variable is left as-is, '%' included */
            ++s;

        g_free (extra);
    }

    /* if not moved but a GString was provided, we should put fmt in there */
    if (moved || str)
        g_string_append (str, fmt);
}
