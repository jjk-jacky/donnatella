/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-progress.c
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
#include "columntype-progress.h"
#include "app.h"
#include "node.h"
#include "macros.h"

/**
 * SECTION:columntype-progress
 * @Short_description: To show a progress bar.
 *
 * Column type to show a progress bar.
 *
 * <refsect2 id="ct-progress-options">
 * <title>Options</title>
 * <para>
 * The following options are available :
 *
 * - <systemitem>property</systemitem> (string) : Name of the property holding
 *   the progress value. This must be either an integer value from 0 to 100, or
 *   a double value from 0.0 to 1.0.
 *   It can also be -1 to indicate the progress bar should be in pulsating mode.
 *   Defaults to "progress"
 * - <systemitem>label</systemitem> (string) : A format string of what to show
 *   over the progress bar. Will be used if no
 *   <systemitem>property_lbl</systemitem> was set. Defaults to "&percnt;P"
 * - <systemitem>property_lbl</systemitem> (string) : Name of a property holding
 *   the text to show over the progress bar; It can use the same format
 *   specifiers as "label". No default.
 * - <systemitem>property_pulse</systemitem> (string) : Name of a property used
 *   for updating a pulsating progress bar. Must be an integer, incremented to
 *   pulsate the progress bar.
 *
 * The following format specifiers are supported in
 * <systemitem>label</systemitem>, as well as the value of
 * <systemitem>property_lbl</systemitem>:
 *
 * - &percnt;p: Current progress value
 * - &percnt;P: Same as &percnt;P but suffixed with a percent sign
 * - &percnt;&percnt;: The '&percnt;' character
 *
 * </para></refsect2>
 *
 * <refsect2 id="ct-progress-filtering">
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

struct tv_col_data
{
    gchar   *property;
    gchar   *property_lbl;
    gchar   *property_pulse;
    gchar   *label;
};

struct _DonnaColumnTypeProgressPrivate
{
    DonnaApp    *app;
};

static void             ct_progress_set_property    (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_progress_get_property    (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_progress_finalize        (GObject            *object);

/* ColumnType */
static const gchar *    ct_progress_get_name        (DonnaColumnType    *ct);
static const gchar *    ct_progress_get_renderers   (DonnaColumnType    *ct);
static DonnaColumnTypeNeed ct_progress_refresh_data (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_progress_free_data       (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_progress_get_props       (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_progress_render          (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_progress_node_cmp        (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static DonnaColumnTypeNeed ct_progress_set_option   (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer            data,
                                                     const gchar        *option,
                                                     const gchar        *value,
                                                     DonnaColumnOptionSaveLocation save_location,
                                                     GError            **error);
static gchar *          ct_progress_get_context_alias (
                                                     DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_progress_get_context_item_info (
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
ct_progress_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_progress_get_name;
    interface->get_renderers            = ct_progress_get_renderers;
    interface->refresh_data             = ct_progress_refresh_data;
    interface->free_data                = ct_progress_free_data;
    interface->get_props                = ct_progress_get_props;
    interface->render                   = ct_progress_render;
    interface->node_cmp                 = ct_progress_node_cmp;
    interface->set_option               = ct_progress_set_option;
    interface->get_context_alias        = ct_progress_get_context_alias;
    interface->get_context_item_info    = ct_progress_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeProgress, donna_column_type_progress,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_progress_column_type_init)
        )

static void
donna_column_type_progress_class_init (DonnaColumnTypeProgressClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_progress_set_property;
    o_class->get_property   = ct_progress_get_property;
    o_class->finalize       = ct_progress_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeProgressPrivate));
}

static void
donna_column_type_progress_init (DonnaColumnTypeProgress *ct)
{
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_PROGRESS,
            DonnaColumnTypeProgressPrivate);
}

static void
ct_progress_finalize (GObject *object)
{
    DonnaColumnTypeProgressPrivate *priv;

    priv = DONNA_COLUMN_TYPE_PROGRESS (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_progress_parent_class)->finalize (object);
}

static void
ct_progress_set_property (GObject            *object,
                          guint               prop_id,
                          const GValue       *value,
                          GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_PROGRESS (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_progress_get_property (GObject            *object,
                          guint               prop_id,
                          GValue             *value,
                          GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_PROGRESS (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_progress_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PROGRESS (ct), NULL);
    return "progress";
}

static const gchar *
ct_progress_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PROGRESS (ct), NULL);
    return "P";
}

static DonnaColumnTypeNeed
ct_progress_refresh_data (DonnaColumnType    *ct,
                          const gchar        *col_name,
                          const gchar        *arr_name,
                          const gchar        *tv_name,
                          gboolean            is_tree,
                          gpointer           *_data)
{
    DonnaColumnTypeProgress *ctpg = (DonnaColumnTypeProgress *) ct;
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    gchar *s;

    config = donna_app_peek_config (ctpg->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL,
            "property", "progress", NULL);
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL,
            "label", "%P", NULL);
    if (!streq (data->label, s))
    {
        g_free (data->label);
        data->label = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL,
            "property_lbl", "", NULL);
    if (*s == '\0')
    {
        g_free (s);
        s = NULL;
    }
    if (!streq (data->property_lbl, s))
    {
        g_free (data->property_lbl);
        data->property_lbl = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, NULL,
            "property_pulse", "pulse", NULL);
    if (!streq (data->property_pulse, s))
    {
        g_free (data->property_pulse);
        data->property_pulse = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    return need;
}

static void
ct_progress_free_data (DonnaColumnType    *ct,
                       gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->property_lbl);
    g_free (data->property_pulse);
    g_free (data->label);
    g_free (data);
}

static GPtrArray *
ct_progress_get_props (DonnaColumnType  *ct,
                       gpointer          _data)
{
    struct tv_col_data *data = _data;
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PROGRESS (ct), NULL);

    props = g_ptr_array_new_full (3, g_free);
    g_ptr_array_add (props, g_strdup (data->property));
    if (data->property_lbl)
        g_ptr_array_add (props, g_strdup (data->property_lbl));
    if (data->property_pulse)
        g_ptr_array_add (props, g_strdup (data->property_pulse));

    return props;
}

#define warn_not_type(node)    do {                     \
    gchar *fl = donna_node_get_full_location (node);    \
    g_warning ("ColumnType 'progress': property '%s' for node '%s' isn't of expected type (%s instead of %s or %s)",  \
            data->property,                             \
            fl,                                         \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_INT),                   \
            g_type_name (G_TYPE_DOUBLE));               \
    g_free (fl);                                        \
} while (0)

static GPtrArray *
ct_progress_render (DonnaColumnType    *ct,
                    gpointer            _data,
                    guint               index,
                    DonnaNode          *node,
                    GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    gint progress = -1;
    gint pulse;
    gchar *lbl = NULL;
    gchar *s;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_PROGRESS (ct), NULL);

    if (data->property_lbl)
    {
        donna_node_get (node, FALSE, data->property_lbl, &has, &value, NULL);
        if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
        {
            if (data->property)
                goto next;
            else
            {
                g_object_set (renderer, "visible", FALSE, NULL);
                return NULL;
            }
        }
        else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
        {
            GPtrArray *arr;

            arr = g_ptr_array_new_full (2, g_free);
            g_ptr_array_add (arr, g_strdup (data->property_lbl));
            if (data->property)
                g_ptr_array_add (arr, g_strdup (data->property));

            g_object_set (renderer, "visible", FALSE, NULL);
            return arr;
        }
        else if (G_VALUE_TYPE (&value) == G_TYPE_STRING)
        {
            lbl = g_value_dup_string (&value);
            g_value_unset (&value);
        }
        else
        {
            gchar *fl = donna_node_get_full_location (node);
            g_warning ("ColumnType 'progress': property '%s' for node '%s' isn't of expected type (%s instead of %s)",
                    data->property_lbl,
                    fl,
                    G_VALUE_TYPE_NAME (&value),
                    g_type_name (G_TYPE_STRING));
            g_free (fl);
            g_value_unset (&value);
            if (!data->property)
            {
                g_object_set (renderer, "visible", FALSE, NULL);
                return NULL;
            }
        }
    }

next:
    donna_node_get (node, FALSE, data->property, &has, &value, NULL);
    if (has == DONNA_NODE_VALUE_NONE || has == DONNA_NODE_VALUE_ERROR)
    {
        if (!lbl)
        {
            g_object_set (renderer, "visible", FALSE, NULL);
            return NULL;
        }
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
    else if (G_VALUE_TYPE (&value) == G_TYPE_INT)
        progress = g_value_get_int (&value);
    else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE)
        progress = (gint) (100.0 * g_value_get_double (&value));
    else
    {
        warn_not_type (node);
        g_value_unset (&value);
        if (!lbl)
        {
            g_object_set (renderer, "visible", FALSE, NULL);
            return NULL;
        }
    }
    g_value_unset (&value);

    if (progress < 0)
        pulse = 0;
    else
    {
        pulse = -1;
        progress = CLAMP (progress, 0, 100);
    }

    if (pulse == 0 && data->property_pulse)
    {
        donna_node_get (node, FALSE, data->property_pulse, &has, &value, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) == G_TYPE_INT)
            {
                pulse = g_value_get_int (&value);
                pulse = CLAMP (pulse, 0, G_MAXINT);
            }
            else
            {
                gchar *fl = donna_node_get_full_location (node);
                g_warning ("ColumnType 'progress': property '%s' for node '%s' isn't of expected type (%s instead of %s)",
                        data->property_pulse,
                        fl,
                        G_VALUE_TYPE_NAME (&value),
                        g_type_name (G_TYPE_INT));
                g_free (fl);
            }
            g_value_unset (&value);
        }
        else if (has == DONNA_NODE_VALUE_NEED_REFRESH)
        {
            GPtrArray *arr;

            arr = g_ptr_array_new_full (1, g_free);
            g_ptr_array_add (arr, g_strdup (data->property_pulse));
            g_object_set (renderer, "visible", FALSE, NULL);
            g_free (lbl);
            return arr;
        }
    }

    if (!lbl)
    {
        s = strchr (data->label, '%');
        if (s)
        {
            GString *str;
            gchar *ss;

            *s = '\0';
            str = g_string_new (data->label);
            *s = '%';
            for (;;)
            {
                if (s[1] == 'p')
                {
                    if (pulse == -1)
                        g_string_append_printf (str, "%d", progress);
                }
                else if (s[1] == 'P')
                {
                    if (pulse == -1)
                        g_string_append_printf (str, "%d%%", progress);
                }
                else if (s[1] == '%')
                    g_string_append_c (str, '%');
                else
                    --s;
                ss = s + 2;
                s = strchr (ss, '%');
                if (!s)
                    break;
            }
            g_string_append (str, ss);
            s = g_string_free (str, FALSE);
        }
    }
    else
        s = NULL;

    g_object_set (renderer,
            "visible",  TRUE,
            "pulse",    pulse,
            "value",    (pulse == -1) ? progress : 0,
            "text",     (lbl) ? lbl : ((s) ? s : data->label),
            NULL);
    g_free (s);
    g_free (lbl);
    return NULL;
}

static gint
ct_progress_node_cmp (DonnaColumnType    *ct,
                      gpointer            _data,
                      DonnaNode          *node1,
                      DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    GValue value = G_VALUE_INIT;
    gint p1 = 0;
    gint p2 = 0;

    donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
    if (has1 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) == G_TYPE_INT)
            p1 = g_value_get_int (&value);
        else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE)
            p1 = (gint) (100.0 * g_value_get_double (&value));
        else
            warn_not_type (node1);
        g_value_unset (&value);
    }

    donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
    if (has2 == DONNA_NODE_VALUE_SET)
    {
        if (G_VALUE_TYPE (&value) == G_TYPE_INT)
            p2 = g_value_get_int (&value);
        else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE)
            p2 = (gint) (100.0 * g_value_get_double (&value));
        else
            warn_not_type (node2);
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

    return (p1 > p2) ? 1 : (p1 < p2) ? -1 : 0;
}

static DonnaColumnTypeNeed
ct_progress_set_option (DonnaColumnType    *ct,
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
        return DONNA_COLUMN_TYPE_NEED_RESORT | DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "property_lbl"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, save_location,
                    option, G_TYPE_STRING, &data->property_lbl, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->property_lbl);
        data->property_lbl = g_strdup (value);
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "property_pulse"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, save_location,
                    option, G_TYPE_STRING, &data->property_pulse, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->property_pulse);
        data->property_pulse = g_strdup (value);
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "label"))
    {
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, save_location,
                    option, G_TYPE_STRING, &data->label, &value, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        g_free (data->label);
        data->label = g_strdup (value);
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'progress': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_progress_get_context_alias (DonnaColumnType   *ct,
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
                "ColumnType 'progress': Unknown alias '%s'",
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
                "ColumnType 'progress': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "label:@", save_location, ",",
            prefix, "property:@", save_location, ",",
            prefix, "property_lbl:@", save_location, ",",
            prefix, "property_pulse:@", save_location,
            NULL);
}

static gboolean
ct_progress_get_context_item_info (DonnaColumnType   *ct,
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
    const gchar *ask_title;
    const gchar *ask_details = NULL;
    const gchar *ask_current;
    const gchar *save_location;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (extra)
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_OTHER,
                "ColumnType 'progress': Invalid extra '%s' for item '%s'",
                extra, item);
        return FALSE;
    }

    if (streq (item, "property"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Property Progress: ", data->property, NULL);
        info->desc = "The property containing the current progress value";
        ask_title = "Enter the property name for the current progress value";
        ask_current = data->property;
    }
    else if (streq (item, "property_lbl"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Property Label: ", data->property_lbl, NULL);
        info->desc = "The property containing the text to show as label";
        ask_title = "Enter the property name for the label";
        ask_current = data->property_lbl;
    }
    else if (streq (item, "property_pulse"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Property Pulse: ", data->property_pulse, NULL);
        info->desc = "The property containing the current pulse value (if no progress value)";
        ask_title = "Enter the property name for the current pulse value";
        ask_current = data->property_pulse;
    }
    else if (streq (item, "label"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->name = g_strconcat ("Label: ", data->label, NULL);
        info->desc = "The label (if no property label; can use %p/%P for progress value (w/ percent sign))";
        ask_title = "Enter the label";
        ask_details = "Use %p/%P for current progress value (without/with percent sign)";
        ask_current = data->label;
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'progress': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, NULL, FALSE,
                ask_title, ask_details, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
