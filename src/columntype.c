/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype.c
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
#include "columntype.h"
#include "app.h"
#include "conf.h"
#include "util.h"
#include "macros.h"

/**
 * SECTION:columntype
 * @Short_description: A column type is the "engine" of a column
 *
 * Tree views use columns to show information about nodes. A column is first
 * defined by its columntype, through option
 * `defaults/&lt;TREEVIEW-MODE&gt;s/columns/&lt;COLUMN-NAME&gt;`
 *
 * A column type is what defines the behavior of the column: what it will show
 * and how, it also handles sorting by that column or the filtering capabilities
 * of the column.
 *
 * In addition, in their context menus tree views will offer an alias
 * `column.&lt;COLUMN-NAME&gt;.options` which is resolved by the column type,
 * which can also offer items that will be available under
 * `column.&lt;COLUMN-NAME&gt;.&lt;ITEM&gt;`
 * As a rule, column types will provide items for their options.
 *
 * Available column types are:
 * - "name" : See #DonnaColumnTypeName.description
 * - "time" : See #DonnaColumnTypeTime.description
 * - "size" : See #DonnaColumnTypeSize.description
 * - "perms" : See #DonnaColumnTypePerms.description
 * - "text" : See #DonnaColumnTypeText.description
 * - "value" : See #DonnaColumnTypeValue.description
 * - "progress" : See #DonnaColumnTypeProgress.description
 * - "label" : See #DonnaColumnTypeLabel.description
 */

/* internal; used by treeview.c */
DonnaColumnOptionSaveLocation
_donna_column_type_ask_save_location (DonnaApp     *app,
                                      const gchar  *col_name,
                                      const gchar  *arr_name,
                                      const gchar  *tv_name,
                                      gboolean      is_tree,
                                      const gchar  *def_cat,
                                      const gchar  *option,
                                      guint         from);
/* internal: used by treeview.c and any columntype implementations with options
 * using an extra for value */
gboolean
_donna_context_add_items_for_extra (GString              *str,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   const gchar          *prefix,
                                   const gchar           *item,
                                   const gchar          *save_location,
                                   GError              **error);
gboolean
_donna_context_set_item_from_extra (DonnaContextInfo    *info,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   gboolean              is_column_option,
                                   const gchar          *option,
                                   const gchar          *item,
                                   gintptr               current,
                                   const gchar          *save_location,
                                   GError              **error);

/* internal; from provider-config.c */
guint
_donna_config_get_from_column (DonnaConfig *config,
                               const gchar *col_name,
                               const gchar *arr_name,
                               const gchar *tv_name,
                               gboolean     is_tree,
                               const gchar *def_cat,
                               const gchar *opt_name,
                               GType        type);


/* internal helpers to handle aliases/items for treeview/column options whose
 * values are extras */

gboolean
_donna_context_add_items_for_extra (GString             *str,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   const gchar          *prefix,
                                   const gchar          *item,
                                   const gchar          *save_location,
                                   GError              **error)
{
    const DonnaConfigExtra *extra;
    gint i;

    extra = donna_config_get_extra (config, name, error);
    if (!extra)
    {
        g_prefix_error (error, "Failed to get extra '%s' from config: ", name);
        return FALSE;
    }

    if (extra->any.type != type)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Invalid extra type for '%s' in config", name);
        return FALSE;
    }

    if (type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) extra;

        donna_g_string_append_concat (str, prefix, item, "<", NULL);
        for (i = 0; i < e->nb_items; ++i)
            donna_g_string_append_concat (str, prefix, item, ".",
                    e->items[i].in_file, ":@", save_location, ",", NULL);
        g_string_truncate (str, str->len - 1);
        g_string_append_c (str, '>');
    }
    else if (type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
    {
        DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) extra;

        donna_g_string_append_concat (str, prefix, item, "<", NULL);
        for (i = 0; i < e->nb_items; ++i)
            donna_g_string_append_concat (str, prefix, item, ".",
                    e->items[i].in_file, ":@", save_location, ",", NULL);
        g_string_truncate (str, str->len - 1);
        g_string_append_c (str, '>');
    }
    else if (type == DONNA_CONFIG_EXTRA_TYPE_LIST)
    {
        DonnaConfigExtraList *e = (DonnaConfigExtraList *) extra;

        donna_g_string_append_concat (str, prefix, item, "<", NULL);
        for (i = 0; i < e->nb_items; ++i)
            donna_g_string_append_concat (str, prefix, item, ".",
                    e->items[i].value, ":@", save_location, ",", NULL);
        g_string_truncate (str, str->len - 1);
        g_string_append_c (str, '>');
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Unexpected extra type");
        return FALSE;
    }

    return TRUE;
}

gboolean
_donna_context_set_item_from_extra (DonnaContextInfo    *info,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   gboolean              is_column_option,
                                   const gchar          *option,
                                   const gchar          *item,
                                   gintptr               current,
                                   const gchar          *save_location,
                                   GError              **error)
{
    const DonnaConfigExtra *extra;
    const gchar *command;
    gint i;

    extra = donna_config_get_extra (config, name, error);
    if (!extra)
    {
        g_prefix_error (error, "Failed to get extras '%s' from config: ", name);
        return FALSE;
    }

    if (extra->any.type != type)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Invalid extra type for '%s' in config", name);
        return FALSE;
    }

    if (is_column_option)
        command = "command:tv_column_set_option (%o,%R,";
    else
        command = "command:tv_set_option (%o,";

    if (type == DONNA_CONFIG_EXTRA_TYPE_LIST_INT)
    {
        DonnaConfigExtraListInt *e = (DonnaConfigExtraListInt *) extra;

        for (i = 0; i < e->nb_items; ++i)
        {
            if (streq (e->items[i].in_file, item))
            {
                info->name = g_strdup (
                        (e->items[i].label) ? e->items[i].label : e->items[i].in_file);
                info->free_name = TRUE;
                info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
                info->trigger = g_strconcat (command, option, ",",
                        e->items[i].in_file, ",",
                        (save_location) ? save_location : "", ")", NULL);
                info->free_trigger = TRUE;
                if (current == e->items[i].value)
                    info->is_active = TRUE;
                break;
            }
        }

        if (i >= e->nb_items)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "No such value (%s) in extra '%s'",
                    item, name);
            return FALSE;
        }
    }
    else if (type == DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS)
    {
        DonnaConfigExtraListFlags *e = (DonnaConfigExtraListFlags *) extra;

        for (i = 0; i < e->nb_items; ++i)
        {
            if (streq (e->items[i].in_file, item))
            {
                info->name = g_strdup (
                        (e->items[i].label) ? e->items[i].label : e->items[i].in_file);
                info->free_name = TRUE;
                info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
                info->trigger = g_strconcat (command, option, ",",
                        "\",", e->items[i].in_file, "\",",
                        (save_location) ? save_location : "", ")", NULL);
                info->free_trigger = TRUE;
                if (current & e->items[i].value)
                    info->is_active = TRUE;
                break;
            }
        }

        if (i >= e->nb_items)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "No such value (%s) in extra '%s'",
                    item, name);
            return FALSE;
        }
    }
    else if (type == DONNA_CONFIG_EXTRA_TYPE_LIST)
    {
        DonnaConfigExtraList *e = (DonnaConfigExtraList *) extra;

        for (i = 0; i < e->nb_items; ++i)
        {
            if (streq (e->items[i].value, item))
            {
                info->name = g_strdup (
                        (e->items[i].label) ? e->items[i].label : e->items[i].value);
                info->free_name = TRUE;
                info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
                info->trigger = g_strconcat (command, option, ",",
                        e->items[i].value, ",",
                        (save_location) ? save_location : "", ")", NULL);
                info->free_trigger = TRUE;
                if (streq ((gchar *) current, e->items[i].value))
                    info->is_active = TRUE;
                break;
            }
        }

        if (i >= e->nb_items)
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "No such value (%s) in extra '%s'",
                    item, name);
            return FALSE;
        }
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "Unexpected extra type");
        return FALSE;
    }

    return TRUE;
}

/* *** */

static GtkSortType
default_get_default_sort_order (DonnaColumnType    *ct,
                                const gchar        *col_name,
                                const gchar        *arr_name,
                                const gchar        *tv_name,
                                gboolean            is_tree,
                                gpointer            data)
{
    DonnaApp *app;
    const gchar *type;
    gchar buf[55], *b = buf;
    GtkSortType order;

    g_object_get (ct, "app", &app, NULL);
    type = donna_column_type_get_name (ct);
    /* 41 == 55 - strlen ("column_types/") - 1 */
    if (G_UNLIKELY (strlen (type) + 41 >= 41))
        b = g_strconcat ("column_types/", type, NULL);
    else
        strcpy (stpcpy (buf, "column_types/"), type);

    order = (donna_config_get_boolean_column (donna_app_peek_config (app),
                col_name, arr_name, tv_name, is_tree, b, "desc_first", FALSE))
        ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;

    if (G_UNLIKELY (b != buf))
        g_free (b);
    g_object_unref (app);
    return order;
}

static gboolean
default_can_edit (DonnaColumnType    *ct,
                  gpointer            data,
                  DonnaNode          *node,
                  GError            **error)
{
    g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
            "ColumnType '%s': No editing supported",
            donna_column_type_get_name (ct));
    return FALSE;
}

static gboolean
default_edit (DonnaColumnType    *ct,
              gpointer            data,
              DonnaNode          *node,
              GtkCellRenderer   **renderer,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
            "ColumnType '%s': No editing supported",
            donna_column_type_get_name (ct));
    return FALSE;
}

static gboolean
default_set_value (DonnaColumnType    *ct,
                   gpointer            data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
            "ColumnType '%s': No editing supported",
            donna_column_type_get_name (ct));
    return FALSE;
}

static gboolean
default_set_tooltip (DonnaColumnType    *ct,
                     gpointer            data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    return FALSE;
}

static gboolean
helper_can_edit (DonnaColumnType    *ct,
                 const gchar        *property,
                 DonnaNode          *node,
                 GError            **error)
{
    DonnaNodeHasProp has_prop;

    has_prop = donna_node_has_property (node, property);

    if (!(has_prop & DONNA_NODE_PROP_EXISTS))
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NODE_NO_PROP,
                "ColumnType '%s': property '%s' doesn't exist",
                donna_column_type_get_name (ct), property);
        return FALSE;
    }

    if (!(has_prop & DONNA_NODE_PROP_WRITABLE))
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NODE_NOT_WRITABLE,
                "ColumnType '%s': property '%s' isn't writable",
                donna_column_type_get_name (ct), property);
        return FALSE;
    }

    return TRUE;
}

static const gchar *
helper_get_save_location (DonnaColumnType    *ct,
                          const gchar       **extra,
                          gboolean            from_alias,
                          GError            **error)
{
    const gchar *save = "";
    g_assert (extra);

    if (*extra && (from_alias || **extra == '@'))
    {
        gchar *s;
        gsize len;

        if (!from_alias)
            ++*extra;
        s = strchr (*extra, ':');
        if (s)
            len = (gsize) (s - *extra);
        else
            len = strlen (*extra);

        if (len == 0)
            save = "";
        else if (streqn (*extra, "memory", len))
            save = "memory";
        else if (streqn (*extra, "current", len))
            save = "current";
        else if (streqn (*extra, "ask", len))
            save = "ask";
        else if (streqn (*extra, "arr", len))
            save = "arr";
        else if (streqn (*extra, "tree", len))
            save = "tree";
        else if (streqn (*extra, "mode", len))
            save = "mode";
        else if (streqn (*extra, "default", len))
            save = "default";
        else if (streqn (*extra, "save-location", len))
            save = "save-location";
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType '%s': Invalid save location from extra: '%s''",
                    donna_column_type_get_name (ct), *extra);
            return NULL;
        }

        if (!s || s[1] == '\0')
            *extra = NULL;
        else
            *extra = s + 1;
    }

    return save;
}

struct asl
{
    GtkWidget *win;
    GtkWidget *btn_cancel;
    GSList *list;
    DonnaColumnOptionSaveLocation save_location;
    DonnaTreeView *tree;
    gulong sid;
};

static void
btn_clicked (struct asl *asl)
{
    for ( ; asl->list; asl->list = asl->list->next)
    {
        if (gtk_toggle_button_get_active ((GtkToggleButton *) asl->list->data))
        {
            gpointer sl;

            sl = g_object_get_data (asl->list->data, "_from");
            asl->save_location = GPOINTER_TO_UINT (sl);
            gtk_widget_destroy (asl->win);
            return;
        }
    }
    g_warn_if_reached ();
}

static gboolean
key_pressed (struct asl *asl, GdkEventKey *event)
{
    if (event->keyval == GDK_KEY_Escape)
    {
        gtk_widget_activate (asl->btn_cancel);
        return TRUE;
    }

    return FALSE;
}

static void
tree_changed_location (struct asl *asl)
{
    g_signal_handler_disconnect (asl->tree, asl->sid);
    asl->sid = 0;
    gtk_widget_destroy (asl->win);
}

/* this is also used by treeview.c for treeview options. This is why we have a
 * special handling when col_name is NULL */
DonnaColumnOptionSaveLocation
_donna_column_type_ask_save_location (DonnaApp    *app,
                                      const gchar *col_name,
                                      const gchar *arr_name,
                                      const gchar *tv_name,
                                      gboolean     is_tree,
                                      const gchar *def_cat,
                                      const gchar *option,
                                      guint        from)
{
    struct asl asl = { NULL, };
    GMainLoop *loop;
    GtkStyleContext *context;
    GtkWidget *win;
    GtkWidget *hbox;
    GtkWidget *btn;
    GtkWidget *w;
    GtkGrid *grid;
    GtkRadioButton *btn_grp;
    GtkWidget *btn_box;
    gchar *s;
    gint row;
    gboolean is_tree_option = col_name == NULL;

    win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_signal_connect_swapped (win, "key-press-event",
            (GCallback) key_pressed, &asl);
    if (!is_tree_option)
        gtk_widget_set_name (win, "columnoption-save-location");
    else
        gtk_widget_set_name (win, "treeoption-save-location");
    donna_app_add_window (app, (GtkWindow *) win, TRUE);
    gtk_window_set_default_size ((GtkWindow *) win, 420, -1);
    gtk_window_set_decorated ((GtkWindow *) win, FALSE);
    gtk_container_set_border_width ((GtkContainer *) win, 4);

    hbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) win, hbox);

    w = gtk_label_new ("Where do you want to save the new value ?");
    context = gtk_widget_get_style_context (w);
    gtk_style_context_add_class (context, "title");
    gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 0);

    if (!is_tree_option)
        s = g_strdup_printf ("Column options can be saved in different locations, "
                "each with a different reach. Select where the new value for "
                "option '%s' of column '%s' will be saved.",
                option, col_name);
    else
        s = g_strdup_printf ("Tree options can be saved in different locations, "
                "each with a different reach. Select where the new value for "
                "option '%s' will be saved.",
                option);
    w = gtk_label_new (s);
    g_object_set (w, "wrap", TRUE, NULL);
    g_free (s);
    context = gtk_widget_get_style_context (w);
    gtk_style_context_add_class (context, "details");
    gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 0);

    w = gtk_grid_new ();
    grid = (GtkGrid *) w;
    gtk_grid_set_row_homogeneous (grid, TRUE);
    gtk_grid_set_column_homogeneous (grid, TRUE);
    gtk_grid_set_column_spacing (grid, 6);
    gtk_box_pack_start ((GtkBox *) hbox, w, FALSE, FALSE, 0);
    row = 0;

    if (!is_tree_option)
    {
        btn = gtk_radio_button_new (NULL);
        btn_grp = (GtkRadioButton *) btn;
        w = gtk_label_new (NULL);
        gtk_label_set_markup ((GtkLabel *) w, "In <b>current arrangement</b>");
        gtk_container_add ((GtkContainer *) btn, w);
        if (!arr_name)
        {
            gtk_widget_set_sensitive (btn, FALSE);
            gtk_grid_attach (grid, btn, 0, row, 2, 1);
        }
        else
        {
            gtk_grid_attach (grid, btn, 0, row, 1, 1);

            w = gtk_label_new (NULL);
            s = g_strdup_printf ("(<i>%s/columns_options/%s</i>)", arr_name, col_name);
            gtk_label_set_markup ((GtkLabel *) w, s);
            g_free (s);
            gtk_widget_set_halign (w, GTK_ALIGN_START);
            gtk_widget_set_valign (w, GTK_ALIGN_CENTER);
            if (from == _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT)
                g_object_set (btn, "active", TRUE, NULL);
            gtk_grid_attach (grid, w, 1, row, 1, 1);
        }
        g_object_set_data ((GObject *) btn, "_from",
                GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT));
        ++row;

        btn = gtk_radio_button_new_from_widget (btn_grp);
    }
    else
    {
        btn = gtk_radio_button_new (NULL);
        btn_grp = (GtkRadioButton *) btn;
    }
    w = gtk_label_new (NULL);
    s = g_strdup_printf ("In <b>tree view '%s'</b> only", tv_name);
    gtk_label_set_markup ((GtkLabel *) w, s);
    g_free (s);
    gtk_container_add ((GtkContainer *) btn, w);
    gtk_grid_attach (grid, btn, 0, row, 1, 1);
    w = gtk_label_new (NULL);
    if (!is_tree_option)
        s = g_strdup_printf ("(<i>tree_views/%s/columns/%s</i>)", tv_name, col_name);
    else
        s = g_strdup_printf ("(<i>tree_views/%s</i>)", tv_name);
    gtk_label_set_markup ((GtkLabel *) w, s);
    g_free (s);
    gtk_widget_set_halign (w, GTK_ALIGN_START);
    gtk_widget_set_valign (w, GTK_ALIGN_CENTER);
    if (from == _DONNA_CONFIG_COLUMN_FROM_TREE)
        g_object_set (btn, "active", TRUE, NULL);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_TREE));
    ++row;

    btn = gtk_radio_button_new_from_widget (btn_grp);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, (is_tree)
            ? "As <b>mode</b> default (i.e. for <b>trees</b>)"
            : "As <b>mode</b> default (i.e. for <b>lists</b>)");
    gtk_container_add ((GtkContainer *) btn, w);
    gtk_grid_attach (grid, btn, 0, row, 1, 1);
    w = gtk_label_new (NULL);
    if (is_tree_option)
        s = g_strdup_printf ("(<i>defaults/%s</i>)",
                (is_tree) ? "trees": "lists");
    else
        s = g_strdup_printf ("(<i>defaults/%s/columns/%s</i>)",
                (is_tree) ? "trees": "lists", col_name);
    gtk_label_set_markup ((GtkLabel *) w, s);
    g_free (s);
    gtk_widget_set_halign (w, GTK_ALIGN_START);
    gtk_widget_set_valign (w, GTK_ALIGN_CENTER);
    if (from == _DONNA_CONFIG_COLUMN_FROM_MODE)
        g_object_set (btn, "active", TRUE, NULL);
    gtk_grid_attach (grid, w, 1, row, 1, 1);
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_MODE));
    ++row;

    if (!is_tree_option)
    {
        btn = gtk_radio_button_new_from_widget (btn_grp);
        w = gtk_label_new (NULL);
        gtk_label_set_markup ((GtkLabel *) w, "As a new <b>default</b>");
        gtk_container_add ((GtkContainer *) btn, w);
        if (!def_cat)
        {
            gtk_widget_set_sensitive (btn, FALSE);
            gtk_grid_attach (grid, btn, 0, row, 2, 1);
        }
        else
        {
            gtk_grid_attach (grid, btn, 0, row, 1, 1);

            w = gtk_label_new (NULL);
            s = g_strdup_printf ("(<i>defaults/%s</i>)", def_cat);
            gtk_label_set_markup ((GtkLabel *) w, s);
            g_free (s);
            gtk_widget_set_halign (w, GTK_ALIGN_START);
            gtk_widget_set_valign (w, GTK_ALIGN_CENTER);
            if (from == _DONNA_CONFIG_COLUMN_FROM_DEFAULT)
                g_object_set (btn, "active", TRUE, NULL);
            gtk_grid_attach (grid, w, 1, row, 1, 1);
        }
        g_object_set_data ((GObject *) btn, "_from",
                GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT));
        ++row;
    }

    btn = gtk_radio_button_new_from_widget (btn_grp);
    w = gtk_label_new (NULL);
    gtk_label_set_markup ((GtkLabel *) w, "Do <b>not</b> save in configuration, "
            "only apply in memory");
    gtk_container_add ((GtkContainer *) btn, w);
    gtk_grid_attach (grid, btn, 0, row, 1, 1);
    g_object_set_data ((GObject *) btn, "_from",
            GUINT_TO_POINTER (DONNA_COLUMN_OPTION_SAVE_IN_MEMORY));
    ++row;

    asl.win = win;
    asl.list = gtk_radio_button_get_group ((GtkRadioButton *) btn);
    asl.save_location = (guint) -1;

    btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_end ((GtkBox *) hbox, btn_box, FALSE, FALSE, 4);

    w = gtk_button_new_with_label ("Save option");
    gtk_widget_set_can_default (w, TRUE);
    gtk_window_set_default ((GtkWindow *) win, w);
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-ok", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) btn_clicked, &asl);
    gtk_box_pack_end ((GtkBox *) btn_box, w, FALSE, FALSE, 2);

    w = asl.btn_cancel = gtk_button_new_with_label ("Cancel");
    gtk_button_set_image ((GtkButton *) w,
            gtk_image_new_from_icon_name ("gtk-cancel", GTK_ICON_SIZE_MENU));
    g_signal_connect_swapped (w, "clicked", (GCallback) gtk_widget_destroy, win);
    gtk_box_pack_end ((GtkBox *) btn_box, w, FALSE, FALSE, 2);


    if (col_name)
    {
        /* if the tree changes location, we need to abort. Because we start a
         * main loop, the tree could change location, which could mean our
         * arr_name and col_name will point to random memory location */
        asl.tree = donna_app_get_tree_view (app, tv_name);
        asl.sid = g_signal_connect_swapped (asl.tree, "notify::location",
                /* we don't connect gtk_widget_destroy in the off chance the
                 * tree would notify of multiple change of locations */
                (GCallback) tree_changed_location, &asl);
    }

    loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect_swapped (win, "destroy", (GCallback) g_main_loop_quit, loop);

    gtk_widget_show_all (win);
    g_main_loop_run (loop);

    if (asl.sid > 0)
        g_signal_handler_disconnect (asl.tree, asl.sid);
    if (asl.tree)
        g_object_unref (asl.tree);

    return asl.save_location;
}

#define _cfg_set(type, value, location, ...)                                \
    if (!donna_config_set_##type (config, error, value, __VA_ARGS__))       \
    {                                                                       \
        g_prefix_error (error,                                              \
                "ColumnType '%s': Failed to save option '%s': ",            \
                donna_column_type_get_name (ct), option);                   \
        g_object_unref (app);                                               \
        return FALSE;                                                       \
    }
#define cfg_set(location, ...)                                              \
    if (type == G_TYPE_STRING)                                              \
    {                                                                       \
        _cfg_set (string, * (gchar **) value, location, __VA_ARGS__);       \
    }                                                                       \
    else if (type == G_TYPE_BOOLEAN)                                        \
    {                                                                       \
        _cfg_set (boolean, * (gboolean *) value, location, __VA_ARGS__);    \
    }                                                                       \
    else if (type == G_TYPE_INT)                                            \
    {                                                                       \
        _cfg_set (int, * (gint *) value, location, __VA_ARGS__);            \
    }                                                                       \
    else /* G_TYPE_DOUBLE */                                                \
    {                                                                       \
        _cfg_set (double, * (gdouble *) value, location, __VA_ARGS__);      \
    }
static gboolean
helper_set_option (DonnaColumnType    *ct,
                   const gchar        *col_name,
                   const gchar        *arr_name,
                   const gchar        *tv_name,
                   gboolean            is_tree,
                   const gchar        *def_cat,
                   DonnaColumnOptionSaveLocation *save_location,
                   const gchar        *option,
                   GType               type,
                   gpointer            current,
                   gpointer            value,
                   GError            **error)
{
    DonnaApp *app;
    DonnaConfig *config;

    g_return_val_if_fail (type == G_TYPE_STRING || type == G_TYPE_BOOLEAN
            || type == G_TYPE_INT || type == G_TYPE_DOUBLE, FALSE);

    g_object_get (ct, "app", &app, NULL);
    config = donna_app_peek_config (app);

    if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_CURRENT
            || *save_location == DONNA_COLUMN_OPTION_SAVE_IN_ASK)
    {
        guint from = _donna_config_get_from_column (config, col_name,
                arr_name, tv_name, is_tree, def_cat, option, type);

        if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_ASK)
        {
            *save_location = _donna_column_type_ask_save_location (app,
                    col_name, arr_name, tv_name, is_tree, def_cat, option, from);
            if (*save_location == (guint) -1)
            {
                g_object_unref (app);
                return FALSE;
            }
        }
        else
        {
            switch (from)
            {
                case _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT:
                    *save_location = DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT;
                    break;
                case _DONNA_CONFIG_COLUMN_FROM_TREE:
                    *save_location = DONNA_COLUMN_OPTION_SAVE_IN_TREE;
                    break;
                case _DONNA_CONFIG_COLUMN_FROM_MODE:
                    *save_location = DONNA_COLUMN_OPTION_SAVE_IN_MODE;
                    break;
                case _DONNA_CONFIG_COLUMN_FROM_DEFAULT:
                    *save_location = DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT;
                    break;
            }
        }
    }

    if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT && !arr_name)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType '%s': Cannot save option '%s' in arrangement: "
                "No current arrangement available",
                donna_column_type_get_name (ct), option);
        g_object_unref (app);
        return FALSE;
    }
    else if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT && !def_cat)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType '%s': Cannot save option '%s' in defaults: "
                "No default location for option",
                donna_column_type_get_name (ct), option);
        g_object_unref (app);
        return FALSE;
    }

    if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT)
    {
        cfg_set ("arrangement", "%s/columns_options/%s/%s",
                arr_name, col_name, option);
    }
    else if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_TREE)
    {
        cfg_set ("treeview", "tree_views/%s/columns/%s/%s",
                tv_name, col_name, option);
    }
    else if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_MODE)
    {
        cfg_set ("column", "defaults/%s/columns/%s/%s",
                (is_tree) ? "trees" : "lists", col_name, option);
    }
    else if (*save_location == DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT)
    {
        cfg_set ("defaults", "defaults/%s/%s", def_cat, option);
    }

    g_object_unref (app);
    return TRUE;
}
#undef cfg_set
#undef _cfg_set

static gchar *
helper_get_set_option_trigger (const gchar  *option,
                               const gchar  *value,
                               gboolean      quote_value,
                               const gchar  *ask_title,
                               const gchar  *ask_details,
                               const gchar  *ask_current,
                               const gchar  *save_location)
{
    GString *str;

    g_return_val_if_fail (value != NULL || ask_title != NULL, NULL);

    str = g_string_new ("command:tv_column_set_option (%o,%R,");
    g_string_append (str, option);
    g_string_append_c (str, ',');
    if (quote_value)
        donna_g_string_append_quoted (str, value, TRUE);
    else if (value)
        g_string_append (str, value);
    else
    {
        g_string_append (str, "@ask_text(");
        g_string_append (str, ask_title);
        if (ask_details)
        {
            g_string_append_c (str, ',');
            donna_g_string_append_quoted (str, ask_details, TRUE);
        }
        else if (ask_current)
            g_string_append_c (str, ',');
        if (ask_current)
        {
            g_string_append_c (str, ',');
            donna_g_string_append_quoted (str, ask_current, TRUE);
        }
        g_string_append_c (str, ')');
    }
    if (*save_location != '\0')
    {
        g_string_append_c (str, ',');
        g_string_append (str, save_location);
    }
    g_string_append_c (str, ')');
    return g_string_free (str, FALSE);
}

G_DEFINE_INTERFACE (DonnaColumnType, donna_column_type, G_TYPE_OBJECT)

static void
donna_column_type_default_init (DonnaColumnTypeInterface *interface)
{
    interface->helper_can_edit                  = helper_can_edit;
    interface->helper_get_save_location         = helper_get_save_location;
    interface->helper_set_option                = helper_set_option;
    interface->helper_get_set_option_trigger    = helper_get_set_option_trigger;

    interface->get_default_sort_order           = default_get_default_sort_order;
    interface->can_edit                         = default_can_edit;
    interface->edit                             = default_edit;
    interface->set_value                        = default_set_value;
    interface->set_tooltip                      = default_set_tooltip;

    g_object_interface_install_property (interface,
            g_param_spec_object ("app", "app", "Application",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

const gchar *
donna_column_type_get_name (DonnaColumnType *ct)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), NULL);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_name != NULL, NULL);

    return (*interface->get_name) (ct);
}

const gchar *
donna_column_type_get_renderers (DonnaColumnType  *ct)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), NULL);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_renderers != NULL, NULL);

    return (*interface->get_renderers) (ct);
}

DonnaColumnTypeNeed
donna_column_type_refresh_data (DonnaColumnType   *ct,
                                const gchar       *col_name,
                                const gchar       *arr_name,
                                const gchar       *tv_name,
                                gboolean           is_tree,
                                gpointer          *data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), DONNA_COLUMN_TYPE_NEED_NOTHING);
    g_return_val_if_fail (col_name != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);
    g_return_val_if_fail (data != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);
    g_return_val_if_fail (interface->refresh_data != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);

    return (*interface->refresh_data) (ct, col_name, arr_name, tv_name, is_tree, data);
}

void
donna_column_type_free_data (DonnaColumnType   *ct,
                             gpointer           data)
{
    DonnaColumnTypeInterface *interface;

    g_return_if_fail (DONNA_IS_COLUMN_TYPE (ct));

    if (G_UNLIKELY (!data))
        return;

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->free_data != NULL);

    return (*interface->free_data) (ct, data);
}

GPtrArray *
donna_column_type_get_props (DonnaColumnType   *ct,
                             gpointer           data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), NULL);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_props != NULL, NULL);

    return (*interface->get_props) (ct, data);
}

GtkSortType
donna_column_type_get_default_sort_order (DonnaColumnType   *ct,
                                          const gchar       *col_name,
                                          const gchar       *arr_name,
                                          const gchar       *tv_name,
                                          gboolean           is_tree,
                                          gpointer           data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), GTK_SORT_ASCENDING);
    g_return_val_if_fail (tv_name != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (col_name != NULL, GTK_SORT_ASCENDING);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, GTK_SORT_ASCENDING);
    g_return_val_if_fail (interface->get_default_sort_order != NULL, GTK_SORT_ASCENDING);

    return (*interface->get_default_sort_order) (ct, col_name, arr_name,
            tv_name, is_tree, data);
}

gboolean
donna_column_type_can_edit (DonnaColumnType   *ct,
                            gpointer           data,
                            DonnaNode         *node,
                            GError           **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->can_edit != NULL, FALSE);

    return (*interface->can_edit) (ct, data, node, error);
}

gboolean
donna_column_type_edit (DonnaColumnType   *ct,
                        gpointer           data,
                        DonnaNode         *node,
                        GtkCellRenderer  **renderers,
                        renderer_edit_fn   renderer_edit,
                        gpointer           re_data,
                        DonnaTreeView     *treeview,
                        GError           **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (renderers != NULL && GTK_IS_CELL_RENDERER (renderers[0]), FALSE);
    g_return_val_if_fail (renderer_edit != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->edit != NULL, FALSE);

    return (*interface->edit) (ct, data, node, renderers,
            renderer_edit, re_data, treeview, error);
}

void
donna_column_type_get_options (DonnaColumnType    *ct,
                               DonnaColumnOptionInfo **options,
                               guint              *nb_options)
{
    DonnaColumnTypeInterface *interface;

    g_return_if_fail (DONNA_IS_COLUMN_TYPE (ct));
    g_return_if_fail (options != NULL);
    g_return_if_fail (nb_options != NULL);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->get_options != NULL, FALSE);

    (*interface->get_options) (ct, options, nb_options);
}

DonnaColumnTypeNeed
donna_column_type_set_option (DonnaColumnType   *ct,
                              const gchar       *col_name,
                              const gchar       *arr_name,
                              const gchar       *tv_name,
                              gboolean           is_tree,
                              gpointer           data,
                              const gchar       *option,
                              gpointer           value,
                              gboolean           toggle,
                              DonnaColumnOptionSaveLocation save_location,
                              GError           **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), DONNA_COLUMN_TYPE_NEED_NOTHING);
    g_return_val_if_fail (tv_name != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);
    g_return_val_if_fail (col_name != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);
    g_return_val_if_fail (option != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, DONNA_COLUMN_TYPE_NEED_NOTHING);

    if (interface->set_option == NULL)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
                "ColumnType '%s': Setting column option not supported",
                donna_column_type_get_name (ct));
        return DONNA_COLUMN_TYPE_NEED_NOTHING;
    }

    return (*interface->set_option) (ct, col_name, arr_name, tv_name, is_tree,
            data, option, value, toggle, save_location, error);
}

gboolean
donna_column_type_set_value (DonnaColumnType   *ct,
                             gpointer           data,
                             GPtrArray         *nodes,
                             const gchar       *value,
                             DonnaNode         *node_ref,
                             DonnaTreeView     *treeview,
                             GError           **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (!node_ref || DONNA_IS_NODE (node_ref), FALSE);
    g_return_val_if_fail (DONNA_IS_TREE_VIEW (treeview), FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_value != NULL, FALSE);

    return (*interface->set_value) (ct, data, nodes, value, node_ref, treeview, error);
}

GPtrArray *
donna_column_type_render (DonnaColumnType   *ct,
                          gpointer           data,
                          guint              index,
                          DonnaNode         *node,
                          GtkCellRenderer   *renderer)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);
    g_return_val_if_fail (GTK_IS_CELL_RENDERER (renderer), NULL);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->render != NULL, NULL);

    return (*interface->render) (ct, data, index, node, renderer);
}

gboolean
donna_column_type_set_tooltip (DonnaColumnType   *ct,
                               gpointer           data,
                               guint              index,
                               DonnaNode         *node,
                               GtkTooltip        *tooltip)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);
    g_return_val_if_fail (GTK_IS_TOOLTIP (tooltip), FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_tooltip != NULL, FALSE);

    return (*interface->set_tooltip) (ct, data, index, node, tooltip);
}

gint
donna_column_type_node_cmp (DonnaColumnType   *ct,
                            gpointer           data,
                            DonnaNode         *node1,
                            DonnaNode         *node2)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node1), 0);
    g_return_val_if_fail (DONNA_IS_NODE (node2), 0);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->node_cmp != NULL, 0);

    return (*interface->node_cmp) (ct, data, node1, node2);
}

gboolean
donna_column_type_refresh_filter_data (DonnaColumnType    *ct,
                                       const gchar        *filter,
                                       gpointer           *filter_data,
                                       GError            **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (filter != NULL, FALSE);
    g_return_val_if_fail (filter_data != NULL, FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    if (!interface->refresh_filter_data)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType '%s': no filtering supported",
                donna_column_type_get_name (ct));
        return FALSE;
    }

    return (*interface->refresh_filter_data) (ct, filter, filter_data, error);
}

gboolean
donna_column_type_is_filter_match (DonnaColumnType   *ct,
                                   gpointer           data,
                                   gpointer           filter_data,
                                   DonnaNode         *node)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (filter_data != NULL, FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->is_filter_match != NULL, FALSE);

    return (*interface->is_filter_match) (ct, data, filter_data, node);
}

void
donna_column_type_free_filter_data (DonnaColumnType  *ct,
                                    gpointer          filter_data)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), 0);

    if (G_UNLIKELY (!filter_data))
        return;

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);

    g_return_val_if_fail (interface != NULL, 0);
    g_return_val_if_fail (interface->free_filter_data != NULL, 0);

    return (*interface->free_filter_data) (ct, filter_data);
}

gchar *
donna_column_type_get_context_alias (DonnaColumnType   *ct,
                                     gpointer           data,
                                     const gchar       *alias,
                                     const gchar       *extra,
                                     DonnaContextReference reference,
                                     DonnaNode         *node_ref,
                                     get_sel_fn         get_sel,
                                     gpointer           get_sel_data,
                                     const gchar       *prefix,
                                     GError           **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), NULL);
    g_return_val_if_fail (alias != NULL, NULL);
    g_return_val_if_fail (prefix != NULL, NULL);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);
    g_return_val_if_fail (interface != NULL, NULL);

    if (interface->get_context_alias == NULL)
    {
        /* all column types should support an alias "options" so let's resolve
         * that to an empty string, i.e. nothing (but not an error) */
        if (streq (alias, "options"))
            return (gchar *) "";

        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType '%s': No context alias supported",
                donna_column_type_get_name (ct));
        return NULL;
    }

    return (*interface->get_context_alias) (ct, data,
            alias, extra, reference, node_ref, get_sel, get_sel_data,
            prefix, error);
}

gboolean
donna_column_type_get_context_item_info (DonnaColumnType   *ct,
                                         gpointer           data,
                                         const gchar       *item,
                                         const gchar       *extra,
                                         DonnaContextReference reference,
                                         DonnaNode         *node_ref,
                                         get_sel_fn         get_sel,
                                         gpointer           get_sel_data,
                                         DonnaContextInfo  *info,
                                         GError           **error)
{
    DonnaColumnTypeInterface *interface;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE (ct), FALSE);
    g_return_val_if_fail (item != NULL, FALSE);
    g_return_val_if_fail (info != NULL, FALSE);
    g_return_val_if_fail (node_ref == NULL || DONNA_IS_NODE (node_ref), FALSE);
    g_return_val_if_fail (get_sel != NULL, FALSE);

    interface = DONNA_COLUMN_TYPE_GET_INTERFACE (ct);
    g_return_val_if_fail (interface != NULL, FALSE);

    if (interface->get_context_item_info == NULL)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType '%s': No context item supported",
                donna_column_type_get_name (ct));
        return FALSE;
    }

    return (*interface->get_context_item_info) (ct, data,
            item, extra, reference, node_ref, get_sel, get_sel_data,
            info, error);
}

/**
 * donna_column_type_new_floating_window:
 * @tree: #DonnaTreeView where the column is
 * @destroy_on_sel_changed: Whether to destroy the window on selection change
 *
 * Helper function to create a little window that can be used to perform some
 * property editing on a given node (or selection of nodes).
 * This will create the window, attach it to @tree, remove decorations, set
 * %GDK_WINDOW_TYPE_HINT_UTILITY, set position to center of parent, set a border
 * of 6 pixels, make it non-resizable, and make it destroyed on location change
 * on @tree, as well as selection change if @destroy_on_sel_changed is %TRUE.
 *
 * Make sure to call donna_app_set_floating_window() on the returned window
 * (before making it visible) else the placement might not be correct.
 *
 * Return: (transfer full): A new g_object_ref_sink()ed #GtkWindow
 */
GtkWindow *
donna_column_type_new_floating_window (DonnaTreeView *tree,
                                       gboolean       destroy_on_sel_changed)
{
    GtkWindow *win;

    win = (GtkWindow *) gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (win, FALSE);
    gtk_window_set_type_hint (win, GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_attached_to (win, (GtkWidget *) tree);
    gtk_window_set_position (win, GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_resizable (win, FALSE);
    g_object_set (win, "border-width", 6, NULL);
    g_object_ref_sink (win);
    g_signal_connect_swapped (tree, "notify::location",
            (GCallback) gtk_widget_destroy, win);
    if (destroy_on_sel_changed)
    {
        GtkTreeSelection *sel;

        sel = gtk_tree_view_get_selection ((GtkTreeView *) tree);
        g_signal_connect_swapped (sel, "changed",
                (GCallback) gtk_widget_destroy, win);
    }
    return win;
}
