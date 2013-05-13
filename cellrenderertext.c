
#include <gtk/gtk.h>
#include "cellrenderertext.h"

#define REGION_HIGHLIGHTED  "highlighted"

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
                                 const GdkRectangle     *cell_area,
                                 GtkCellRendererState    flags)
{
    gchar *highlight = ((DonnaCellRendererText *) cell)->priv->highlight;
    GtkStyleContext *context;

    if (highlight)
    {
        gint width;

        ((GtkCellRendererClass *) donna_cell_renderer_text_parent_class)
            ->get_preferred_width (cell, widget, NULL, &width);

        context = gtk_widget_get_style_context (widget);
        gtk_style_context_save (context);

        /* add highlight class & draw background */
        gtk_style_context_add_class (context, highlight);
        gtk_render_background (context, cr,
                cell_area->x, cell_area->y, width, cell_area->height);

        /* set region "highlight" for extra bit on the right (allows CSS to keep
         * extra bit highlighted even when focused/selected) */
        gtk_style_context_save (context);
        gtk_style_context_add_region (context, REGION_HIGHLIGHTED, 0);
        gtk_render_background (context, cr,
                cell_area->x + width, cell_area->y,
                get_highlighted_size (widget), cell_area->height);
        gtk_style_context_restore (context);
    }
    ((GtkCellRendererClass *) donna_cell_renderer_text_parent_class)->render (
            cell, cr, widget, background_area, cell_area, flags);
    if (highlight)
        /* remove highlight class */
        gtk_style_context_restore (context);
}

GtkCellRenderer *
donna_cell_renderer_text_new (void)
{
    return g_object_new (DONNA_TYPE_CELL_RENDERER_TEXT, NULL);
}