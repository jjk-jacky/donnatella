/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * filter.h
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

#ifndef __DONNA_FILTER_H__
#define __DONNA_FILTER_H__

#include <glib.h>
#include <glib-object.h>
#include "common.h"

G_BEGIN_DECLS

#define DONNA_FILTER_ERROR          g_quark_from_static_string ("DonnaFilter-Error")
typedef enum
{
    DONNA_FILTER_ERROR_INVALID_COLUMN_TYPE,
    DONNA_FILTER_ERROR_INVALID_SYNTAX,
} DonnaFilterError;

typedef struct _DonnaFilter         DonnaFilter;
typedef struct _DonnaFilterPrivate  DonnaFilterPrivate;
typedef struct _DonnaFilterClass    DonnaFilterClass;

#define DONNA_TYPE_FILTER           (donna_filter_get_type ())
#define DONNA_FILTER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_FILTER, DonnaFilter))
#define DONNA_FILTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_FILTER, DonnaFilterClass))
#define DONNA_IS_FILTER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_FILTER))
#define DONNA_IS_FILTER_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_FILTER))
#define DONNA_FILTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_FILTER, DonnaFilterClass))

GType   donna_filter_get_type       (void) G_GNUC_CONST;

struct _DonnaFilter
{
    /*< private >*/
    GObject parent;
    DonnaFilterPrivate *priv;
};

struct _DonnaFilterClass
{
    GObjectClass parent_class;
};

gchar *             donna_filter_get_filter         (DonnaFilter    *filter);
gboolean            donna_filter_compile            (DonnaFilter    *filter,
                                                     GError        **error);
gboolean            donna_filter_is_compiled        (DonnaFilter    *filter);
gboolean            donna_filter_is_match           (DonnaFilter    *filter,
                                                     DonnaNode      *node,
                                                     DonnaTreeView  *treeview);

G_END_DECLS

#endif /* __DONNA_FILTER_H__ */
