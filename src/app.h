/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * app.h
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

#ifndef __DONNA_APP_H__
#define __DONNA_APP_H__

#include <gtk/gtk.h>
#include "common.h"
#include "conf.h"
#include "task-manager.h"
#include "treeview.h"
#include "terminal.h"
#include "columntype.h"
#include "filter.h"
#include "pattern.h"
#include "context.h"

G_BEGIN_DECLS

#define DONNA_APP_ERROR         g_quark_from_static_string ("DonnaApp-Error")
typedef enum
{
    DONNA_APP_ERROR_EMPTY,
    DONNA_APP_ERROR_UNKNOWN_TYPE,
    DONNA_APP_ERROR_NOT_FOUND,
    DONNA_APP_ERROR_OTHER,
} DonnaAppError;

struct _DonnaApp
{
    /*< private >*/
    GObject object;
    DonnaAppPrivate *priv;
};

struct _DonnaAppClass
{
    /*< private >*/
    GObjectClass parent_class;

    /*< public >*/
    void                (*tree_view_loaded)         (DonnaApp       *app,
                                                     DonnaTreeView  *tree);
    gboolean            (*event)                    (DonnaApp       *app,
                                                     const gchar    *event,
                                                     const gchar    *source,
                                                     DonnaContext   *context);
};

gint                donna_app_run                   (DonnaApp       *app,
                                                     gint            argc,
                                                     gchar          *argv[]);
void                donna_app_ensure_focused        (DonnaApp       *app);
void                donna_app_move_focus            (DonnaApp       *app,
                                                     gint            move);
gboolean            donna_app_set_focus             (DonnaApp       *app,
                                                     const gchar    *type,
                                                     const gchar    *name,
                                                     GError        **error);
void                donna_app_add_window            (DonnaApp       *app,
                                                     GtkWindow      *window,
                                                     gboolean        destroy_with_parent);
void                donna_app_set_floating_window   (DonnaApp       *app,
                                                     GtkWindow      *window);
DonnaConfig *       donna_app_get_config            (DonnaApp       *app);
DonnaConfig *       donna_app_peek_config           (DonnaApp       *app);
DonnaProvider *     donna_app_get_provider          (DonnaApp       *app,
                                                     const gchar    *domain);
DonnaNode *         donna_app_get_node              (DonnaApp       *app,
                                                     const gchar    *full_location,
                                                     gboolean        do_user_parse,
                                                     GError        **error);
gboolean            donna_app_trigger_node          (DonnaApp       *app,
                                                     const gchar    *full_location,
                                                     gboolean        do_user_parse,
                                                     GError        **error);
DonnaColumnType *   donna_app_get_column_type       (DonnaApp       *app,
                                                     const gchar    *type);
DonnaFilter *       donna_app_get_filter            (DonnaApp       *app,
                                                     const gchar    *filter);
DonnaPattern *      donna_app_get_pattern           (DonnaApp       *app,
                                                     const gchar    *pattern,
                                                     GError        **error);
void                donna_app_run_task              (DonnaApp       *app,
                                                     DonnaTask      *task);
gboolean            donna_app_run_task_and_wait     (DonnaApp       *app,
                                                     DonnaTask      *task,
                                                     DonnaTask      *current_task,
                                                     GError        **error);
DonnaTaskManager *  donna_app_peek_task_manager     (DonnaApp       *app);
DonnaTreeView *     donna_app_get_tree_view         (DonnaApp       *app,
                                                     const gchar    *name);
DonnaTerminal *     donna_app_get_terminal          (DonnaApp       *app,
                                                     const gchar    *name);
DonnaNode *         donna_app_get_current_location  (DonnaApp       *app,
                                                     GError        **error);
gchar *             donna_app_get_current_dirname   (DonnaApp       *app);
gchar *             donna_app_get_conf_filename     (DonnaApp       *app,
                                                     const gchar    *fmt,
                                                     ...);
gchar **            donna_app_get_environ           (DonnaApp       *app);
gchar *             donna_app_new_int_ref           (DonnaApp       *app,
                                                     DonnaArgType    type,
                                                     gpointer        ptr);
gpointer            donna_app_get_int_ref           (DonnaApp       *app,
                                                     const gchar    *intref,
                                                     DonnaArgType    type);
gboolean            donna_app_free_int_ref          (DonnaApp       *app,
                                                     const gchar    *intref);
gchar *             donna_app_parse_fl              (DonnaApp       *app,
                                                     gchar          *fl,
                                                     gboolean        must_free_fl,
                                                     DonnaContext   *context,
                                                     GPtrArray     **intrefs);
gboolean            donna_app_trigger_fl            (DonnaApp       *app,
                                                     const gchar    *fl,
                                                     GPtrArray      *intrefs,
                                                     gboolean        blocking,
                                                     GError        **error);
gboolean            donna_app_emit_event            (DonnaApp       *app,
                                                     const gchar    *event,
                                                     gboolean        is_confirm,
                                                     DonnaContext   *context,
                                                     const gchar    *fmt_source,
                                                     ...);
gboolean            donna_app_show_menu             (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *menu,
                                                     GError        **error);
void                donna_app_show_error            (DonnaApp       *app,
                                                     const GError   *error,
                                                     const gchar    *fmt,
                                                     ...);
gboolean            donna_app_filter_nodes          (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     const gchar    *filter,
                                                     DonnaTreeView  *tree,
                                                     GError       **error);
DonnaTask *         donna_app_nodes_io_task         (DonnaApp       *app,
                                                     GPtrArray      *nodes,
                                                     DonnaIoType     io_type,
                                                     DonnaNode      *dest,
                                                     const gchar    *new_name,
                                                     GError        **error);
gint                donna_app_ask                   (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *btn1_icon,
                                                     const gchar    *btn1_label,
                                                     const gchar    *btn2_icon,
                                                     const gchar    *btn2_label,
                                                     ...);
gchar *             donna_app_ask_text              (DonnaApp       *app,
                                                     const gchar    *title,
                                                     const gchar    *details,
                                                     const gchar    *main_default,
                                                     const gchar   **other_defaults,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_APP_H__ */
