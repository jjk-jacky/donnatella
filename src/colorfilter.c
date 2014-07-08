/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * colorfilter.c
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

#include "colorfilter.h"
#include "filter.h"
#include "renderer.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,
    PROP_FILTER,
    PROP_COLUMN,
    PROP_KEEP_GOING,
    PROP_VIA_TREEVIEW,

    NB_PROPS
};

struct prop
{
    gchar   *name_set;
    gchar   *name;
    GValue   value;
};

struct _DonnaColorFilterPrivate
{
    gchar       *filter;
    DonnaFilter *filter_obj;
    DonnaApp    *app;
    gchar       *column;
    gboolean     keep_going;
    gboolean     via_treeview;
    GSList      *props;
};

static GParamSpec *donna_color_filter_props[NB_PROPS] = { NULL, };

static void     donna_color_filter_set_property (GObject            *object,
                                                 guint               prop_id,
                                                 const GValue       *value,
                                                 GParamSpec         *pspec);
static void     donna_color_filter_get_property (GObject            *object,
                                                 guint               prop_id,
                                                 GValue             *value,
                                                 GParamSpec         *pspec);
static void     donna_color_filter_finalize     (GObject            *object);

G_DEFINE_TYPE (DonnaColorFilter, donna_color_filter, G_TYPE_OBJECT)

static void
donna_color_filter_class_init (DonnaColorFilterClass *klass)
{
    GObjectClass *o_class;

    o_class = G_OBJECT_CLASS (klass);
    o_class->set_property   = donna_color_filter_set_property;
    o_class->get_property   = donna_color_filter_get_property;
    o_class->finalize       = donna_color_filter_finalize;

    donna_color_filter_props[PROP_APP] =
        g_param_spec_object ("app", "app", "The DonnaApp object",
                DONNA_TYPE_APP,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    donna_color_filter_props[PROP_FILTER] =
        g_param_spec_string ("filter", "filter", "Filter string",
                NULL,   /* default */
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    donna_color_filter_props[PROP_COLUMN] =
        g_param_spec_string ("column", "column",
                "Name of column where to apply the colo rfilter",
                NULL,   /* default */
                G_PARAM_READWRITE);

    donna_color_filter_props[PROP_KEEP_GOING] =
        g_param_spec_boolean ("keep-going", "keep-going",
                "Whether to keep processing color filters after a match",
                FALSE,  /* default */
                G_PARAM_READWRITE);

    donna_color_filter_props[PROP_VIA_TREEVIEW] =
        g_param_spec_boolean ("via-treeview", "via-treeview",
                "Whether the filter should be done via treeview, or app",
                TRUE, /* default */
                G_PARAM_READWRITE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_color_filter_props);

    g_type_class_add_private (klass, sizeof (DonnaColorFilterPrivate));
}

static void
donna_color_filter_init (DonnaColorFilter *cf)
{
    DonnaColorFilterPrivate *priv;

    priv = cf->priv = G_TYPE_INSTANCE_GET_PRIVATE (cf,
            DONNA_TYPE_COLOR_FILTER, DonnaColorFilterPrivate);
    priv->via_treeview = TRUE;
}

static void
donna_color_filter_set_property (GObject            *object,
                                 guint               prop_id,
                                 const GValue       *value,
                                 GParamSpec         *pspec)
{
    DonnaColorFilterPrivate *priv = ((DonnaColorFilter *) object)->priv;
    DonnaApp *app;

    switch (prop_id)
    {
        case PROP_APP:
            app = g_value_get_object (value);
            g_return_if_fail (DONNA_IS_APP (app));
            priv->app = g_object_ref (app);
            break;
        case PROP_FILTER:
            priv->filter = g_value_dup_string (value);
            break;
        case PROP_COLUMN:
            g_free (priv->column);
            priv->column = g_value_dup_string (value);
            break;
        case PROP_KEEP_GOING:
            priv->keep_going = g_value_get_boolean (value);
            break;
        case PROP_VIA_TREEVIEW:
            priv->via_treeview = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
donna_color_filter_get_property (GObject            *object,
                                 guint               prop_id,
                                 GValue             *value,
                                 GParamSpec         *pspec)
{
    DonnaColorFilterPrivate *priv = ((DonnaColorFilter *) object)->priv;

    switch (prop_id)
    {
        case PROP_APP:
            g_value_set_object (value, priv->app);
            break;
        case PROP_FILTER:
            g_value_set_string (value, priv->filter);
            break;
        case PROP_COLUMN:
            g_value_set_string (value, priv->column);
            break;
        case PROP_KEEP_GOING:
            g_value_set_boolean (value, priv->keep_going);
            break;
        case PROP_VIA_TREEVIEW:
            g_value_set_boolean (value, priv->via_treeview);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
free_prop (struct prop *prop)
{
    g_free (prop->name_set);
    g_free (prop->name);
    g_value_unset (&prop->value);
    g_slice_free (struct prop, prop);
}

static void
donna_color_filter_finalize (GObject *object)
{
    DonnaColorFilterPrivate *priv;

    priv = DONNA_COLOR_FILTER (object)->priv;
    g_object_unref (priv->app);
    g_free (priv->filter);
    if (priv->filter_obj)
        g_object_unref (priv->filter_obj);
    g_free (priv->column);
    g_slist_free_full (priv->props, (GDestroyNotify) free_prop);

    G_OBJECT_CLASS (donna_color_filter_parent_class)->finalize (object);
}

gboolean
donna_color_filter_add_prop (DonnaColorFilter   *cf,
                             const gchar        *name_set,
                             const gchar        *name,
                             const GValue       *value)
{
    DonnaColorFilterPrivate *priv;
    struct prop *prop;
    gsize len;

    g_return_val_if_fail (DONNA_IS_COLOR_FILTER (cf), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (G_IS_VALUE (value), FALSE);

    priv = cf->priv;

    len = strlen (name);
    if (len > 5 && streq (name + len - 5, "-rgba"))
    {
        GdkRGBA rgba;
        const gchar *color;

        /* special handling of *-rgba props; as we need to convert them from a
         * string to a GdkRGBA* */

        color = g_value_get_string (value);
        if (!gdk_rgba_parse (&rgba, color))
            return FALSE;

        prop = g_slice_new0 (struct prop);
        prop->name = g_strdup (name);
        g_value_init (&prop->value, GDK_TYPE_RGBA);
        g_value_set_boxed (&prop->value, &rgba);
    }
    else
    {
        prop = g_slice_new0 (struct prop);
        prop->name = g_strdup (name);
        g_value_init (&prop->value, G_VALUE_TYPE (value));
        g_value_copy (value, &prop->value);
    }
    prop->name_set = g_strdup (name_set);

    priv->props = g_slist_prepend (priv->props, prop);
    return TRUE;
}

gboolean
donna_color_filter_apply_if_match (DonnaColorFilter *cf,
                                   GObject          *renderer,
                                   const gchar      *col_name,
                                   DonnaNode        *node,
                                   DonnaTreeView    *tree,
                                   gboolean         *keep_going,
                                   GError          **error)
{
    DonnaColorFilterPrivate *priv;
    GSList *l;

    g_return_val_if_fail (DONNA_IS_COLOR_FILTER (cf), FALSE);
    g_return_val_if_fail (GTK_IS_CELL_RENDERER (renderer), FALSE);
    g_return_val_if_fail (DONNA_IS_NODE (node), FALSE);

    priv = cf->priv;

    if (priv->via_treeview)
        g_return_val_if_fail (DONNA_IS_TREE_VIEW (tree), FALSE);

    if (priv->column && !streq (priv->column, col_name))
        return FALSE;

    if (!priv->filter_obj)
    {
        priv->filter_obj = donna_app_get_filter (priv->app, priv->filter, error);
        if (G_UNLIKELY (!priv->filter_obj))
        {
            g_prefix_error (error, "Color filter failed: cannot get filter for '%s': ",
                    priv->filter);
            return FALSE;
        }

        if (!donna_filter_is_compiled (priv->filter_obj)
                && !donna_filter_compile (priv->filter_obj, error))
            return FALSE;
    }

    if (!donna_filter_is_match (priv->filter_obj, node, tree))
        return FALSE;

    for (l = priv->props; l; l = l->next)
    {
        struct prop *prop = l->data;

        g_object_set_property (renderer, prop->name, &prop->value);
        g_object_set (renderer, prop->name_set, TRUE, NULL);
        donna_renderer_set ((GtkCellRenderer *) renderer, prop->name_set, NULL);
    }
    if (keep_going)
        *keep_going = priv->keep_going;
    return TRUE;
}
