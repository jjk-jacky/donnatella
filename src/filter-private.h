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

typedef gpointer (*get_ct_data_fn) (const gchar *col_name, gpointer data);

struct col_ct_data
{
    gchar   *col_name;
    gpointer ct_data;
    /*< private >*/
    guint    index;
    guint    ref_count;
};

/* from treeview.c */
gpointer                _donna_tree_view_get_ct_data    (const gchar    *col_name,
                                                         DonnaTreeView  *treeview);
/* from app.c */
struct col_ct_data *    _donna_app_get_col_ct_data      (DonnaApp       *app,
                                                         const gchar    *col_name);
void                    _donna_app_unref_col_ct_data    (DonnaApp       *app,
                                                         struct col_ct_data *col_ct_data);

G_END_DECLS

#endif /* __DONNA_FILTER_H__ */
