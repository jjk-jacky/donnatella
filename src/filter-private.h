/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * filter-private.h
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

#ifndef __DONNA_FILTER_PRIVATE_H__
#define __DONNA_FILTER_PRIVATE_H__

G_BEGIN_DECLS

#include "common.h"

/* refresh_properties */
enum rp
{
    RP_VISIBLE = 0,
    RP_PRELOAD,
    RP_ON_DEMAND,
    _MAX_RP
};

typedef gboolean (*get_ct_data_fn) (const gchar *col_name,
                                    DonnaNode   *node,
                                    gpointer    *ctdata,
                                    gpointer     data);

struct col_ct_data
{
    gchar       *col_name;
    GPtrArray   *props;
    gpointer     ct_data;
    /*< private >*/
    guint        index;
    guint        ref_count;
};

/* from treeview.c */
gboolean                _donna_tree_view_get_ct_data    (const gchar    *col_name,
                                                         DonnaNode      *node,
                                                         gpointer       *ctdata,
                                                         DonnaTreeView  *treeview);
/* from app.c */
struct col_ct_data *    _donna_app_get_col_ct_data      (DonnaApp       *app,
                                                         const gchar    *col_name);
void                    _donna_app_unref_col_ct_data    (DonnaApp       *app,
                                                         struct col_ct_data *col_ct_data);


/* private API from filter.c (for provider-filter.c) */
enum
{
    _DONNA_FILTER_PROP_ALIAS        = (1 << 0),
    _DONNA_FILTER_PROP_NAME         = (1 << 1),
    _DONNA_FILTER_PROP_ICON_NAME    = (1 << 2)
};

gboolean    _donna_filter_has_props                     (DonnaFilter        *filter,
                                                         guint               props);
gchar *     _donna_filter_get_key                       (DonnaFilter        *filter);
void        _donna_filter_set_alias                     (DonnaFilter        *filter,
                                                         const gchar        *alias,
                                                         gboolean            notify);
void        _donna_filter_set_name                      (DonnaFilter        *filter,
                                                         const gchar        *name,
                                                         gboolean            notify);
void        _donna_filter_set_icon_name                 (DonnaFilter        *filter,
                                                         const gchar        *icon_name,
                                                         gboolean            notify);
DonnaApp *  _donna_filter_peek_app                      (DonnaFilter        *filter);

G_END_DECLS

#endif /* __DONNA_FILTER_H__ */
