/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * context.h
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

#ifndef __DONNA_CONTEXT_H__
#define __DONNA_CONTEXT_H__

#include <glib.h>
#include "common.h"         /* DonnaArgType */

G_BEGIN_DECLS

/**
 * DonnaContextOptions:
 * @DONNA_CONTEXT_DEREFERENCE_NONE: No dereferencing by default; intrefs will be
 * used for (arrays of) nodes
 * @DONNA_CONTEXT_DEREFERENCE_FULL: Full dereferencing by default; (lists of)
 * full location(s) will be used
 * @DONNA_CONTEXT_DEREFERENCE_FS: FS dereferencing by default; Locations will be
 * used for nodes in "fs", others wil be skipped. Arrays use space and not comma
 * as separator, and are not quoted themselves (i.e. can be used in cmdline)
 * @DONNA_CONTEXT_NO_QUOTES: Don't use quotes. This is meant to be used for
 * parsing into a string to show e.g. in statusbar, etc. Note that for arrays
 * only the array itself won't be quoted, elements will still be quoted (i.e.
 * with FS dereferencing there's no difference)
 */
typedef enum
{
    DONNA_CONTEXT_DEREFERENCE_NONE  = (1 << 0),
    DONNA_CONTEXT_DEREFERENCE_FULL  = (1 << 1),
    DONNA_CONTEXT_DEREFERENCE_FS    = (1 << 2),
    DONNA_CONTEXT_NO_QUOTES         = (1 << 3),
} DonnaContextOptions;

/**
 * conv_flag_fn:
 * @c: The character of the variable to resolve
 * @extra: (allow-none): The extra (that was specified in between brackets), or
 * %NULL
 * @type: (out): Return location for the type
 * @ptr: (out): Return location for the value. For _DONNA_ARG_TYPE_CUSTOM this
 * should be the #conv_custom_fn to be used
 * @destroy: (allow-none) (out): Return location for function to be used on @ptr
 * after use; or %NULL
 * @data: The user-data from the #DonnaContext
 *
 * Function used during contextual parsing, to convert/resolve a variable. This
 * only needs to indicate the type & the actual value, donna_context_parse()
 * will handle using the appropriate string representation.
 *
 * Returns: %TRUE on success, else %FALSE
 */
typedef gboolean (*conv_flag_fn) (const gchar     c,
                                  gchar          *extra,
                                  DonnaArgType   *type,
                                  gpointer       *ptr,
                                  GDestroyNotify *destroy,
                                  gpointer        data);

/**
 * conv_custom_fn:
 * @c: The character of the variable to resolve
 * @extra: (allow-none): The extra (that was specified in between brackets), or
 * %NULL
 * @options: The options specified to donna_context_parse()
 * @str: The #GString to add whatever the variable resolves to
 * @data: The user-data from the #DonnaContext
 *
 * Performs custom resolving of variable, when #conv_flag_fn returned a type of
 * _DONNA_ARG_TYPE_CUSTOM
 */
typedef void (*conv_custom_fn)   (const gchar         c,
                                  gchar              *extra,
                                  DonnaContextOptions options,
                                  GString            *str,
                                  gpointer            data);

/**
 * DonnaContext:
 * @flags: The flags/variables supported during parsing
 * @allow_extra: Whether or not an extra can be specified in between brackets;
 * e.g. When %TRUE "%{foo}v" will trigger @conv for 'v' with "foo" as extra
 * @conv: The function used to resolve variables
 * @data: The user-data sent to @conv
 *
 * Represents the information needed to perform contextual parsing of a string.
 * See donna_context_parse()
 */
struct _DonnaContext
{
    const gchar *flags;
    gboolean     allow_extra;
    conv_flag_fn conv;
    gpointer     data;
};

typedef struct _DonnaContext DonnaContext;

void            donna_context_parse             (DonnaContext       *context,
                                                 DonnaContextOptions options,
                                                 DonnaApp           *app,
                                                 const gchar        *fmt,
                                                 GString           **str,
                                                 GPtrArray         **intrefs);

G_END_DECLS

#endif /* __DONNA_CONTEXT_H__ */
