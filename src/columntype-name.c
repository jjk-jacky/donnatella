/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-name.c
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

#include <glib-object.h>
#include <string.h>
#include "columntype.h"
#include "columntype-name.h"
#include "renderer.h"
#include "node.h"
#include "app.h"
#include "conf.h"
#include "sort.h"
#include "misc.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:columntype-name
 * @Short_description: To show the icon & name of the node. 
 *
 * Column type to show the icon & name of the node. You will usually want to
 * have only one column of this type in each tree, and one in each lists.
 *
 * # Options # {#ct-name-options}
 *
 * The only options are regarding sorting capabilities :
 *
 * - `locale_based` (boolean) : Whether to use a locale-based sort algorithm or
 *   not.  The former is probably better to account for locales with special
 *   character handling, such as accents, but doesn't provide as many options as
 *   donna's own sorting algorithm.
 * - `natural_order` (boolean) : Whether to use natural order (1, 2, 11) or not.
 * - `dot_first` (boolean) : Whether to put "dot files" (i.e. nodes with a dot
 *   as first character in their names) first or not.
 *   Note that even with this option disabled, they might be sorted first - or
 *   before non-dotted names at least - by the sort algorithm.
 *
 * Using the locale-based sort algorithm:
 * - `special_first` (boolean) : Whether to place nodes whose names begins with
 *   a "special" character first or not. Special here means non alphanumeric.
 *
 * Using donna's own (non locale-based) algorithm:
 * - `dot_mixed` (boolean) : Alongside `dot_first` set to `false`, this will
 *   have dot files mixed in, i.e. ignoring the dot when sorting.
 * - `case_sensitive` (boolean) : Whether to be case sensitive or not. Note that
 *   with locale-based sort algorithm, this depends on the locale.
 * - `ignore_spunct` (boolean) : Whether to ignore space and punctuation/symbol
 *   characters or not. (Much like `dot_mixed` ignores dots.)
 *
 *
 * # Filtering # {#ct-name-filtering}
 *
 * If the filter starts with either the plus or minus sign, it must then be
 * followed by either 'c' or 'i' to only match containers or items,
 * respectively.  You can also use 'd' (directory) or 'f' (file) as well.
 *
 * Else, it must be a #DonnaPattern<!-- -->s, which will be matched against the
 * name. See donna_pattern_new() for more.
 */

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct tv_col_data
{
    gchar               *collate_key;
    gboolean             is_locale_based;
    DonnaSortOptions     options;
    /* not used in strcmp_ext so included in DonnaSortOptions */
    gboolean             sort_special_first;
};

struct _DonnaColumnTypeNamePrivate
{
    DonnaApp                    *app;
    /* domains we're connected for node being renamed, when we use node_key */
    GPtrArray                   *domains;
};

static void             ct_name_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_name_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_name_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_name_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_name_get_renderers       (DonnaColumnType    *ct);
static void             ct_name_get_options         (DonnaColumnType    *ct,
                                                     DonnaColumnOptionInfo **options,
                                                     guint              *nb_options);
static DonnaColumnTypeNeed ct_name_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_name_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_name_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_name_can_edit            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static gboolean         ct_name_edit                (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static gboolean         ct_name_set_value           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     GPtrArray          *nodes,
                                                     const gchar        *value,
                                                     DonnaNode          *node_ref,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_name_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gboolean         ct_name_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gint             ct_name_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_name_refresh_filter_data (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     GError            **error);
static gboolean         ct_name_is_filter_match     (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     gpointer            filter_data,
                                                     DonnaNode          *node);
static void             ct_name_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_name_set_option       (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     gpointer            value,
                                                     gboolean            toggle,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_name_get_context_alias   (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_name_get_context_item_info (
                                                     DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *item,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     DonnaContextInfo  *info,
                                                     GError           **error);

static void
ct_name_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_name_get_name;
    interface->get_renderers            = ct_name_get_renderers;
    interface->get_options              = ct_name_get_options;
    interface->refresh_data             = ct_name_refresh_data;
    interface->free_data                = ct_name_free_data;
    interface->get_props                = ct_name_get_props;
    interface->can_edit                 = ct_name_can_edit;
    interface->edit                     = ct_name_edit;
    interface->set_value                = ct_name_set_value;
    interface->render                   = ct_name_render;
    interface->set_tooltip              = ct_name_set_tooltip;
    interface->node_cmp                 = ct_name_node_cmp;
    interface->refresh_filter_data      = ct_name_refresh_filter_data;
    interface->is_filter_match          = ct_name_is_filter_match;
    interface->free_filter_data         = ct_name_free_filter_data;
    interface->set_option               = ct_name_set_option;
    interface->get_context_alias        = ct_name_get_context_alias;
    interface->get_context_item_info    = ct_name_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeName, donna_column_type_name,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_name_column_type_init)
        )

static void
donna_column_type_name_class_init (DonnaColumnTypeNameClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_name_set_property;
    o_class->get_property   = ct_name_get_property;
    o_class->finalize       = ct_name_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeNamePrivate));
}

static void
donna_column_type_name_init (DonnaColumnTypeName *ct)
{
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_NAME,
            DonnaColumnTypeNamePrivate);
}

static void
ct_name_finalize (GObject *object)
{
    DonnaColumnTypeNamePrivate *priv;

    priv = DONNA_COLUMN_TYPE_NAME (object)->priv;
    DONNA_DEBUG (MEMORY, NULL,
            g_debug ("ColumnType 'name' finalizing"));

    g_object_unref (priv->app);
    if (priv->domains)
        g_ptr_array_free (priv->domains, TRUE);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_name_parent_class)->finalize (object);
}

static void
ct_name_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_NAME (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_name_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_NAME (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_name_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_NAME (ct), NULL);
    return "name";
}

static const gchar *
ct_name_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_NAME (ct), NULL);
    return "pt";
}

static void
ct_name_get_options (DonnaColumnType    *ct,
                     DonnaColumnOptionInfo **options,
                     guint              *nb_options)
{
    static DonnaColumnOptionInfo o[] = {
        { "locale_based",       G_TYPE_BOOLEAN,     NULL },
        { "natural_order",      G_TYPE_BOOLEAN,     NULL },
        { "dot_first",          G_TYPE_BOOLEAN,     NULL },
        { "special_first",      G_TYPE_BOOLEAN,     NULL },
        { "dot_mixed",          G_TYPE_BOOLEAN,     NULL },
        { "case_sensitive",     G_TYPE_BOOLEAN,     NULL },
        { "ignore_spunct",      G_TYPE_BOOLEAN,     NULL }
    };

    *options = o;
    *nb_options = G_N_ELEMENTS (o);
}

#define check_option(opt_name_lower, opt_name_upper, value, def_val)          \
    if (donna_config_get_boolean_column (config, col_name, arr_name, tv_name, \
                is_tree, "sort", opt_name_lower, def_val) == value)           \
    {                                                                         \
        if (!(data->options & opt_name_upper))                                \
        {                                                                     \
            need |= DONNA_COLUMN_TYPE_NEED_RESORT;                            \
            data->options |= opt_name_upper;                                  \
        }                                                                     \
    }                                                                         \
    else if (data->options & opt_name_upper)                                  \
    {                                                                         \
        need |= DONNA_COLUMN_TYPE_NEED_RESORT;                                \
        data->options &= (DonnaSortOptions) ~opt_name_upper;                  \
    }

static DonnaColumnTypeNeed
ct_name_refresh_data (DonnaColumnType    *ct,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      const gchar        *tv_name,
                      gboolean            is_tree,
                      gpointer           *_data)
{
    DonnaColumnTypeName *ctname = DONNA_COLUMN_TYPE_NAME (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;

    config = donna_app_peek_config (ctname->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    if (data->is_locale_based != donna_config_get_boolean_column (config,
                col_name, arr_name, tv_name, is_tree, "sort",
                "locale_based", FALSE))
    {
        need |= DONNA_COLUMN_TYPE_NEED_RESORT;
        data->is_locale_based = !data->is_locale_based;

        if (data->is_locale_based)
        {
            g_free (data->collate_key);
            data->collate_key = g_strdup_printf ("%s/%s/%s/utf8-collate-key",
                        tv_name, col_name, arr_name);
        }
        else
        {
            g_free (data->collate_key);
            data->collate_key = NULL;
        }
    }

    check_option ("natural_order",  DONNA_SORT_NATURAL_ORDER,   TRUE, TRUE);
    check_option ("dot_first",      DONNA_SORT_DOT_FIRST,       TRUE, TRUE);

    if (data->is_locale_based)
    {
        if (data->sort_special_first != donna_config_get_boolean_column (config,
                    col_name, arr_name, tv_name, is_tree, "sort",
                    "special_first", TRUE))
        {
            need |= DONNA_COLUMN_TYPE_NEED_RESORT;
            data->sort_special_first = !data->sort_special_first;
        }
    }
    else
    {
        check_option ("dot_mixed",      DONNA_SORT_DOT_MIXED,        TRUE,  FALSE);
        check_option ("case_sensitive", DONNA_SORT_CASE_INSENSITIVE, FALSE, FALSE);
        check_option ("ignore_spunct",  DONNA_SORT_IGNORE_SPUNCT,    TRUE,  FALSE);
    }

    return need;
}

static void
ct_name_free_data (DonnaColumnType    *ct,
                   gpointer            data)
{
    g_free (((struct tv_col_data *) data)->collate_key);
    g_free (data);
}

static GPtrArray *
ct_name_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_NAME (ct), NULL);

    props = g_ptr_array_new_full (2, g_free);
    g_ptr_array_add (props, g_strdup ("name"));
    g_ptr_array_add (props, g_strdup ("icon"));

    return props;
}

struct editing_data
{
    DonnaColumnTypeName *ctname;
    DonnaTreeView       *tree;
    DonnaNode           *node;
    gulong               editing_started_sid;
    gulong               editing_done_sid;
    gulong               key_press_event_sid;
};

static gboolean
set_value (const gchar      *property,
           const gchar      *value,
           DonnaNode        *node,
           DonnaTreeView    *tree,
           GError          **error)
{
    GValue v = G_VALUE_INIT;
    gchar *current;

    g_return_val_if_fail (streq (property, "name"), FALSE);

    current = donna_node_get_name (node);
    if (streq (current, value))
    {
        g_free (current);
        return TRUE;
    }
    g_free (current);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    if (!donna_tree_view_set_node_property (tree, node, property, &v, error))
    {
        gchar *fl = donna_node_get_full_location (node);
        g_prefix_error (error, "ColumnType 'name': Unable to rename '%s' to '%s'",
                fl, value);
        g_free (fl);
        g_value_unset (&v);
        return FALSE;
    }
    g_value_unset (&v);
    return TRUE;
}

static void
editing_done_cb (GtkCellEditable *editable, struct editing_data *data)
{
    gboolean canceled;

    g_signal_handler_disconnect (editable, data->editing_done_sid);

    g_object_get (editable, "editing-canceled", &canceled, NULL);
    if (!canceled)
    {
        GError *err = NULL;

        if (G_UNLIKELY (!GTK_IS_ENTRY (editable)))
        {
            gchar *fl = donna_node_get_full_location (data->node);
            donna_app_show_error (data->ctname->priv->app, NULL,
                    "ColumnType name: Unable to change property 'name' for '%s': "
                    "Editable widget isn't a GtkEntry", fl);
            g_free (fl);
            g_free (data);
            return;
        }

        if (!set_value ("name", gtk_entry_get_text ((GtkEntry *) editable),
                    data->node, data->tree, &err))
        {
            donna_app_show_error (data->ctname->priv->app, err, NULL);
            g_clear_error (&err);
            g_free (data);
            return;
        }
    }

    g_free (data);
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct editing_data *data)
{
    gchar *name;
    gint   len;
    gint   dot;
    gchar *s;

    g_signal_handler_disconnect (renderer, data->editing_started_sid);
    data->editing_started_sid = 0;

    data->editing_done_sid = g_signal_connect (editable, "editing-done",
            (GCallback) editing_done_cb, data);
    data->key_press_event_sid = g_signal_connect (editable, "key-press-event",
            (GCallback) _key_press_ctrl_a_cb, NULL);

    /* do not select the whole name */
    g_object_set (gtk_widget_get_settings ((GtkWidget *) editable),
            "gtk-entry-select-on-focus", FALSE, NULL);
    /* locate the dot before the extension */
    name = donna_node_get_name (data->node);
    dot = -1;
    for (len = 1, s = g_utf8_next_char (name);
            *s != '\0';
            ++len, s = g_utf8_next_char (s))
    {
        if (*s == '.')
            dot = len;
    }
    g_free (name);
    /* select only up to the .ext, or all if no .ext found */
    gtk_editable_select_region ((GtkEditable *) editable, 0, dot);
}

static gboolean
ct_name_can_edit (DonnaColumnType    *ct,
                  gpointer            data,
                  DonnaNode          *node,
                  GError            **error)
{
    return DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            "name", node, error);
}

static gboolean
ct_name_edit (DonnaColumnType    *ct,
              gpointer            data,
              DonnaNode          *node,
              GtkCellRenderer   **renderers,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    struct editing_data *ed;

    if (!ct_name_can_edit (ct, data, node, error))
        return FALSE;

    ed = g_new0 (struct editing_data, 1);
    ed->ctname    = (DonnaColumnTypeName *) ct;
    ed->tree      = treeview;
    ed->node      = node;
    ed->editing_started_sid = g_signal_connect (renderers[1],
            "editing-started",
            (GCallback) editing_started_cb, ed);

    g_object_set (renderers[1], "editable", TRUE, NULL);
    if (!renderer_edit (renderers[1], re_data))
    {
        g_signal_handler_disconnect (renderers[1], ed->editing_started_sid);
        g_free (ed);
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType 'name': Failed to put renderer in edit mode");
        return FALSE;
    }
    return TRUE;
}

static gboolean
ct_name_set_value (DonnaColumnType    *ct,
                   gpointer            data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    if (nodes->len != 1)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
                "ColumnType 'name': Can only set value to one node at a time");
        return FALSE;
    }

    if (!ct_name_can_edit (ct, data, nodes->pdata[0], error))
        return FALSE;

    return set_value ("name", value, nodes->pdata[0], treeview, error);
}

static GPtrArray *
ct_name_render (DonnaColumnType    *ct,
                gpointer            data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_NAME (ct), NULL);

    if (index == 1)
    {
        DonnaNodeHasValue has_value;
        GIcon *icon;

        has_value = donna_node_get_icon (node, FALSE, &icon);
        if (has_value == DONNA_NODE_VALUE_SET)
        {
            GtkIconInfo *info;

            info = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_default (),
                    icon, 16, GTK_ICON_LOOKUP_GENERIC_FALLBACK);
            g_object_unref (icon);
            if (info)
            {
                g_object_unref (info);
                g_object_set (renderer, "visible", TRUE, "gicon", icon, NULL);
                return NULL;
            }

            /* fallthrough if lookup failed, so instead of showing tke "broken"
             * image, we can default to the file/folder one */
        }
        else if (has_value == DONNA_NODE_VALUE_NEED_REFRESH)
        {
            GPtrArray *arr;

            arr = g_ptr_array_sized_new (1);
            g_ptr_array_add (arr, (gpointer) "icon");
            return arr;
        }

        if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
            g_object_set (renderer,
                    "visible",      TRUE,
                    "icon-name",    "text-x-generic",
                    NULL);
        else /* DONNA_NODE_CONTAINER */
            g_object_set (renderer,
                    "visible",      TRUE,
                    "icon-name",    "folder",
                    NULL);
    }
    else /* index == 2 */
    {
        gchar *name;

        name = donna_node_get_name (node);
        g_object_set (renderer,
                "visible",      TRUE,
                "text",         name,
                "ellipsize",    PANGO_ELLIPSIZE_END,
                "ellipsize-set",TRUE,
                NULL);
        donna_renderer_set (renderer, "ellipsize-set", NULL);
        g_free (name);
    }

    return NULL;
}

static void
node_updated_cb (DonnaProvider  *provider,
                 DonnaNode      *node,
                 const gchar    *name,
                 gchar          *key)
{
    /* removes the data for key */
    g_object_set_data (G_OBJECT (node), key, NULL);
}

static gboolean
ct_name_set_tooltip (DonnaColumnType    *ct,
                     gpointer            data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    gchar *s;
    DonnaNodeHasValue has;

    /* FIXME:
     * 1 (icon) : show full-name (using location as fallback only)
     * 2 (name) : show name if ellipsed, else no tooltip. Not sure how to find
     * out if the text was ellipsed or not... */

    if (index <= 1)
    {
        has = donna_node_get_full_name (node, FALSE, &s);
        if (has == DONNA_NODE_VALUE_NONE /*FIXME*/||has==DONNA_NODE_VALUE_NEED_REFRESH)
            s = donna_node_get_location (node);
        /* FIXME: if NEED_REFRESH do a task and whatnot? */
        else if (has != DONNA_NODE_VALUE_SET)
            return FALSE;
    }
    else
        s = donna_node_get_name (node);

    gtk_tooltip_set_text (tooltip, s);
    g_free (s);
    return TRUE;
}

static inline gchar *
get_node_key (DonnaColumnTypeName   *ctname,
              struct tv_col_data    *data,
              DonnaNode             *node)
{
    DonnaColumnTypeNamePrivate *priv = ctname->priv;
    gchar *key;
    gboolean dot_first = data->options & DONNA_SORT_DOT_FIRST;
    gboolean natural_order = data->options & DONNA_SORT_NATURAL_ORDER;

    key = g_object_get_data (G_OBJECT (node), data->collate_key);
    /* no key, or invalid (options changed) */
    if (!key || *key != donna_sort_get_options_char (dot_first,
                data->sort_special_first,
                natural_order))
    {
        gchar *name;

        /* if we're installing the key (i.e. not updating an invalid one) we
         * need to make sure we're listening on the provider's
         * node-updated::name signal, to remove the key on rename */
        if (!key)
        {
            const gchar *domain;
            guint i;

            if (!priv->domains)
                priv->domains = g_ptr_array_new ();
            domain = donna_node_get_domain (node);
            for (i = 0; i < priv->domains->len; ++i)
                if (streq (domain, priv->domains->pdata[i]))
                    break;
            /* no match, must connect */
            if (i >= priv->domains->len)
            {
                /* FIXME? (not actually needed since our cb is "self-contained")
                 * - also connect to a new signal "destroy" when provider is
                 *   being finalized. in handler, we remove it from the ptrarr
                 * - when we're finalized, disconnect all hanlers */
                g_signal_connect_data (donna_node_peek_provider (node),
                        "node-updated::name",
                        G_CALLBACK (node_updated_cb),
                        g_strdup (data->collate_key),
                        (GClosureNotify) g_free,
                        0);

                g_ptr_array_add (priv->domains, (gpointer) domain);
            }
        }

        name = donna_node_get_name (node);
        key = donna_sort_get_utf8_collate_key (name, -1,
                dot_first, data->sort_special_first, natural_order);
        g_free (name);
        g_object_set_data_full (G_OBJECT (node), data->collate_key, key, g_free);
    }

    return key + 1; /* skip options_char */
}

static gint
ct_name_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    gchar *name1;
    gchar *name2;
    gint ret;

    if (data->is_locale_based)
    {
        DonnaColumnTypeName *ctname = DONNA_COLUMN_TYPE_NAME (ct);
        return strcmp (get_node_key (ctname, data, node1),
                       get_node_key (ctname, data, node2));
    }

    name1 = donna_node_get_name (node1);
    name2 = donna_node_get_name (node2);
    ret = donna_strcmp (name1, name2, data->options);
    g_free (name1);
    g_free (name2);
    return ret;
}

struct filter_data
{
    gboolean is_pattern;
    union {
        DonnaPattern *pattern;
        gboolean match_containers;
    };
};

static gboolean
ct_name_refresh_filter_data (DonnaColumnType    *ct,
                             const gchar        *filter,
                             gpointer           *filter_data,
                             GError            **error)
{
    struct filter_data *fd = *filter_data;

    if (!fd)
        fd = *filter_data = g_new (struct filter_data, 1);
    else if (fd && fd->is_pattern)
        donna_pattern_unref (fd->pattern);

    fd->is_pattern = !(*filter == '+' || *filter == '-');
    if (fd->is_pattern)
    {
        fd->pattern = donna_app_get_pattern (((DonnaColumnTypeName *) ct)->priv->app,
                filter, error);
        if (!fd->pattern)
        {
            g_free (fd);
            *filter_data = NULL;
            return FALSE;
        }
    }
    else
    {
        if (streq (filter + 1, "c") || streq (filter + 1, "d"))
            fd->match_containers = TRUE;
        else if (streq (filter + 1, "i") || streq (filter + 1, "f"))
            fd->match_containers = FALSE;
        else
        {
            g_free (fd);
            g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                    DONNA_COLUMN_TYPE_ERROR_INVALID_SYNTAX,
                    "ColumnType 'name': Invalid filter syntax: "
                    "'%c' must be followed by 'c' (or 'd') to match containers (directory) "
                    "or 'i' (or 'f') to match items (files), given: %s",
                    *filter, filter + 1);
            *filter_data = NULL;
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
ct_name_is_filter_match (DonnaColumnType    *ct,
                         gpointer            data,
                         gpointer            filter_data,
                         DonnaNode          *node)
{
    struct filter_data *fd = filter_data;
    gboolean ret;

    if (fd->is_pattern)
    {
        gchar *name;

        name = donna_node_get_name (node);
        ret = donna_pattern_is_match (fd->pattern, name);
        g_free (name);
    }
    else
    {
        DonnaNodeType type;

        type = donna_node_get_node_type (node);
        if (fd->match_containers)
            ret = type == DONNA_NODE_CONTAINER;
        else
            ret = type == DONNA_NODE_ITEM;
    }

    return ret;
}

static void
ct_name_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    struct filter_data *fd = filter_data;

    if (fd->is_pattern)
        donna_pattern_unref (fd->pattern);
    g_free (fd);
}

static DonnaColumnTypeNeed
ct_name_set_option (DonnaColumnType    *ct,
                    const gchar        *col_name,
                    const gchar        *arr_name,
                    const gchar        *tv_name,
                    gboolean            is_tree,
                    gpointer            _data,
                    const gchar        *option,
                    gpointer            value,
                    gboolean            toggle,
                    DonnaColumnOptionSaveLocation save_location,
                    GError            **error)
{
    struct tv_col_data *data = _data;
    gboolean c;
    gpointer v;

    if (streq (option, "natural_order"))
    {
        c = (data->options & DONNA_SORT_NATURAL_ORDER) ? TRUE : FALSE;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            if (* (gboolean *) value)
                data->options |= DONNA_SORT_NATURAL_ORDER;
            else
                data->options &= (DonnaSortOptions) ~DONNA_SORT_NATURAL_ORDER;
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "dot_first"))
    {
        c = (data->options & DONNA_SORT_DOT_FIRST) ? TRUE : FALSE;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            if (* (gboolean *) value)
                data->options |= DONNA_SORT_DOT_FIRST;
            else
                data->options &= (DonnaSortOptions) ~DONNA_SORT_DOT_FIRST;
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "locale_based"))
    {
        c = data->is_locale_based;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            data->is_locale_based = * (gboolean *) value;
            if (data->is_locale_based)
            {
                g_free (data->collate_key);
                data->collate_key = g_strdup_printf ("%s/%s/%s/utf8-collate-key",
                        tv_name, col_name, arr_name);
            }
            else
            {
                g_free (data->collate_key);
                data->collate_key = NULL;
            }
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "special_first"))
    {
        c = data->sort_special_first;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->sort_special_first = * (gboolean *) value;
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "case_sensitive"))
    {
        c = (data->options & DONNA_SORT_CASE_INSENSITIVE) ? FALSE : TRUE;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            if (* (gboolean *) value)
                data->options &= (DonnaSortOptions) ~DONNA_SORT_CASE_INSENSITIVE;
            else
                data->options |= DONNA_SORT_CASE_INSENSITIVE;
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "dot_mixed"))
    {
        c = (data->options & DONNA_SORT_DOT_MIXED) ? TRUE : FALSE;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            if (* (gboolean *) value)
                data->options |= DONNA_SORT_DOT_MIXED;
            else
                data->options &= (DonnaSortOptions) ~DONNA_SORT_DOT_MIXED;
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "ignore_spunct"))
    {
        c = (data->options & DONNA_SORT_IGNORE_SPUNCT) ? TRUE : FALSE;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "sort", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            if (* (gboolean *) value)
                data->options |= DONNA_SORT_IGNORE_SPUNCT;
            else
                data->options &= (DonnaSortOptions) ~DONNA_SORT_IGNORE_SPUNCT;
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'name': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_name_get_context_alias (DonnaColumnType   *ct,
                           gpointer           _data,
                           const gchar       *alias,
                           const gchar       *extra,
                           DonnaContextReference reference,
                           DonnaNode         *node_ref,
                           get_sel_fn         get_sel,
                           gpointer           get_sel_data,
                           const gchar       *prefix,
                           GError           **error)
{
    struct tv_col_data *data = _data;
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'name': Unknown alias '%s'",
                alias);
        return NULL;
    }

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, TRUE, error);
    if (!save_location)
        return NULL;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'name': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    if (data->is_locale_based)
        return g_strconcat (
                prefix, "natural_order:@", save_location, ",",
                prefix, "dot_first:@", save_location, ",",
                prefix, "locale_based:@", save_location, "<",
                    prefix, "special_first:@", save_location, ">",
                NULL);
    else
        return g_strconcat (
                prefix, "natural_order:@", save_location, ",",
                prefix, "dot_first:@", save_location, ",",
                prefix, "locale_based:@", save_location, "<",
                    prefix, "case_sensitive:@", save_location, ",",
                    prefix, "dot_mixed:@", save_location, ",",
                    prefix, "ignore_spunct:@", save_location, ">",
                NULL);
}

static gboolean
ct_name_get_context_item_info (DonnaColumnType   *ct,
                               gpointer           _data,
                               const gchar       *item,
                               const gchar       *extra,
                               DonnaContextReference reference,
                               DonnaNode         *node_ref,
                               get_sel_fn         get_sel,
                               gpointer           get_sel_data,
                               DonnaContextInfo  *info,
                               GError           **error)
{
    struct tv_col_data *data = _data;
    const gchar *save_location;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "natural_order"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_NATURAL_ORDER) ? TRUE : FALSE;
        info->name = "Natural Order";
    }
    else if (streq (item, "dot_first"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_DOT_FIRST) ? TRUE : FALSE;
        info->name = "Show \"dot files\" first";
    }
    else if (streq (item, "locale_based"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = data->is_locale_based;
        info->name = "Use locale-based sort algorithm";
        info->desc = "Note that some options (e.g. case sensitive) are algorithm-dependent.";
    }
    else if (streq (item, "special_first"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = data->is_locale_based;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = data->sort_special_first;
        info->name = "Special Characters First";
    }
    else if (streq (item, "case_sensitive"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = !data->is_locale_based;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_CASE_INSENSITIVE) ? FALSE: TRUE;
        info->name = "Case Sensitive";
    }
    else if (streq (item, "dot_mixed"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = !data->is_locale_based;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_DOT_MIXED) ? TRUE : FALSE;
        info->name = "Sort \"dot files\" amongst others";
    }
    else if (streq (item, "ignore_spunct"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = !data->is_locale_based;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_IGNORE_SPUNCT) ? TRUE : FALSE;
        info->name = "Ignore leading spunctuation characters";
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'name': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, (info->is_active) ? "0" : "1", FALSE,
                NULL, NULL, NULL, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
