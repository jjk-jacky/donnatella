/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * columntype-size.c
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
#include "columntype-size.h"
#include "renderer.h"
#include "node.h"
#include "app.h"
#include "conf.h"
#include "util.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:columntype-size
 * @Short_description: To show a size.
 *
 * Column type to show a size.
 *
 * # Options # {#ct-size-options}
 *
 * The following options are available :

 * - `property` (string) : Name of the property to use.  Defaults to "size"
 * - `format` (string) : A format string on how to show the size. Defaults to
 *   "&percnt;R"
 * - `format_tooltip` (string) : A format string like `format` for the tooltip;
 *   Defaults to "&percnt;B"
 * - `digits` (integer) : Must be a value between 0 and 2 (both included), the
 *   number of digits to show after the decimal point on rounded sizes. Defaults
 *   to 1
 * - `long_unit` (boolean) : Wheter to use long units (e.g. "MiB") or not (e.g.
 *   "M") Defaults to false
 *
 * The following format specifiers are supported `format` and `format_tooltip`:
 *
 * - &percnt;r: Raw size, without any formatting (not even thousand separator)
 * - &percnt;b: Size in bytes, much like &percnt;r only with a thousand
 *   separator. (The actual character used depends on the LC_NUMERIC part of the
 *   locale.)
 * - &percnt;B: Like &percnt;b followed with the unit
 * - &percnt;k: Size in KiB (w/ thousand separator)
 * - &percnt;K: Like &percnt;k followed with the unit
 * - &percnt;m: Size in MiB (w/ thousand separator)
 * - &percnt;M: Like &percnt;m followed with the unit
 * - &percnt;R: Size rounded up in the best fitting unit (with the unit obviously)
 * - &percnt;&percnt;: The '&percnt;' character
 *
 *
 * # Filtering # {#ct-size-filtering}
 *
 * You can filter by using the following format:
 * [COMP] VALUE
 *
 * Where COMP can be one of the usuals: <, <=, =, >=, or > If none specified, it
 * defaults to '='
 *
 * VALUE must be a number. When COMP is '=' then you can also follow it with a
 * dash ('-') and another number, to specify a range, to match when the value
 * compared is within the specified range.
 *
 * The numbers in VALUE can be suffixed with a unit character: B, K, M, G, or T.
 * The number is then assumed to be a size in the given unit, and timed by 1024
 * as many times as needed.
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
    gchar   *format;
    gchar   *format_tooltip;
    guint8   digits;
    gboolean long_unit;
    gboolean is_size;
};

enum comp
{
    COMP_LESSER_EQUAL,
    COMP_LESSER,
    COMP_EQUAL,
    COMP_GREATER,
    COMP_GREATER_EQUAL,
    COMP_IN_RANGE
};

struct filter_data
{
    enum comp   comp;
    guint64     ref;
    guint64     ref2;
};

struct _DonnaColumnTypeSizePrivate
{
    DonnaApp                    *app;
};

static void             ct_size_set_property        (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void             ct_size_get_property        (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void             ct_size_finalize            (GObject            *object);

/* ColumnType */
static const gchar *    ct_size_get_name            (DonnaColumnType    *ct);
static const gchar *    ct_size_get_renderers       (DonnaColumnType    *ct);
static void             ct_size_get_options         (DonnaColumnType    *ct,
                                                     DonnaColumnOptionInfo **options,
                                                     guint              *nb_options);
static DonnaColumnTypeNeed ct_size_refresh_data     (DonnaColumnType    *ct,
                                                     const gchar        *col_name,
                                                     const gchar        *arr_name,
                                                     const gchar        *tv_name,
                                                     gboolean            is_tree,
                                                     gpointer           *data);
static void             ct_size_free_data           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_size_get_props           (DonnaColumnType    *ct,
                                                     gpointer            data);
static GPtrArray *      ct_size_render              (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkCellRenderer    *renderer);
static gint             ct_size_node_cmp            (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     DonnaNode          *node1,
                                                     DonnaNode          *node2);
static gboolean         ct_size_set_tooltip         (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     guint               index,
                                                     DonnaNode          *node,
                                                     GtkTooltip         *tooltip);
static gboolean         ct_size_refresh_filter_data (DonnaColumnType    *ct,
                                                     const gchar        *filter,
                                                     gpointer           *filter_data,
                                                     GError            **error);
static gboolean         ct_size_is_filter_match     (DonnaColumnType    *ct,
                                                     gpointer            data,
                                                     gpointer            filter_data,
                                                     DonnaNode          *node);
static void             ct_size_free_filter_data    (DonnaColumnType    *ct,
                                                     gpointer            filter_data);
static DonnaColumnTypeNeed ct_size_set_option       (DonnaColumnType    *ct,
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
static gchar *          ct_size_get_context_alias   (DonnaColumnType   *ct,
                                                     gpointer           data,
                                                     const gchar       *alias,
                                                     const gchar       *extra,
                                                     DonnaContextReference reference,
                                                     DonnaNode         *node_ref,
                                                     get_sel_fn         get_sel,
                                                     gpointer           get_sel_data,
                                                     const gchar       *prefix,
                                                     GError           **error);
static gboolean         ct_size_get_context_item_info (
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
ct_size_column_type_init (DonnaColumnTypeInterface *interface)
{
    interface->get_name                 = ct_size_get_name;
    interface->get_renderers            = ct_size_get_renderers;
    interface->get_options              = ct_size_get_options;
    interface->refresh_data             = ct_size_refresh_data;
    interface->free_data                = ct_size_free_data;
    interface->get_props                = ct_size_get_props;
    interface->render                   = ct_size_render;
    interface->set_tooltip              = ct_size_set_tooltip;
    interface->node_cmp                 = ct_size_node_cmp;
    interface->refresh_filter_data      = ct_size_refresh_filter_data;
    interface->is_filter_match          = ct_size_is_filter_match;
    interface->free_filter_data         = ct_size_free_filter_data;
    interface->set_option               = ct_size_set_option;
    interface->get_context_alias        = ct_size_get_context_alias;
    interface->get_context_item_info    = ct_size_get_context_item_info;
}

G_DEFINE_TYPE_WITH_CODE (DonnaColumnTypeSize, donna_column_type_size,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_COLUMN_TYPE, ct_size_column_type_init)
        )

static void
donna_column_type_size_class_init (DonnaColumnTypeSizeClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = ct_size_set_property;
    o_class->get_property   = ct_size_get_property;
    o_class->finalize       = ct_size_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaColumnTypeSizePrivate));
}

static void
donna_column_type_size_init (DonnaColumnTypeSize *ct)
{
    ct->priv = G_TYPE_INSTANCE_GET_PRIVATE (ct,
            DONNA_TYPE_COLUMN_TYPE_SIZE,
            DonnaColumnTypeSizePrivate);
}

static void
ct_size_finalize (GObject *object)
{
    DonnaColumnTypeSizePrivate *priv;

    priv = DONNA_COLUMN_TYPE_SIZE (object)->priv;
    DONNA_DEBUG (MEMORY, NULL,
            g_debug ("ColumnType 'size' finalizing"));

    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_column_type_size_parent_class)->finalize (object);
}

static void
ct_size_set_property (GObject            *object,
                      guint               prop_id,
                      const GValue       *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        DONNA_COLUMN_TYPE_SIZE (object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
ct_size_get_property (GObject            *object,
                      guint               prop_id,
                      GValue             *value,
                      GParamSpec         *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, DONNA_COLUMN_TYPE_SIZE (object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
ct_size_get_name (DonnaColumnType *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_SIZE (ct), NULL);
    return "size";
}

static const gchar *
ct_size_get_renderers (DonnaColumnType   *ct)
{
    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_SIZE (ct), NULL);
    return "t";
}

static void
ct_size_get_options (DonnaColumnType    *ct,
                     DonnaColumnOptionInfo **options,
                     guint              *nb_options)
{
    static DonnaColumnOptionInfo o[] = {
        { "property",           G_TYPE_STRING,      NULL },
        { "format",             G_TYPE_STRING,      NULL },
        { "format_tooltip",     G_TYPE_STRING,      NULL },
        { "digits",             G_TYPE_INT,         NULL },
        { "long_unit",          G_TYPE_BOOLEAN,     NULL }
    };

    *options = o;
    *nb_options = G_N_ELEMENTS (o);
}

static DonnaColumnTypeNeed
ct_size_refresh_data (DonnaColumnType    *ct,
                      const gchar        *col_name,
                      const gchar        *arr_name,
                      const gchar        *tv_name,
                      gboolean            is_tree,
                      gpointer           *_data)
{
    DonnaColumnTypeSize *ctsize = DONNA_COLUMN_TYPE_SIZE (ct);
    DonnaConfig *config;
    struct tv_col_data *data;
    DonnaColumnTypeNeed need = DONNA_COLUMN_TYPE_NEED_NOTHING;
    gchar *s;
    gint i;

    config = donna_app_peek_config (ctsize->priv->app);

    if (!*_data)
        *_data = g_new0 (struct tv_col_data, 1);
    data = *_data;

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/size", "property", "size");
    if (!streq (data->property, s))
    {
        g_free (data->property);
        data->property = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW | DONNA_COLUMN_TYPE_NEED_RESORT;

        data->is_size = streq (s, "size");
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "size", "format", "%b");
    if (!streq (data->format, s))
    {
        g_free (data->format);
        data->format = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    s = donna_config_get_string_column (config, col_name,
            arr_name, tv_name, is_tree, "column_types/size", "format_tooltip", "%R");
    if (!streq(data->format_tooltip, s))
    {
        g_free (data->format_tooltip);
        data->format_tooltip = s;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else
        g_free (s);

    i = donna_config_get_int_column (config, col_name,
            arr_name, tv_name, is_tree, "size", "digits", 2);
    /* we enforce [0,2] because it's what makes sense */
    i = CLAMP (i, 0, 2);
    if (data->digits != i)
    {
        data->digits = (guint8) i;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    i = donna_config_get_boolean_column (config, col_name,
            arr_name, tv_name, is_tree, "size", "long_unit", FALSE);
    if (data->long_unit != i)
    {
        data->long_unit = i;
        need = DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    return need;
}

static void
ct_size_free_data (DonnaColumnType    *ct,
                   gpointer            _data)
{
    struct tv_col_data *data = _data;

    g_free (data->property);
    g_free (data->format);
    g_free (data->format_tooltip);
    g_free (data);
}

static GPtrArray *
ct_size_get_props (DonnaColumnType  *ct,
                   gpointer          data)
{
    GPtrArray *props;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_SIZE (ct), NULL);

    props = g_ptr_array_new_full (1, g_free);
    g_ptr_array_add (props, g_strdup (((struct tv_col_data *) data)->property));

    return props;
}

static gchar *
format_size (guint64             size,
             struct tv_col_data *data,
             const gchar        *fmt,
             gchar              *str,
             gsize               max)
{
    gsize len;

    len = donna_print_size (str, max, fmt, size, data->digits, data->long_unit);
    if (len >= max)
    {
        str = g_new (gchar, ++len);
        donna_print_size (str, len, fmt, size, data->digits, data->long_unit);
    }
    return str;
}

#define warn_not_uint64(node)    do {                   \
    gchar *location = donna_node_get_location (node);   \
    g_warning ("ColumnType 'size': property '%s' for node '%s:%s' isn't of expected type (%s instead of %s)",  \
            data->property,                             \
            donna_node_get_domain (node), location,     \
            G_VALUE_TYPE_NAME (&value),                 \
            g_type_name (G_TYPE_UINT64));               \
    g_free (location);                                  \
} while (0)

static GPtrArray *
ct_size_render (DonnaColumnType    *ct,
                gpointer            _data,
                guint               index,
                DonnaNode          *node,
                GtkCellRenderer    *renderer)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;
    guint64 size;
    gchar buf[20], *b = buf;

    g_return_val_if_fail (DONNA_IS_COLUMN_TYPE_SIZE (ct), NULL);

    if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
    {
        g_object_set (renderer, "visible", FALSE, NULL);
        return NULL;
    }

    if (data->is_size)
        has = donna_node_get_size (node, FALSE, &size);
    else
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
    else if (!data->is_size)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            g_object_set (renderer, "visible", FALSE, NULL);
            return NULL;
        }
        size = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    b = format_size (size, data, data->format, b, 20);
    g_object_set (renderer,
            "visible",      TRUE,
            "text",         b,
            "xalign",       1.0,
            "ellipsize",    PANGO_ELLIPSIZE_END,
            "ellipsize-set",TRUE,
            NULL);
    donna_renderer_set (renderer, "xalign", "ellipsize-set", NULL);
    if (b != buf)
        g_free (b);
    return NULL;
}

static gboolean
ct_size_set_tooltip (DonnaColumnType    *ct,
                     gpointer            _data,
                     guint               index,
                     DonnaNode          *node,
                     GtkTooltip         *tooltip)
{
    struct tv_col_data *data = _data;
    guint64 size;
    gchar buf[20], *b = buf;
    DonnaNodeHasValue has;
    GValue value = G_VALUE_INIT;

    if (data->is_size)
        has = donna_node_get_size (node, FALSE, &size);
    else
        donna_node_get (node, FALSE, data->property, &has, &value, NULL);

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;
    else if (!data->is_size)
    {
        if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
        {
            warn_not_uint64 (node);
            g_value_unset (&value);
            return FALSE;
        }
        size = g_value_get_uint64 (&value);
        g_value_unset (&value);
    }

    b = format_size (size, data, data->format_tooltip, b, 20);
    gtk_tooltip_set_text (tooltip, b);
    if (b != buf)
        g_free (b);
    return TRUE;
}

static gint
ct_size_node_cmp (DonnaColumnType    *ct,
                  gpointer            _data,
                  DonnaNode          *node1,
                  DonnaNode          *node2)
{
    struct tv_col_data *data = _data;
    DonnaNodeHasValue has1;
    DonnaNodeHasValue has2;
    guint64 size1;
    guint64 size2;

    if (donna_node_get_node_type (node1) == DONNA_NODE_CONTAINER)
    {
        if (donna_node_get_node_type (node2) == DONNA_NODE_CONTAINER)
            return 0;
        else
            return -1;
    }
    else if (donna_node_get_node_type (node2) == DONNA_NODE_CONTAINER)
        return 1;

    if (data->is_size)
    {
        has1 = donna_node_get_size (node1, TRUE, &size1);
        has2 = donna_node_get_size (node2, TRUE, &size2);
    }
    else
    {
        GValue value = G_VALUE_INIT;

        donna_node_get (node1, TRUE, data->property, &has1, &value, NULL);
        if (has1 == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node1);
                has1 = DONNA_NODE_VALUE_ERROR;
            }
            else
                size1 = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
        donna_node_get (node2, TRUE, data->property, &has2, &value, NULL);
        if (has2 == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node2);
                has2 = DONNA_NODE_VALUE_ERROR;
            }
            else
                size2 = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
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

    return (size1 > size2) ? 1 : (size1 < size2) ? -1 : 0;
}

static gboolean
ct_size_refresh_filter_data (DonnaColumnType    *ct,
                             const gchar        *filter,
                             gpointer           *filter_data,
                             GError            **error)
{
    struct filter_data *fd;
    const gchar unit[] = { 'B', 'K', 'M', 'G', 'T' };
    guint nb_units = sizeof (unit) / sizeof (unit[0]);
    gchar *s;
    guint i;

    if (*filter_data)
        fd = *filter_data;
    else
        fd = *filter_data = g_new0 (struct filter_data, 1);

    fd->comp = COMP_EQUAL;

    while (isblank (*filter))
        ++filter;
    if (*filter == '<')
    {
        ++filter;
        if (*filter == '=')
        {
            ++filter;
            fd->comp = COMP_LESSER_EQUAL;
        }
        else
            fd->comp = COMP_LESSER;
    }
    else if (*filter == '>')
    {
        ++filter;
        if (*filter == '=')
        {
            ++filter;
            fd->comp = COMP_GREATER_EQUAL;
        }
        else
            fd->comp = COMP_GREATER;
    }
    else if (*filter == '=')
        ++filter;

    fd->ref = g_ascii_strtoull (filter, &s, 10);
    for (i = 0; i < nb_units; ++i)
    {
        if (*s == unit[i])
        {
            ++s;
            for ( ; i > 0; --i)
                fd->ref *= 1024;
            break;
        }
    }

    while (isblank (*s))
        ++s;
    if (*s == '\0')
        return TRUE;

    if (fd->comp == COMP_EQUAL && *s == '-')
    {
        guint64 r;

        fd->comp = COMP_IN_RANGE;
        r = g_ascii_strtoull (s + 1, &s, 10);
        for (i = 0; i < nb_units; ++i)
        {
            if (*s == unit[i])
            {
                ++s;
                for ( ; i > 0; --i)
                    r *= 1024;
                break;
            }
        }

        if (r > fd->ref)
            fd->ref2 = r;
        else
        {
            fd->ref2 = fd->ref;
            fd->ref = r;
        }
    }

    return TRUE;
}

static gboolean
ct_size_is_filter_match (DonnaColumnType    *ct,
                         gpointer            _data,
                         gpointer            filter_data,
                         DonnaNode          *node)
{
    struct tv_col_data *data = _data;
    struct filter_data *fd = filter_data;
    DonnaNodeHasValue has;
    guint64 size;

    if (data->is_size)
        has = donna_node_get_size (node, TRUE, &size);
    else
    {
        GValue value = G_VALUE_INIT;

        donna_node_get (node, TRUE, data->property, &has, &value, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            if (G_VALUE_TYPE (&value) != G_TYPE_UINT64)
            {
                warn_not_uint64 (node);
                has = DONNA_NODE_VALUE_ERROR;
            }
            else
                size = g_value_get_uint64 (&value);
            g_value_unset (&value);
        }
    }

    if (has != DONNA_NODE_VALUE_SET)
        return FALSE;

    switch (fd->comp)
    {
        case COMP_LESSER_EQUAL:
            return size <= fd->ref;

        case COMP_LESSER:
            return size < fd->ref;

        case COMP_EQUAL:
            return size == fd->ref;

        case COMP_GREATER:
            return size > fd->ref;

        case COMP_GREATER_EQUAL:
            return size >= fd->ref;

        case COMP_IN_RANGE:
            return size >= fd->ref && size <= fd->ref2;
    }

    g_return_val_if_reached (0);
}

static void
ct_size_free_filter_data (DonnaColumnType    *ct,
                          gpointer            filter_data)
{
    g_free (filter_data);
}

static DonnaColumnTypeNeed
ct_size_set_option (DonnaColumnType    *ct,
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
    gint c;
    gpointer v;

    if (streq (option, "format"))
    {
        v = (value) ? value : &data->format;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "size", &save_location,
                    option, G_TYPE_STRING, &data->format, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->format);
            data->format = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "format_tooltip"))
    {
        v = (value) ? value : &data->format_tooltip;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, NULL, &save_location,
                    option, G_TYPE_STRING, &data->format_tooltip, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->format_tooltip);
            data->format_tooltip = g_strdup (* (gchar **) value);
        }
        return DONNA_COLUMN_TYPE_NEED_NOTHING;
    }
    else if (streq (option, "long_unit"))
    {
        c = data->long_unit;
        v = (value) ? value : &c;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "size", &save_location,
                    option, G_TYPE_BOOLEAN, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->long_unit = * (gboolean *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "digits"))
    {
        c = data->digits;
        if (value)
        {
            if (* (gint *) value < 0 || * (gint *) value > 2)
            {
                g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
                        DONNA_COLUMN_TYPE_ERROR_OTHER,
                        "ColumnType 'size': Invalid value for option 'long_unit': "
                        "Must be '0', '1' or '2'");
                return DONNA_COLUMN_TYPE_NEED_NOTHING;
            }
            v = value;
        }
        else
            v = &c;

        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "size", &save_location,
                    option, G_TYPE_INT, &c, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
            data->digits = (guint8) * (gint *) value;
        return DONNA_COLUMN_TYPE_NEED_REDRAW;
    }
    else if (streq (option, "property"))
    {
        v = (value) ? value : &data->property;
        if (!DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->helper_set_option (ct,
                    col_name, arr_name, tv_name, is_tree, "column_types/size",
                    &save_location,
                    option, G_TYPE_STRING, &data->property, v, error))
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (save_location != DONNA_COLUMN_OPTION_SAVE_IN_MEMORY)
            return DONNA_COLUMN_TYPE_NEED_NOTHING;

        if (value)
        {
            g_free (data->property);
            data->property = g_strdup (* (gchar **) value);
            data->is_size = streq (data->property, "size");
        }
        return DONNA_COLUMN_TYPE_NEED_RESORT | DONNA_COLUMN_TYPE_NEED_REDRAW;
    }

    g_set_error (error, DONNA_COLUMN_TYPE_ERROR,
            DONNA_COLUMN_TYPE_ERROR_OTHER,
            "ColumnType 'size': Unknown option '%s'",
            option);
    return DONNA_COLUMN_TYPE_NEED_NOTHING;
}

static gchar *
ct_size_get_context_alias (DonnaColumnType   *ct,
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
    const gchar *save_location;

    if (!streq (alias, "options"))
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ALIAS,
                "ColumnType 'size': Unknown alias '%s'",
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
                "ColumnType 'size': Invalid extra '%s' for alias '%s'",
                extra, alias);
        return NULL;
    }

    return g_strconcat (
            prefix, "format:@", save_location, "<",
                prefix, "format:@", save_location, ":%R,",
                prefix, "format:@", save_location, ":%M,",
                prefix, "format:@", save_location, ":%m,",
                prefix, "format:@", save_location, ":%K,",
                prefix, "format:@", save_location, ":%k,",
                prefix, "format:@", save_location, ":%B,",
                prefix, "format:@", save_location, ":%b,",
                prefix, "format:@", save_location, ":%r,-,",
                prefix, "format:@", save_location, ":=>,",
            prefix, "format_tooltip:@", save_location, "<",
                prefix, "format_tooltip:@", save_location, ":%R,",
                prefix, "format_tooltip:@", save_location, ":%M,",
                prefix, "format_tooltip:@", save_location, ":%m,",
                prefix, "format_tooltip:@", save_location, ":%K,",
                prefix, "format_tooltip:@", save_location, ":%k,",
                prefix, "format_tooltip:@", save_location, ":%B,",
                prefix, "format_tooltip:@", save_location, ":%b,",
                prefix, "format_tooltip:@", save_location, ":%r,-,",
                prefix, "format_tooltip:@", save_location, ":=>,",
            prefix, "digits:@", save_location, "<",
                prefix, "digits:@", save_location, ":0,",
                prefix, "digits:@", save_location, ":1,",
                prefix, "digits:@", save_location, ":2>,",
            prefix, "long_unit:@", save_location, ",",
            prefix, "property:@", save_location, "<",
                prefix, "property:@", save_location, ":size,",
                prefix, "property:@", save_location, ":custom>",
            NULL);
}

static gboolean
ct_size_get_context_item_info (DonnaColumnType   *ct,
                               gpointer            _data,
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
    gboolean quote_value = FALSE;

    save_location = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_save_location (ct, &extra, FALSE, error);
    if (!save_location)
        return FALSE;

    if (streq (item, "property"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;

        if (!extra)
        {
            info->name = g_strconcat ("Node Property: ", data->property, NULL);
            info->free_name = TRUE;
            value = NULL;
            ask_title = "Enter the name of the property";
            ask_current = data->property;
        }
        else if (streq (extra, "size"))
        {
            info->name = "Size";
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->is_size;
            value = "size";
        }
        else if (streq (extra, "custom"))
        {
            info->name = g_strconcat ("Custom: ", data->property, NULL);
            info->free_name = TRUE;
            value = NULL;
            ask_title = "Enter the name of the property";
            ask_current = data->property;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'size': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else if (streq (item, "format"))
    {
        gchar buf[20], *b = buf;
        guint64 size = 123456789;

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            b = format_size (size, data, data->format, b, 20);
            info->name = g_strconcat ("Column: ", b, NULL);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", data->format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the column";
            ask_current = data->format;
            if (b != buf)
                g_free (b);
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ", data->format, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the column";
            ask_current = data->format;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->format);
            b = format_size (size, data, extra, b, 20);
            if (b == buf)
                info->name = g_strdup (b);
            else
                info->name = b;
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "format_tooltip"))
    {
        gchar buf[20], *b = buf;
        guint64 size = 123456789;

        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            b = format_size (size, data, data->format_tooltip, b, 20);
            info->name = g_strconcat ("Tooltip: ", b, NULL);
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", data->format_tooltip, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the tooltip";
            ask_current = data->format_tooltip;
            if (b != buf)
                g_free (b);
        }
        else if (*extra == '=')
        {
            if (extra[1] == '\0')
                info->name = "Custom...";
            else
            {
                info->name = g_strdup (extra + 1);
                info->free_name = TRUE;
            }
            info->desc = g_strconcat ("Current format: ", data->format_tooltip, NULL);
            info->free_desc = TRUE;
            value = NULL;
            ask_title = "Enter the format for the tooltip";
            ask_current = data->format_tooltip;
        }
        else
        {
            if (*extra == ':')
                ++extra;
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = streq (extra, data->format_tooltip);
            b = format_size (size, data, extra, b, 20);
            if (b == buf)
                info->name = g_strdup (b);
            else
                info->name = b;
            info->free_name = TRUE;
            info->desc = g_strconcat ("Format: ", extra, NULL);
            info->free_desc = TRUE;
            value = extra;
            quote_value = TRUE;
        }
    }
    else if (streq (item, "long_unit"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        info->icon_special = DONNA_CONTEXT_ICON_IS_CHECK;
        info->is_active = data->long_unit;
        info->name = "Use long units (MiB instead of M)";
        value = (data->long_unit) ? "0" : "1";
    }
    else if (streq (item, "digits"))
    {
        info->is_visible = TRUE;
        info->is_sensitive = TRUE;
        if (!extra)
        {
            info->name = "Number of digits";
            info->desc = "Number of digits to use when rounding up";
            info->submenus = 1;
            return TRUE;
        }
        else if (extra[1] == '\0' && *extra >= '0' && *extra <= '2')
        {
            info->icon_special = DONNA_CONTEXT_ICON_IS_RADIO;
            info->is_active = data->digits == *extra - '0';
            info->name = g_strdup (extra);
            info->free_name = TRUE;
            value = extra;
        }
        else
        {
            g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                    DONNA_CONTEXT_MENU_ERROR_OTHER,
                    "ColumnType 'size': Invalid extra '%s' for item '%s'",
                    extra, item);
            return FALSE;
        }
    }
    else
    {
        g_set_error (error, DONNA_CONTEXT_MENU_ERROR,
                DONNA_CONTEXT_MENU_ERROR_UNKNOWN_ITEM,
                "ColumnType 'size': Unknown item '%s'",
                item);
        return FALSE;
    }

    info->trigger = DONNA_COLUMN_TYPE_GET_INTERFACE (ct)->
        helper_get_set_option_trigger (item, value, quote_value,
                ask_title, NULL, ask_current, save_location);
    info->free_trigger = TRUE;

    return TRUE;
}
