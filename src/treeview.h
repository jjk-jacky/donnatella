/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * treeview.h
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

#ifndef __DONNA_TREE_VIEW_H__
#define __DONNA_TREE_VIEW_H__

#include "common.h"
#include "app.h"
#include "columntype.h"
#include "conf.h"
#include "node.h"
#include "history.h"
#include "filter.h"

G_BEGIN_DECLS

#define DONNA_TREE_VIEW_ERROR            g_quark_from_static_string ("DonnaTreeView-Error")
enum
{
    DONNA_TREE_VIEW_ERROR_NOMEM,
    DONNA_TREE_VIEW_ERROR_NOT_FOUND,
    DONNA_TREE_VIEW_ERROR_CANNOT_ADD_NODE,
    DONNA_TREE_VIEW_ERROR_INVALID_ROW_ID,
    DONNA_TREE_VIEW_ERROR_UNKNOWN_COLUMN,
    DONNA_TREE_VIEW_ERROR_INVALID_MODE,
    DONNA_TREE_VIEW_ERROR_INCOMPATIBLE_OPTION,
    DONNA_TREE_VIEW_ERROR_FLAT_PROVIDER,
    DONNA_TREE_VIEW_ERROR_COLUMN_NAME_TOO_BROAD,
    DONNA_TREE_VIEW_ERROR_OTHER,
} DonnaTreeViewError;

typedef struct _DonnaArrangement        DonnaArrangement;

typedef enum
{
    DONNA_ARRANGEMENT_PRIORITY_LOW,
    DONNA_ARRANGEMENT_PRIORITY_NORMAL,
    DONNA_ARRANGEMENT_PRIORITY_HIGH,
    DONNA_ARRANGEMENT_PRIORITY_OVERRIDE
} DonnaArrangementPriority;

typedef enum
{
    DONNA_SORT_UNKNOWN = 0,
    DONNA_SORT_ASC,
    DONNA_SORT_DESC
} DonnaSortOrder;

typedef enum
{
    DONNA_SECOND_SORT_STICKY_UNKNOWN = 0,
    DONNA_SECOND_SORT_STICKY_ENABLED,
    DONNA_SECOND_SORT_STICKY_DISABLED
} DonnaSecondSortSticky;

typedef enum
{
    DONNA_ARRANGEMENT_HAS_COLUMNS               = (1 << 0),
    DONNA_ARRANGEMENT_HAS_SORT                  = (1 << 1),
    DONNA_ARRANGEMENT_HAS_SECOND_SORT           = (1 << 2),
    DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS       = (1 << 3),
    DONNA_ARRANGEMENT_HAS_COLOR_FILTERS         = (1 << 4),

    DONNA_ARRANGEMENT_COLUMNS_ALWAYS            = (1 << 10),
    DONNA_ARRANGEMENT_SORT_ALWAYS               = (1 << 11),
    DONNA_ARRANGEMENT_SECOND_SORT_ALWAYS        = (1 << 12),
    DONNA_ARRANGEMENT_COLUMNS_OPTIONS_ALWAYS    = (1 << 13),
    DONNA_ARRANGEMENT_COLOR_FILTERS_ALWAYS      = (1 << 14),

    DONNA_ARRANGEMENT_HAS_ALL           = DONNA_ARRANGEMENT_HAS_COLUMNS
        | DONNA_ARRANGEMENT_HAS_SORT | DONNA_ARRANGEMENT_HAS_SECOND_SORT
        | DONNA_ARRANGEMENT_HAS_COLUMNS_OPTIONS
        | DONNA_ARRANGEMENT_HAS_COLOR_FILTERS
} DonnaArrangementFlags;

struct _DonnaArrangement
{
    DonnaArrangementPriority     priority;
    DonnaArrangementFlags        flags;
    gchar                       *columns;
    gchar                       *main_column;
    gchar                       *columns_source;
    gchar                       *sort_column;
    DonnaSortOrder               sort_order;
    gchar                       *sort_source;
    gchar                       *second_sort_column;
    DonnaSortOrder               second_sort_order;
    DonnaSecondSortSticky        second_sort_sticky;
    gchar                       *second_sort_source;
    gchar                       *columns_options;
    GSList                      *color_filters;
};

/* special handling for DONNA_ARG_TYPE_ROW_ID that can be a ROW, NODE, or PATH.
 * This allows any of those to be used/parsed, and the command can check it got
 * the right one. */
struct _DonnaRowId
{
    DonnaArgType    type;
    gpointer        ptr;
};

struct _DonnaRow
{
    DonnaNode   *node;
    GtkTreeIter *iter;
};

typedef enum
{
    DONNA_SEL_SELECT = 1,
    DONNA_SEL_UNSELECT,
    DONNA_SEL_INVERT,
    DONNA_SEL_DEFINE,       /* basically unselect all + select */
} DonnaSelAction;

typedef enum
{
    DONNA_TREE_TOGGLE_STANDARD,
    DONNA_TREE_TOGGLE_FULL,
    DONNA_TREE_TOGGLE_MAXI,
} DonnaTreeToggle;

typedef enum
{
    DONNA_TREE_VIEW_REFRESH_VISIBLE,
    DONNA_TREE_VIEW_REFRESH_SIMPLE,
    DONNA_TREE_VIEW_REFRESH_NORMAL,
    DONNA_TREE_VIEW_REFRESH_RELOAD
} DonnaTreeViewRefreshMode;

typedef enum
{
    DONNA_TREE_VISUAL_NOTHING    = 0,
    DONNA_TREE_VISUAL_NAME       = (1 << 0),
    DONNA_TREE_VISUAL_ICON       = (1 << 1),
    DONNA_TREE_VISUAL_BOX        = (1 << 2),
    DONNA_TREE_VISUAL_HIGHLIGHT  = (1 << 3),
    DONNA_TREE_VISUAL_CLICK_MODE = (1 << 4),
} DonnaTreeVisual;

typedef enum
{
    DONNA_TREE_VISUAL_SOURCE_TREE   = (1 << 0),
    DONNA_TREE_VISUAL_SOURCE_NODE   = (1 << 1),

    DONNA_TREE_VISUAL_SOURCE_ANY    = DONNA_TREE_VISUAL_SOURCE_TREE
        | DONNA_TREE_VISUAL_SOURCE_NODE,
} DonnaTreeVisualSource;

typedef enum
{
    DONNA_TREE_VIEW_SET_SCROLL   = (1 << 0),
    DONNA_TREE_VIEW_SET_FOCUS    = (1 << 1),
    DONNA_TREE_VIEW_SET_CURSOR   = (1 << 2),
} DonnaTreeViewSet;

typedef enum
{
    DONNA_TREE_VIEW_GOTO_LINE,
    DONNA_TREE_VIEW_GOTO_REPEAT,
    DONNA_TREE_VIEW_GOTO_PERCENT,
    DONNA_TREE_VIEW_GOTO_VISIBLE,
} DonnaTreeViewGoto;

/* must be same as DonnaColumnOptionSaveLocation */
typedef enum
{
    DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY = 0, /* i.e. don't save, only apply */
    DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT,
    DONNA_TREE_VIEW_OPTION_SAVE_IN_ARRANGEMENT,
    DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE,
    DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE,
    DONNA_TREE_VIEW_OPTION_SAVE_IN_DEFAULT,
    DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK,
    DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION /* treeview option default_save_location */
} DonnaTreeViewOptionSaveLocation;

typedef enum
{
    DONNA_LIST_FILE_FOCUS       = (1 << 0),
    DONNA_LIST_FILE_SORT        = (1 << 1),
    DONNA_LIST_FILE_SCROLL      = (1 << 2),
    DONNA_LIST_FILE_SELECTION   = (1 << 3)
} DonnaListFileElements;

struct _DonnaTreeView
{
    /*< private >*/
    GtkTreeView              treeview;
    DonnaTreeViewPrivate    *priv;
};

struct _DonnaTreeViewClass
{
    GtkTreeViewClass parent_class;

    DonnaArrangement *      (*select_arrangement)       (DonnaTreeView  *tree,
                                                         const gchar    *name,
                                                         DonnaNode      *node);
};


GtkWidget *     donna_tree_view_new             (DonnaApp           *app,
                                                 const gchar        *name);
const gchar *   donna_tree_view_get_name        (DonnaTreeView      *tree);
gboolean        donna_tree_view_is_tree         (DonnaTreeView      *tree);
void            donna_tree_view_build_arrangement (
                                                 DonnaTreeView      *tree,
                                                 gboolean            force);
/* both modes */
gboolean        donna_tree_view_set_node_property (DonnaTreeView    *tree,
                                                 DonnaNode          *node,
                                                 const gchar        *prop,
                                                 const GValue       *value,
                                                 GError            **error);
gboolean        donna_tree_view_set_location    (DonnaTreeView      *tree,
                                                 DonnaNode          *node,
                                                 GError            **error);
DonnaNode *     donna_tree_view_get_location    (DonnaTreeView      *tree);
gboolean        donna_tree_view_selection       (DonnaTreeView      *tree,
                                                 DonnaSelAction      action,
                                                 DonnaRowId         *rowid,
                                                 gboolean            to_focused,
                                                 GError            **error);
gboolean        donna_tree_view_set_focus       (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
gboolean        donna_tree_view_set_cursor      (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 gboolean            no_scroll,
                                                 GError            **error);
gboolean        donna_tree_view_activate_row    (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
gboolean        donna_tree_view_column_edit     (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 const gchar        *column,
                                                 GError            **error);
gboolean        donna_tree_view_column_set_option (
                                                 DonnaTreeView      *tree,
                                                 const gchar        *column,
                                                 const gchar        *option,
                                                 const gchar        *value,
                                                 DonnaTreeViewOptionSaveLocation save_location,
                                                 GError            **error);
gboolean        donna_tree_view_column_set_value(DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 gboolean            to_focused,
                                                 const gchar        *column,
                                                 const gchar        *value,
                                                 DonnaRowId         *rowid_ref,
                                                 GError            **error);
gboolean        donna_tree_view_refresh         (DonnaTreeView      *tree,
                                                 DonnaTreeViewRefreshMode mode,
                                                 GError             **error);
gboolean        donna_tree_view_goto_line       (DonnaTreeView      *tree,
                                                 DonnaTreeViewSet    set,
                                                 DonnaRowId         *rowid,
                                                 guint               nb,
                                                 DonnaTreeViewGoto   nb_type,
                                                 DonnaSelAction      action,
                                                 gboolean            to_focused,
                                                 GError            **error);
DonnaNode *     donna_tree_view_get_node_at_row (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
void            donna_tree_view_set_key_mode    (DonnaTreeView      *tree,
                                                 const gchar        *key_mode);
void            donna_tree_view_reset_keys      (DonnaTreeView      *tree);
GPtrArray *     donna_tree_view_get_nodes       (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 gboolean            to_focused,
                                                 GError            **error);
DonnaNode *     donna_tree_view_get_node_up     (DonnaTreeView      *tree,
                                                 gint                level,
                                                 GError            **error);
gboolean        donna_tree_view_go_up           (DonnaTreeView      *tree,
                                                 gint                level,
                                                 DonnaTreeViewSet    set,
                                                 GError            **error);
GPtrArray *     donna_tree_view_context_get_nodes (
                                                 DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 const gchar        *column,
                                                 gchar              *items,
                                                 GError            **error);
gboolean        donna_tree_view_context_popup   (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 const gchar        *column,
                                                 gchar              *items,
                                                 const gchar        *menus,
                                                 gboolean            no_focus_grab,
                                                 GError            **error);
gboolean        donna_tree_view_set_sort_order  (DonnaTreeView      *tree,
                                                 const gchar        *column,
                                                 DonnaSortOrder      order,
                                                 GError            **error);
gboolean        donna_tree_view_set_second_sort_order (
                                                 DonnaTreeView      *tree,
                                                 const gchar        *column,
                                                 DonnaSortOrder      order,
                                                 GError            **error);
gboolean        donna_tree_view_set_option      (DonnaTreeView      *tree,
                                                 const gchar        *option,
                                                 const gchar        *value,
                                                 DonnaTreeViewOptionSaveLocation save_location,
                                                 GError            **error);
gboolean        donna_tree_view_toggle_column   (DonnaTreeView      *tree,
                                                 const gchar        *column,
                                                 GError            **error);
gboolean        donna_tree_view_set_columns     (DonnaTreeView      *tree,
                                                 const gchar        *columns,
                                                 GError            **error);
void            donna_tree_view_start_interactive_search (
                                                 DonnaTreeView      *tree);
gboolean        donna_tree_view_save_to_config  (DonnaTreeView      *tree,
                                                 const gchar        *elements,
                                                 GError            **error);
/* mode Tree */
gboolean        donna_tree_view_save_tree_file  (DonnaTreeView      *tree,
                                                 const gchar        *filename,
                                                 DonnaTreeVisual     visuals,
                                                 GError            **error);
gboolean        donna_tree_view_load_tree_file  (DonnaTreeView      *tree,
                                                 const gchar        *filename,
                                                 DonnaTreeVisual     visuals,
                                                 GError            **error);
gboolean        donna_tree_view_add_root        (DonnaTreeView      *tree,
                                                 DonnaNode          *node,
                                                 GError            **error);
gboolean        donna_tree_view_set_visual      (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 DonnaTreeVisual     visual,
                                                 const gchar        *value,
                                                 GError            **error);
gboolean        donna_tree_view_root_set_child_visual (
                                                 DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 DonnaNode          *node,
                                                 DonnaTreeVisual     visual,
                                                 const gchar        *value,
                                                 GError            **error);
gchar *         donna_tree_view_get_visual      (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 DonnaTreeVisual     visual,
                                                 DonnaTreeVisualSource source,
                                                 GError            **error);
gchar *         donna_tree_view_root_get_child_visual (
                                                 DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 DonnaNode          *node,
                                                 DonnaTreeVisual     visual,
                                                 DonnaTreeVisualSource source,
                                                 GError            **error);
gboolean        donna_tree_view_toggle_row      (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 DonnaTreeToggle     toggle,
                                                 GError            **error);
gboolean        donna_tree_view_full_expand     (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
gboolean        donna_tree_view_full_collapse   (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
gboolean        donna_tree_view_remove_row      (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
gboolean        donna_tree_view_go_root         (DonnaTreeView      *tree,
                                                 GError            **error);
DonnaNode *     donna_tree_view_get_node_root   (DonnaTreeView      *tree,
                                                 GError            **error);
gboolean        donna_tree_view_move_root       (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 gint                move,
                                                 GError            **error);
/* Mini-Tree */
gboolean        donna_tree_view_maxi_expand     (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
gboolean        donna_tree_view_maxi_collapse   (DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 GError            **error);
/* Mode List */
gboolean        donna_tree_view_save_list_file  (DonnaTreeView      *tree,
                                                 const gchar        *filename,
                                                 DonnaListFileElements elements,
                                                 GError            **error);
gboolean        donna_tree_view_load_list_file  (DonnaTreeView      *tree,
                                                 const gchar        *filename,
                                                 DonnaListFileElements elements,
                                                 GError            **error);
gboolean        donna_tree_view_selection_nodes (DonnaTreeView      *tree,
                                                 DonnaSelAction      action,
                                                 GPtrArray          *nodes,
                                                 GError            **error);
GPtrArray *     donna_tree_view_get_selected_nodes (
                                                 DonnaTreeView      *tree,
                                                 GError            **error);
GPtrArray *     donna_tree_view_get_children    (DonnaTreeView      *tree,
                                                 DonnaNode          *node,
                                                 DonnaNodeType       node_types);
void            donna_tree_view_abort           (DonnaTreeView      *tree);
GPtrArray *     donna_tree_view_history_get     (DonnaTreeView      *tree,
                                                 DonnaHistoryDirection direction,
                                                 guint               nb,
                                                 GError            **error);
DonnaNode *     donna_tree_view_history_get_node(DonnaTreeView      *tree,
                                                 DonnaHistoryDirection direction,
                                                 guint               nb,
                                                 GError            **error);
gboolean        donna_tree_view_history_move    (DonnaTreeView      *tree,
                                                 DonnaHistoryDirection direction,
                                                 guint               nb,
                                                 GError            **error);
gboolean        donna_tree_view_history_clear   (DonnaTreeView      *tree,
                                                 DonnaHistoryDirection direction,
                                                 GError            **error);
DonnaNode *     donna_tree_view_get_node_down   (DonnaTreeView      *tree,
                                                 gint                level,
                                                 GError            **error);
gboolean        donna_tree_view_go_down         (DonnaTreeView      *tree,
                                                 gint                level,
                                                 GError            **error);
gboolean        donna_tree_view_set_visual_filter (
                                                 DonnaTreeView      *tree,
                                                 DonnaFilter        *filter,
                                                 gboolean            toggle,
                                                 GError            **error);
DonnaFilter *   donna_tree_view_get_visual_filter (
                                                 DonnaTreeView      *tree,
                                                 GError            **error);
gboolean        donna_tree_view_column_refresh_nodes (
                                                 DonnaTreeView      *tree,
                                                 DonnaRowId         *rowid,
                                                 gboolean            to_focused,
                                                 const gchar        *column,
                                                 GError            **error);

G_END_DECLS

#endif /* __DONNA_TREE_VIEW_H__ */
