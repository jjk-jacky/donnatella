/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * cellrenderertext.c
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

#include <gtk/gtk.h>
#include "cellrenderertext.h"

#define REGION_HIGHLIGHT_OVERFLOW     "highlight-overflow"

enum
{
    PROP_0,

    PROP_HIGHLIGHT,

    NB_PROPS
};

struct _DonnaCellRendererTextPrivate
{
    gchar   *highlight;
};

static GParamSpec * donna_cell_renderer_text_props[NB_PROPS] = { NULL, };

static void donna_cell_renderer_text_get_property   (GObject            *object,
                                                     guint               prop_id,
                                                     GValue             *value,
                                                     GParamSpec         *pspec);
static void donna_cell_renderer_text_set_property   (GObject            *object,
                                                     guint               prop_id,
                                                     const GValue       *value,
                                                     GParamSpec         *pspec);
static void donna_cell_renderer_text_finalize       (GObject            *object);
static void donna_cell_renderer_text_get_preferred_width (
                                                     GtkCellRenderer    *cell,
                                                     GtkWidget          *widget,
                                                     gint               *minimum,
                                                     gint               *natural);
static void donna_cell_renderer_text_render         (GtkCellRenderer    *cell,
                                                     cairo_t            *cr,
                                                     GtkWidget          *widget,
                                                     const GdkRectangle *background_area,
                                                     const GdkRectangle *cell_area,
                                                     GtkCellRendererState flags);


G_DEFINE_TYPE (DonnaCellRendererText, donna_cell_renderer_text,
        GTK_TYPE_CELL_RENDERER_TEXT)

static void
donna_cell_renderer_text_class_init (DonnaCellRendererTextClass *klass)
{
    GObjectClass *o_class;
    GtkCellRendererClass *cell_class;

    o_class = (GObjectClass *) klass;
    o_class->get_property   = donna_cell_renderer_text_get_property;
    o_class->set_property   = donna_cell_renderer_text_set_property;
    o_class->finalize       = donna_cell_renderer_text_finalize;

    donna_cell_renderer_text_props[PROP_HIGHLIGHT] =
        g_param_spec_string ("highlight", "highlight",
                "Class name for the highlight effect",
                NULL,   /* default */
                G_PARAM_READWRITE);

    g_object_class_install_properties (o_class, NB_PROPS,
            donna_cell_renderer_text_props);

    cell_class = (GtkCellRendererClass *) klass;
    cell_class->render               = donna_cell_renderer_text_render;
    cell_class->get_preferred_width  = donna_cell_renderer_text_get_preferred_width;

    g_type_class_add_private (klass, sizeof (DonnaCellRendererTextPrivate));
}

static void
donna_cell_renderer_text_init (DonnaCellRendererText *renderer)
{
    renderer->priv = G_TYPE_INSTANCE_GET_PRIVATE (renderer,
            DONNA_TYPE_CELL_RENDERER_TEXT, DonnaCellRendererTextPrivate);
}

static void
donna_cell_renderer_text_finalize (GObject *object)
{
    DonnaCellRendererTextPrivate *priv;

    priv = ((DonnaCellRendererText *) object)->priv;
    g_free (priv->highlight);

    ((GObjectClass *) donna_cell_renderer_text_parent_class)->finalize (object);
}

static void
donna_cell_renderer_text_get_property (GObject        *object,
                                       guint           prop_id,
                                       GValue         *value,
                                       GParamSpec     *pspec)
{
    if (prop_id == PROP_HIGHLIGHT)
        g_value_set_string (value, ((DonnaCellRendererText *) object)->priv->highlight);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
donna_cell_renderer_text_set_property (GObject        *object,
                                       guint           prop_id,
                                       const GValue   *value,
                                       GParamSpec     *pspec)
{
    DonnaCellRendererTextPrivate *priv = ((DonnaCellRendererText *) object)->priv;

    if (prop_id == PROP_HIGHLIGHT)
    {
        g_free (priv->highlight);
        priv->highlight = g_value_dup_string (value);
    }
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static inline gint
get_highlighted_size (GtkWidget *widget)
{
    gint size;
    gtk_widget_style_get (widget, "highlighted-size", &size, NULL);
    return size;
}

static void
donna_cell_renderer_text_get_preferred_width (GtkCellRenderer    *cell,
                                              GtkWidget          *widget,
                                              gint               *minimum,
                                              gint               *natural)
{
    ((GtkCellRendererClass *) donna_cell_renderer_text_parent_class)
        ->get_preferred_width (cell, widget, minimum, natural);
    if (((DonnaCellRendererText *) cell)->priv->highlight)
    {
        gint size = get_highlighted_size (widget);

        if (minimum)
            *minimum += size;
        if (natural)
            *natural += size;
    }
}

static void
donna_cell_renderer_text_render (GtkCellRenderer        *cell,
                                 cairo_t                *cr,
                                 GtkWidget              *widget,
                                 const GdkRectangle     *background_area,
                                 const GdkRectangle     *_cell_area,
                                 GtkCellRendererState    flags)
{
    gchar *highlight = ((DonnaCellRendererText *) cell)->priv->highlight;
    GtkStyleContext *context;
    GdkRectangle cell_area = *_cell_area;

    if (highlight)
    {
        gint pref_width;
        gint highlighted_size = get_highlighted_size (widget);

        ((GtkCellRendererClass *) donna_cell_renderer_text_parent_class)
            ->get_preferred_width (cell, widget, NULL, &pref_width);

        context = gtk_widget_get_style_context (widget);
        gtk_style_context_save (context);
        /* we add the class (for the color) */
        gtk_style_context_add_class (context, highlight);

        /* draw background */
        gtk_render_background (context, cr,
                cell_area.x,
                cell_area.y,
                (pref_width + highlighted_size <= cell_area.width)
                ? pref_width + highlighted_size : cell_area.width,
                cell_area.height);

        /* we now add a region for the overflow, so it can be made to still be
         * visible even when selected */
        gtk_style_context_save (context);
        gtk_style_context_add_region (context, REGION_HIGHLIGHT_OVERFLOW, 0);
        gtk_render_background (context, cr,
                (pref_width + highlighted_size <= cell_area.width)
                ? cell_area.x + pref_width
                : cell_area.x + cell_area.width - highlighted_size,
                cell_area.y,
                highlighted_size,
                cell_area.height);
        gtk_style_context_restore (context);

        /* make sure it doesn't overwrite the overflow */
        if (pref_width + highlighted_size > cell_area.width)
            cell_area.width -= highlighted_size;
    }
    ((GtkCellRendererClass *) donna_cell_renderer_text_parent_class)->render (
            cell, cr, widget, background_area, &cell_area, flags);
    if (highlight)
        /* remove class */
        gtk_style_context_restore (context);
}

GtkCellRenderer *
donna_cell_renderer_text_new (void)
{
    return g_object_new (DONNA_TYPE_CELL_RENDERER_TEXT, NULL);
}
