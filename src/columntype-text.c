/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-text.c
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
#include "columntype.h"
#include "columntype-text.h"
#include "renderer.h"
#include "node.h"
#include "app.h"
#include "conf.h"
#include "sort.h"
#include "util.h"
#include "macros.h"

/**
 * SECTION: columntype-text
 * @Short_description: To show the value of a text property.
 *
 * Column type to show the value of a text property.
 *
 * <refsect2 id="ct-text-options">
 * <title>Options</title>
 * <para>
 * The following options are available :
 *
 * - <systemitem>property</systemitem> (string) : Name of the property to show;
 *   Defaults to "name"
 * - <systemitem>align</systemitem> (integer:align) : Where to align the text in
 *   the column, can be "left" (default), "center" or "right"
 * - <systemitem>natural_order</systemitem> (boolean) : Whether to use natural
 *   order (1, 2, 11) or not.
 * - <systemitem>dot_first</systemitem> (boolean) : Whether to put "dot files"
 *   (i.e. nodes with a dot as first character in their names) first or not.
 *   Note that even with this option disabled, they might be sorted first - or
 *   before non-dotted names at least - by the sort algorithm.
 * - <systemitem>dot_mixed</systemitem> (boolean) : Alongside dot_first set to
 *   false, this will have dot files mixed in, i.e. ignoring the dot when
 *   sorting.
 * - <systemitem>case_sensitive</systemitem> (boolean) : Whether to be case
 *   sensitive or not.
 * - <systemitem>ignore_spunct</systemitem> (boolean) : Whether to ignore space
 *   and punctuation/symbol characters or not. (Much like
 *   <systemitem>dot_mixed</systemitem> ignores dots.)
 * - <systemitem>property_tooltip</systemitem> (string) : Name of the property
 *   to show in the tooltip. Use ":property" (the default) to use the same one
 *   as <systemitem>property</systemitem>
 *
 * </para></refsect2>
 *
 * <refsect2 id="ct-text-filtering">
 * <title>Filtering</title>
 * <para>
 * You can use #DonnaPattern<!-- -->s, which will be matched against the
 * property's value.  See donna_pattern_new() for more.
 * </para></refsect2>
 */

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct tv_col_data
{
    DonnaAlign        align;
    gchar            *property;
    gchar            *property_tooltip;
    DonnaSortOptions  options;
};

struct _DonnaColumnTypeTextPrivate
{
    DonnaApp                    *app;
};

/* internal from columntype.c */
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
_donna_context_set_item_from_extra (DonnaContextInfo     *info,
                                   DonnaConfig          *config,
                                   const gchar          *name,
                                   DonnaConfigExtraType  type,
                                   gboolean              is_column_option,
                                   const gchar          *option,
                                   const gchar          *item,
                                   gintptr               current,
                                   const gchar          *save_location,
                                   GError              **error);

static void             ct_text_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_text_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_text_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_text_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_text_get_renderers       (DonnaColumnType    *ct);
static void             ct_text_get_options         (DonnaColumnType    *ct,
                                                     DonnaColumnOptionInfo **options,
                                                     guint              *nb_options);
static DonnaColumnTypeNeed ct_text_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_text_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_text_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static gboolean         ct_text_can_edit            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GError            **error);
static gboolean         ct_text_edit                (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer   **renderers,
                                                     renderer_edit_fn    renderer_edit,
                                                     gpointer            re_data,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static gboolean         ct_text_set_value           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     GPtrArray          *nodes,
                                                     const gchar        *value,
                                                     DonnaNode          *node_ref,
                                                     DonnaTreeView      *treeview,
                                                     GError            **error);
static GPtrArray *      ct_text_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gboolean         ct_text_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gint             ct_text_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_text_refresh_filter_data (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     GError            **error);
static gboolean         ct_text_is_filter_match     (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     gpointer            filter_data,
                                                     DonnaNode          *node);
static void             ct_text_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_text_set_option       (DonnaColumnType    *ct,
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
static gchar *          ct_text_get_context_alias   (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_text_get_context_item_info (
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
ct_text_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_text_get_name;
    interface->get_renderers            = ct_text_get_renderers;
    interface->get_options              = ct_text_get_options;
    interface->refresh_data             = ct_text_refresh_data;
    interface->free_data                = ct_text_free_data;
    interface->get_props                = ct_text_get_props;
    interface->can_edit                 = ct_text_can_edit;
    interface->edit                     = ct_text_edit;
    interface->set_value                = ct_text_set_value;
    interface->render                   = ct_text_render;
    interface->set_tooltip              = ct_text_set_tooltip;
    interface->node_cmp                 = ct_text_node_cmp;
    interface->refresh_filter_data      = ct_text_refresh_filter_data;
    interface->is_filter_match          = ct_text_is_filter_match;
    interface->free_filter_data         = ct_text_free_filter_data;
    interface->set_option               = ct_text_set_option;
    interface->get_context_alias        = ct_text_get_context_alias;
    interface->get_context_item_info    = ct_text_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeText, donna_column_type_text,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_text_column_type_init)
        )

static void
donna_column_type_text_class_init (DonnaColumnTypeTextClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_text_set_property;
    o_class->get_property   = ct_text_get_property;
    o_class->finalize       = ct_text_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeTextPrivate));
}

static void
donna_column_type_text_init (DonnaColumnTypeText *ct)
{
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_TEXT,
            DonnaColumnTypeTextPrivate);
}

static void
ct_text_finalize (GObject *object)
{
    DonnaColumnTypeTextPrivate *priv;

    priv = DONNA_COLUMN_TYPE_TEXT (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_text_parent_class)->finalize (object);
}

static void
ct_text_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_TEXT (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_text_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_TEXT (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_text_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TEXT (ct), NULL);
    return "text";
}

static const gchar *
ct_text_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TEXT (ct), NULL);
    return "t";
}

static void
ct_text_get_options (DonnaColumnType    *ct,
                     DonnaColumnOptionInfo **options,
                     guint              *nb_options)
{
    static DonnaColumnOptionInfo o[] = {
        { "align",              G_TYPE_INT,         "align" },
        { "property",           G_TYPE_STRING,      NULL },
        { "property_tooltip",   G_TYPE_STRING,      NULL },
        { "natural_order",      G_TYPE_BOOLEAN,     NULL },
        { "dot_first",          G_TYPE_BOOLEAN,     NULL },
        { "dot_mixed",          G_TYPE_BOOLEAN,     NULL },
        { "case_sensitive",     G_TYPE_BOOLEAN,     NULL },
        { "ignore_spunct",      G_TYPE_BOOLEAN,     NULL }
    };

    *options = o;
    *nb_options = G_N_ELEMENTS (o);
}

#define check_option(opt_name_lower, opt_name_upper, value, def_val)          \
    if (donna_config_get_boolean_column (config, col_name,                    \
                arr_name, tv_name, is_tree, "sort",                           \
                opt_name_lower, def_val) == value)                            \
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
ct_text_refresh_data (DonnaColumnType    *ct,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      const gchar        *tv_name,
                      gboolean            is_tree,
                      gpointer           *_data)
{
    DonnaColumnTypeText *cttext = DONNA_COLUMN_TYPE_TEXT (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    gchar *s;
    guint align;

    config = donna_app_peek_config (cttext->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL, "property", "name");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else
        g_free (s);

    align = (guint) donna_config_get_int_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/text",
            "align", DONNA_ALIGN_LEFT);
    if (align != data->align)
    {
        data->align = align;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL, "property_tooltip", ":property");
    if (streq (s, ":property"))
    {
        g_free (data->property_tooltip);
        data->property_tooltip = NULL;
    }
    else if (!streq (data->property_tooltip, s))
    {
        g_free (data->property_tooltip);
        data->property_tooltip = s;
    }
    else
        g_free (s);

    check_option ("natural_order",  DONNA_SORT_NATURAL_ORDER,    TRUE,  TRUE);
    check_option ("dot_first",      DONNA_SORT_DOT_FIRST,        TRUE,  TRUE);
    check_option ("dot_mixed",      DONNA_SORT_DOT_MIXED,        TRUE,  FALSE);
    check_option ("case_sensitive", DONNA_SORT_CASE_INSENSITIVE, FALSE, FALSE);
    check_option ("ignore_spunct",  DONNA_SORT_IGNORE_SPUNCT,    TRUE,  FALSE);

    return need;
}

static void
ct_text_free_data (DonnaColumnType    *ct,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->property_tooltip);
    g_free (data);
}

static GPtrArray *
ct_text_get_props (DonnaColumnType  *ct,
                   gpointer          _data)
{
    struct tv_col_data *data = _data;
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TEXT (ct), NULL);

    props = g_ptr_array_new_full ((data->property_tooltip) ? 2 : 1, g_free);
    g_ptr_array_add (props, g_strdup (data->property));
    if (data->property_tooltip)
        g_ptr_array_add (props, g_strdup (data->property_tooltip));

    return props;
}

#define warn_not_string(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("ColumnType 'text': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_STRING));               \
    g_free (location);                                  \
} while (0)

struct editing_data
{
    DonnaColumnTypeText *cttext;
    DonnaTreeView *tree;
    DonnaNode *node;
    struct tv_col_data *data;
    gulong editing_started_sid;
    gulong editing_done_sid;
};

static gboolean
set_value (const gchar      *property,
           const gchar      *value,
           DonnaNode        *node,
           DonnaTreeView    *tree,
           GError          **error)
{
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    if (!donna_tree_view_set_node_property (tree, node, property, &v, error))
    {
        gchar *fl = donna_node_get_full_location (node);
        g_prefix_error (error, "ColumnType 'text': "
                "Unable to set property '%s' for '%s' to '%s'",
                property, fl, value);
        g_free (fl);
        g_value_unset (&v);
        return FALSE;
    }
    g_value_unset (&v);
    return TRUE;
}

static void
editing_done_cb (GtkCellEditable *editable, struct editing_data *ed)
{
    GError *err = NULL;
    gboolean canceled;

    g_signal_handler_disconnect (editable, ed->editing_done_sid);

    g_object_get (editable, "editing-canceled", &canceled, NULL);
    if (canceled)
    {
        g_free (ed);
        return;
    }

    if (G_UNLIKELY (!GTK_IS_ENTRY (editable)))
    {
        gchar *fl = donna_node_get_full_location (ed->node);
        donna_app_show_error (ed->cttext->priv->app, NULL,
                "ColumnType 'text': Unable to change property '%s' for '%s': "
                "Editable widget isn't a GtkEntry",
                ed->data->property, fl);
        g_free (fl);
        g_free (ed);
        return;
    }

    if (!set_value (ed->data->property, gtk_entry_get_text ((GtkEntry *) editable),
            ed->node, ed->tree, &err))
    {
        donna_app_show_error (ed->cttext->priv->app, err, NULL);
        g_clear_error (&err);
        g_free (ed);
        return;
    }

    g_free (ed);
}

static void
editing_started_cb (GtkCellRenderer     *renderer,
                    GtkCellEditable     *editable,
                    gchar               *path,
                    struct editing_data *ed)
{
    g_signal_handler_disconnect (renderer, ed->editing_started_sid);
    ed->editing_started_sid = 0;

    ed->editing_done_sid = g_signal_connect (editable, "editing-done",
            (GCallback) editing_done_cb, ed);
}

static gboolean
ct_text_can_edit (DonnaColumnType    *ct,
                  gpointer            data,
                  DonnaNode          *node,
                  GError            **error)
{
    return DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_can_edit (ct,
            ((struct tv_col_data *) data)->property, node, error);
}

static gboolean
ct_text_edit (DonnaColumnType    *ct,
              gpointer            _data,
              DonnaNode          *node,
              GtkCellRenderer   **renderers,
              renderer_edit_fn    renderer_edit,
              gpointer            re_data,
              DonnaTreeView      *treeview,
              GError            **error)
{
    struct tv_col_data *data = _data;
    struct editing_data *ed;

    if (!ct_text_can_edit (ct, _data, node, error))
        return FALSE;

    ed = g_new0 (struct editing_data, 1);
    ed->cttext  = (DonnaColumnTypeText *) ct;
    ed->tree    = treeview;
    ed->node    = node;
    ed->data    = data,
    ed->editing_started_sid = g_signal_connect (renderers[0], "editing-started",
            (GCallback) editing_started_cb, ed);

    g_object_set (renderers[0], "editable", TRUE, NULL);
    if (!renderer_edit (renderers[0], re_data))
    {
        g_signal_handler_disconnect (renderers[0], ed->editing_started_sid);
        g_free (ed);
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR, DONNA_COLUMN_TYPE_ERROR_OTHER,
                "ColumnType 'text': Failed to put renderer in edit mode");
        return FALSE;
    }
    return TRUE;
}

static gboolean
ct_text_set_value (DonnaColumnType    *ct,
                   gpointer            _data,
                   GPtrArray          *nodes,
                   const gchar        *value,
                   DonnaNode          *node_ref,
                   DonnaTreeView      *treeview,
                   GError            **error)
{
    struct tv_col_data *data = _data;

    if (nodes->len != 1)
    {
        g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                DONNA_COLUMN_TYPE_ERROR_NOT_SUPPORTED,
                "ColumnType 'text': Can only set value to one node at a time");
        return FALSE;
    }

    if (!ct_text_can_edit (ct, _data, nodes->pdata[0], error))
        return FALSE;

    return set_value (data->property, value, nodes->pdata[0], treeview, error);
}

static GPtrArray *
ct_text_render (DonnaColumnType    *ct,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gdouble xalign;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_TEXT (ct), NULL);

    donna_node_get (node, FALSE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }
    else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
    {
        GPtrArray *arr;

        arr = g_ptr_array_new_full (1, g_free);
        g_ptr_array_add (arr, g_strdup (data->property));
        g_object_set (renderer, "visible", FALSE, NULL);
        return arr;
    }
    /* DONNA_NODE_VALUE_SET */
    else if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
    {
        warn_not_string (node);
        g_value_unset (&value);
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    switch (data->align)
    {
        case DONNA_ALIGN_LEFT:
            xalign = 0.0;
            break;
        case DONNA_ALIGN_CENTER:
            xalign = 0.5;
            break;
        case DONNA_ALIGN_RIGHT:
            xalign = 1.0;
            break;
    }
    g_object_set (renderer,
            "visible",      TRUE,
            "text",         g_value_get_string (&value),
            "ellipsize",    PANGO_ELLIPSIZE_END,
            "ellipsize-set",TRUE,
            "xalign",       xalign,
            NULL);
    donna_renderer_set (renderer, "ellipsize-set", "xalign", NULL);
    g_value_unset (&value);
    return NULL;
}

static gboolean
ct_text_set_tooltip (DonnaColumnType    *ct,
                     gpointer            _data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    const gchar *s;

    donna_node_get (node, FALSE,
            (data->property_tooltip) ? data->property_tooltip : data->property,
            &has, &value, NULL);
    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
    {
        g_value_unset (&value);
        return FALSE;
    }

    /* don't show tooltip w/ an empty string */
    s = g_value_get_string (&value);
    skip_blank (s);
    if (*s != '\0')
        gtk_tooltip_set_text (tooltip, g_value_get_string (&value));
    else
        s = NULL;
    g_value_unset (&value);
    return !!s;
}

static gint
ct_text_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gchar *s1 = NULL;
    gchar *s2 = NULL;
    gint ret;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
            warn_not_string (node1);
        else
            s1 = g_value_dup_string (&value);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
            warn_not_string (node2);
        else
            s2 = g_value_dup_string (&value);
        g_value_unset (&value);
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    if (has1 != DONNA_NODE_VALUE_SET)
    {
        if (has2 == DONNA_NODE_VALUE_SET)
            ret = -1;
        else
            ret = 0;
        goto done;
    }
    else if (has2 != DONNA_NODE_VALUE_SET)
    {
        ret = 1;
        goto done;
    }

    ret = donna_strcmp (s1, s2, data->options);
done:
    g_free (s1);
    g_free (s2);
    return ret;
}

static gboolean
ct_text_refresh_filter_data (DonnaColumnType    *ct,
                             const gchar        *filter,
                             gpointer           *filter_data,
                             GError            **error)
{
    if (*filter_data)
        ct_text_free_filter_data (ct, *filter_data);

    *filter_data = donna_app_get_pattern (((DonnaColumnTypeText *) ct)->priv->app,
            filter, error);
    return *filter_data != NULL;
}

static gboolean
ct_text_is_filter_match (DonnaColumnType    *ct,
                         gpointer            _data,
                         gpointer            filter_data,
                         DonnaNode          *node)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gboolean ret;

    donna_node_get (node, TRUE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_STRING)
        {
            warn_not_string (node);
            g_value_unset (&value);
            return FALSE;
        }
    }
    else
        return FALSE;

    ret = donna_pattern_is_match ((DonnaPattern *) filter_data,
            g_value_get_string (&value));
    g_value_unset (&value);
    return ret;
}

static void
ct_text_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    donna_pattern_unref ((DonnaPattern *) filter_data);
}

static DonnaColumnTypeNeed
ct_text_set_option (DonnaColumnType    *ct,
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

    if (streq (option, "property"))
    {
        v = (value) ? value : &data->property;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, &save_location,
                    option, G_TYPE_STRING, &data->property, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->property);
            data->property = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT | DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "align"))
    {
        gint i;

        i = (gint) data->align;
        v = (value) ? value : &i;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/perms",
                    &save_location,
                    option, G_TYPE_INT, &i, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->align = (guint) * (gint *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "property_tooltip"))
    {
        v = (value) ? value :
            (data->property_tooltip) ? &data->property_tooltip : &data->property;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, &save_location,
                    option, G_TYPE_STRING, &data->property_tooltip, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->property_tooltip);
            if (streq (* (gchar **) value, data->property))
                data->property_tooltip = NULL;
            else
                data->property_tooltip = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_NOTHING;
    }
    else if (streq (option, "natural_order"))
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
            "ColumnType 'text': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_text_get_context_alias (DonnaColumnType   *ct,
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
    GString *str;
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'text': Unknown alias '%s'",
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
                "ColumnType 'text': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    str = g_string_new (NULL);
    donna_g_string_append_concat (str,
            prefix, "property:@", save_location, ",",
            NULL);
    if (!_donna_context_add_items_for_extra (str,
                donna_app_peek_config (((DonnaColumnTypeText *) ct)->priv->app),
                "align", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                prefix, "align", save_location, error))
    {
        g_prefix_error (error, "ColumnType '%s': Failed to process item '%s': ",
                "text", "align");
        g_string_free (str, TRUE);
        return FALSE;
    }
    donna_g_string_append_concat (str, ",-,",
            prefix, "property_tooltip:@", save_location, ",-,",
            prefix, "natural_order:@", save_location, ",",
            prefix, "dot_first:@", save_location, ",",
            prefix, "case_sensitive:@", save_location, ",",
            prefix, "dot_mixed:@", save_location, ",",
            prefix, "ignore_spunct:@", save_location,
            NULL);
    return g_string_free (str, FALSE);
}

static gboolean
ct_text_get_context_item_info (DonnaColumnType   *ct,
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
    const gchar *value = NULL;
    const gchar *ask_title = NULL;
    const gchar *ask_current = NULL;
    const gchar *save_location = NULL;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "property"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Node Property: ", data->property, NULL);
        info->free_name = TRUE;
        ask_title = "Enter the name of the property";
        ask_current = data->property;
    }
    else if (streqn (item, "align", 5))
    {
        info->is_visible = info->is_sensitive = TRUE;
        if (item[5] == '\0')
        {
            info->name = "Alignment";
            info->submenus = 1;
            return TRUE;
        }
        else if (item[5] == '.')
        {
            if (!_donna_context_set_item_from_extra (info,
                        donna_app_peek_config (((DonnaColumnTypeText *) ct)->priv->app),
                        "align", DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
                        TRUE, "align",
                        item + 6, data->align, save_location, error))
            {
                g_prefix_error (error,
                        "ColumnType '%s': Failed to get item '%s': ",
                        "text", item);
                return FALSE;
            }
            return TRUE;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                    "ColumnType '%s': No such item: '%s'",
                    "text", item);
            return FALSE;
        }
    }
    else if (streq (item, "property_tooltip"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Node Property (tooltip): ",
                (data->property_tooltip) ? data->property_tooltip : ":property", NULL);
        info->free_name = TRUE;
        info->desc = "Name of the property shown in tooltip. Use :property to use the same one.";
        ask_title = "Enter the name of the property to be shown on tooltip";
        ask_current = (data->property_tooltip) ? data->property_tooltip : ":property";
    }
    else if (streq (item, "natural_order"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_NATURAL_ORDER) ? TRUE : FALSE;
        info->name = "Natural Order";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "dot_first"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_DOT_FIRST) ? TRUE : FALSE;
        info->name = "Show \"dot files\" first";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "case_sensitive"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_CASE_INSENSITIVE) ? FALSE: TRUE;
        info->name = "Case Sensitive";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "dot_mixed"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_DOT_MIXED) ? TRUE : FALSE;
        info->name = "Sort \"dot files\" amongst others";
        value = (info->is_active) ? "0" : "1";
    }
    else if (streq (item, "ignore_spunct"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = (data->options & DONNA_SORT_IGNORE_SPUNCT) ? TRUE : FALSE;
        info->name = "Ignore leading spunctuation characters";
        value = (info->is_active) ? "0" : "1";
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'text': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, value, FALSE,
                ask_title, NULL, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
