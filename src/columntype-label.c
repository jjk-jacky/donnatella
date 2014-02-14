/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-label.c
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
#include "columntype-label.h"
#include "renderer.h"
#include "app.h"
#include "node.h"
#include "util.h"
#include "macros.h"

/**
 * SECTION:columntype-label
 * @Short_description: To show a label from an integer value
 *
 * Column type to show a label from an integer value; That is, a property on the
 * node will have an integer value, which will be shown as its corresponding
 * label.
 *
 * <refsect2 id="ct-label-options">
 * <title>Options</title>
 * <para>
 * The following options are available :
 *
 * - <systemitem>property</systemitem> (string) : Name of the property; Defaults
 *   to "id"
 * - <systemitem>labels</systemitem> (string) : A coma-separated list of
 *   value=label to use. Defaults to "0=false,1=true"
 *
 * If the property contains a value not featured in the list, it will show as
 * "&lt;unknown id:X&gt;" where X is the integer value of the property.
 * </para></refsect2>
 *
 * <refsect2 id="ct-label-filtering">
 * <title>Filtering</title>
 * <para>
 * There are no filtering possible on column of this type.
 * </para></refsect2>
 */

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct label
{
    gint    id;
    gchar  *label;
    gint    len;
};

struct tv_col_data
{
    gchar           *property;
    gchar           *labels;
    struct label    *defs;
    guint            nb;
};

struct _DonnaColumnTypeLabelPrivate
{
    DonnaApp    *app;
};

static void             ct_label_set_property       (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_label_get_property       (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_label_finalize           (GObject            *object);

/* ColumnType */
static const gchar *    ct_label_get_name           (DonnaColumnType    *ct);
static const gchar *    ct_label_get_renderers      (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_label_refresh_data    (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_label_free_data          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_label_get_props          (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_label_render             (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_label_node_cmp           (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static DonnaColumnTypeNeed ct_label_set_option      (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     const gchar        *value,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_label_get_context_alias  (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_label_get_context_item_info (
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
ct_label_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name              = ct_label_get_name;
    interface->get_renderers         = ct_label_get_renderers;
    interface->refresh_data          = ct_label_refresh_data;
    interface->free_data             = ct_label_free_data;
    interface->get_props             = ct_label_get_props;
    interface->render                = ct_label_render;
    interface->node_cmp              = ct_label_node_cmp;
    interface->set_option            = ct_label_set_option;
    interface->get_context_alias     = ct_label_get_context_alias;
    interface->get_context_item_info = ct_label_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeLabel, donna_column_type_label,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_label_column_type_init)
        )

static void
donna_column_type_label_class_init (DonnaColumnTypeLabelClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_label_set_property;
    o_class->get_property   = ct_label_get_property;
    o_class->finalize       = ct_label_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeLabelPrivate));
}

static void
donna_column_type_label_init (DonnaColumnTypeLabel *ct)
{
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_LABEL,
            DonnaColumnTypeLabelPrivate);
}

static void
ct_label_finalize (GObject *object)
{
    DonnaColumnTypeLabelPrivate *priv;

    priv = DONNA_COLUMN_TYPE_LABEL (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_label_parent_class)->finalize (object);
}

static void
ct_label_set_property (GObject            *object,
                       guint               prop_id,
                       const GValue       *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_LABEL (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_label_get_property (GObject            *object,
                       guint               prop_id,
                       GValue             *value,
                       GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_LABEL (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_label_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_LABEL (ct), NULL);
    return "label";
}

static const gchar *
ct_label_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_LABEL (ct), NULL);
    return "t";
}

static void
set_data_labels (struct tv_col_data *data)
{
    guint alloc, i;
    gchar *l;
    gchar *s;

    g_free (data->defs);
    data->defs = NULL;
    alloc = i = 0;
    l = data->labels;

    for ( ; ; ++i)
    {
        if (i >= alloc)
        {
            alloc += 4;
            data->defs = g_renew (struct label, data->defs, (gsize) alloc);
        }
        s = strchr (l, '=');
        if (!s)
        {
            g_warning ("ColumnType 'label': Invalid labels definition: %s",
                    data->labels);
            g_free (data->labels);
            data->labels = NULL;
            g_free (data->defs);
            data->defs = NULL;
            break;
        }
        data->defs[i].id = (gint) g_ascii_strtoll (l, NULL, 10);
        *s = '=';
        l = s + 1;
        s = strchr (l, ',');
        if (s)
            *s = '\0';
        data->defs[i].label = l;
        data->defs[i].len   = (gint) strlen (l);
        if (s)
        {
            *s = ',';
            l = s + 1;
        }
        else
            break;
    }
    data->nb = i + 1;
}

static DonnaColumnTypeNeed
ct_label_refresh_data (DonnaColumnType    *ct,
                       const gchar        *col_name,
                       const gchar        *arr_name,
                       const gchar        *tv_name,
                       gboolean            is_tree,
                       gpointer           *_data)
{
    DonnaColumnTypeLabel *ctlbl = (DonnaColumnTypeLabel *) ct;
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    gchar *s;

    config = donna_app_peek_config (ctlbl->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL, "property", "id");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL, "labels", "0=false,1=true");
    if (!streq (data->labels, s))
    {
        g_free (data->labels);
        data->labels = s;
        set_data_labels (data);
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    return need;
}

static void
ct_label_free_data (DonnaColumnType    *ct,
                    gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->labels);
    g_free (data->defs);
    g_free (data);
}

static GPtrArray *
ct_label_get_props (DonnaColumnType  *ct,
                    gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_LABEL (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

#define warn_not_int(node)    do {                      \
    gchar *fl = donna_node_get_full_location (node);    \
    g_warning ("ColumnType 'label': property '%s' for node '%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            fl,                                         \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_INT));                  \
    g_free (fl);                                        \
} while (0)

static GPtrArray *
ct_label_render (DonnaColumnType    *ct,
                 gpointer            _data,
                 guint               index,
                 DonnaNode          *node,
                 GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    guint i;
    gint id;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_LABEL (ct), NULL);

    if (!data->labels)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

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
    else if (G_VALUE_TYPE (&value) != G_TYPE_INT)
    {
        warn_not_int (node);
        g_value_unset (&value);
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    id = g_value_get_int (&value);
    for (i = 0; i < data->nb; ++i)
        if (data->defs[i].id == id)
        {
            s = g_strdup_printf ("%.*s", data->defs[i].len, data->defs[i].label);
            break;
        }
    if (i >= data->nb)
        s = g_strdup_printf ("<unknown id:%d>", id);

    g_object_set (renderer,
            "visible",      TRUE,
            "text",         s,
            "ellipsize",    PANGO_ELLIPSIZE_END,
            "ellipsize-set",TRUE,
            NULL);
    donna_renderer_set (renderer, "ellipsize-set", NULL);
    g_value_unset (&value);
    g_free (s);
    return NULL;
}

static gint
ct_label_node_cmp (DonnaColumnType    *ct,
                   gpointer            _data,
                   DonnaNode          *node1,
                   DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gint id1 = 0;
    gint id2 = 0;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_INT)
            warn_not_int (node1);
        else
            id1 = g_value_get_int (&value);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_INT)
            warn_not_int (node2);
        else
            id2 = g_value_get_int (&value);
        g_value_unset (&value);
    }

    /* since we're blocking, has can only be SET, ERROR or NONE */

    if (has1 != DONNA_NODE_VALUE_SET)
    {
        if (has2 == DONNA_NODE_VALUE_SET)
            return -1;
        else
            return 0;
    }
    else if (has2 != DONNA_NODE_VALUE_SET)
        return 1;

    return (id1 > id2) ? 1 : (id1 < id2) ? -1 : 0;
}

static DonnaColumnTypeNeed
ct_label_set_option (DonnaColumnType    *ct,
                     const gchar        *col_name,
                     const gchar        *arr_name,
                     const gchar        *tv_name,
                     gboolean            is_tree,
                     gpointer            _data,
                     const gchar        *option,
                     const gchar        *value,
                     DonnaColumnOptionSaveLocation save_location,
                     GError            **error)
{
    struct tv_col_data *data = _data;

    if (streq (option, "property"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, save_location,
                    option, G_TYPE_STRING, &data->property, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->property);
        data->property = g_strdup (value);
        return DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else if (streq (option, "labels"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, save_location,
                    option, G_TYPE_STRING, &data->labels, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->labels);
        data->labels = g_strdup (value);
        set_data_labels (data);
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'label': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_label_get_context_alias (DonnaColumnType   *ct,
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
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'label': Unknown alias '%s'",
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
                "ColumnType 'label': Invalid extra '%s' for alias '%s' (none supported)",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "property:@", save_location, ",",
            prefix, "labels:@", save_location,
            NULL);
}

static gboolean
ct_label_get_context_item_info (DonnaColumnType   *ct,
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
    const gchar *title;
    const gchar *current;
    const gchar *save_location;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "property"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strdup_printf ("Node Property: %s", data->property);
        info->free_name = TRUE;
        title = "Enter the name of the property";
        current = data->property;
    }
    else if (streq (item, "labels"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (strlen (data->labels) > 23)
            info->name = g_strdup_printf ("Labels: %.*s...", 20, data->labels);
        else
            info->name = g_strdup_printf ("Labels: %s", data->labels);
        info->free_name = TRUE;
        info->desc = g_strdup_printf ("Labels: %s", data->labels);
        info->free_desc = TRUE;
        title = "Enter the labels definition";
        current = data->labels;
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'label': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, NULL, FALSE,
                title, NULL, current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
