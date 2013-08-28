
#include <gtk/gtk.h>
#include "imagemenuitem.h"

enum
{
    PROP_0,

    PROP_IS_COMBINED,
    PROP_IS_LABEL_BOLD,

    NB_PROPS
};

enum
{
    SIGNAL_LOAD_SUBMENU,
    NB_SIGNALS
};

struct _DonnaImageMenuItemPrivate
{
    gboolean is_combined;
    gboolean is_label_bold;
    gint item_width;
    guint sid_button_release;
    guint sid_parent_button_release;
    guint sid_timeout;
};

static GParamSpec * donna_image_menu_item_props[NB_PROPS] = { NULL, };
static guint        donna_image_menu_item_signals[NB_SIGNALS] = { 0, };

static gboolean menu_item_release_cb            (GtkWidget             *widget,
                                                 GdkEventButton        *event,
                                                 DonnaImageMenuItem    *item);

static void donna_image_menu_item_get_property  (GObject            *object,
                                                 guint               prop_id,
                                                 GValue             *value,
                                                 GParamSpec         *pspec);
static void donna_image_menu_item_set_property  (GObject            *object,
                                                 guint               prop_id,
                                                 const GValue       *value,
                                                 GParamSpec         *pspec);
static void donna_image_menu_item_finalize      (GObject            *object);
static void donna_image_menu_item_parent_set    (GtkWidget          *widget,
                                                 GtkWidget          *old_parent);
static void donna_image_menu_item_select        (GtkMenuItem        *menuitem);
static void donna_image_menu_item_deselect      (GtkMenuItem        *menuitem);
static gboolean donna_image_menu_item_draw      (GtkWidget          *widget,
                                                 cairo_t            *cr);


G_DEFINE_TYPE (DonnaImageMenuItem, donna_image_menu_item,
        GTK_TYPE_IMAGE_MENU_ITEM)

static void
donna_image_menu_item_class_init (DonnaImageMenuItemClass *klass)
{
    GObjectClass *o_class;
    GtkWidgetClass *w_class;
    GtkMenuItemClass *mi_class;

    o_class = (GObjectClass *) klass;
    o_class->get_property   = donna_image_menu_item_get_property;
    o_class->set_property   = donna_image_menu_item_set_property;
    o_class->finalize       = donna_image_menu_item_finalize;

    w_class = (GtkWidgetClass *) klass;
    w_class->parent_set     = donna_image_menu_item_parent_set;
    w_class->draw           = donna_image_menu_item_draw;

    mi_class = (GtkMenuItemClass *) klass;
    mi_class->select        = donna_image_menu_item_select;
    mi_class->deselect      = donna_image_menu_item_deselect;

    donna_image_menu_item_props[PROP_IS_COMBINED] =
        g_param_spec_boolean ("is-combined", "is-combined",
                "Whether or not this item is a combined action and submenu",
                FALSE,   /* default */
                G_PARAM_READWRITE);

    donna_image_menu_item_props[PROP_IS_LABEL_BOLD] =
        g_param_spec_boolean ("is-label-bold", "is-label-bold",
                "Whether or not the label if shown in bold",
                FALSE,  /* default */
                G_PARAM_READWRITE);

    g_object_class_install_properties (o_class, NB_PROPS,
            donna_image_menu_item_props);

    donna_image_menu_item_signals[SIGNAL_LOAD_SUBMENU] =
        g_signal_new ("load-submenu",
                DONNA_TYPE_IMAGE_MENU_ITEM,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (DonnaImageMenuItemClass, load_submenu),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__BOOLEAN,
                G_TYPE_NONE,
                1,
                G_TYPE_BOOLEAN);

    g_type_class_add_private (klass, sizeof (DonnaImageMenuItemPrivate));
}

static void
donna_image_menu_item_init (DonnaImageMenuItem *item)
{
    item->priv = G_TYPE_INSTANCE_GET_PRIVATE (item,
            DONNA_TYPE_IMAGE_MENU_ITEM, DonnaImageMenuItemPrivate);

    /* this will be the first handler called, so we can block everything else */
    item->priv->sid_button_release = g_signal_connect (item,
            "button-release-event", (GCallback) menu_item_release_cb, item);
}

static void
donna_image_menu_item_finalize (GObject *object)
{
    DonnaImageMenuItemPrivate *priv;

    priv = ((DonnaImageMenuItem *) object)->priv;

    if (priv->sid_parent_button_release)
        g_signal_handler_disconnect (
                gtk_widget_get_parent ((GtkWidget *) object),
                priv->sid_parent_button_release);

    ((GObjectClass *) donna_image_menu_item_parent_class)->finalize (object);
}

static void
donna_image_menu_item_get_property (GObject        *object,
                                    guint           prop_id,
                                    GValue         *value,
                                    GParamSpec     *pspec)
{
    if (prop_id == PROP_IS_COMBINED)
        g_value_set_boolean (value, ((DonnaImageMenuItem *) object)->priv->is_combined);
    else if (prop_id == PROP_IS_LABEL_BOLD)
        g_value_set_boolean (value, ((DonnaImageMenuItem *) object)->priv->is_label_bold);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
donna_image_menu_item_set_property (GObject        *object,
                                    guint           prop_id,
                                    const GValue   *value,
                                    GParamSpec     *pspec)
{
    if (prop_id == PROP_IS_COMBINED)
        donna_image_menu_item_set_is_combined ((DonnaImageMenuItem *) object,
                g_value_get_boolean (value));
    if (prop_id == PROP_IS_LABEL_BOLD)
        donna_image_menu_item_set_label_bold ((DonnaImageMenuItem *) object,
                g_value_get_boolean (value));
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
popdown_menu (GtkWidget *item)
{
    GtkWidget *parent = gtk_widget_get_parent (item);

    /* locate top parent shell, e.g. main/first menu that was poped up */
    for (;;)
    {
        GtkWidget *w = gtk_menu_shell_get_parent_shell ((GtkMenuShell *) parent);
        if (w)
            parent = w;
        else
            break;
    }

    /* this will hide all menus */
    gtk_menu_popdown ((GtkMenu *) parent);
    /* we use this signal to unref the menu when it wasn't packed nowhere */
    g_signal_emit_by_name (parent, "deactivate");
}

static gboolean
menu_item_release_cb (GtkWidget             *widget,
                      GdkEventButton        *event,
                      DonnaImageMenuItem    *item)
{
    DonnaImageMenuItemPrivate *priv = item->priv;

    /* doesn't get triggered if there's a submenu, so we know we don't have one
     * yet */

    if (!priv->is_combined)
        return FALSE;

    if ((gint) event->x <= priv->item_width)
    {
        /* on the item part, make sure an event gets out & close the menu. We
         * "emit" the event blocking this handler instead of just returning
         * FALSE/letting it go through because our popdown_menu() will must
         * likely destroy the menu, and we need the item to still exists so the
         * click can be processed */
        g_signal_handler_block (widget, priv->sid_button_release);
        gtk_widget_event (widget, (GdkEvent *) event);
        g_signal_handler_unblock (widget, priv->sid_button_release);
        popdown_menu (widget);
        return TRUE;
    }
    else
        g_signal_emit (item, donna_image_menu_item_signals[SIGNAL_LOAD_SUBMENU], 0,
                TRUE);

    return TRUE;
}

static gboolean
parent_button_release_cb (GtkWidget             *parent,
                          GdkEventButton        *event,
                          DonnaImageMenuItem    *item)
{
    DonnaImageMenuItemPrivate *priv = item->priv;
    GtkWidget *w;

    if (!priv->is_combined)
        return FALSE;

    w = gtk_get_event_widget ((GdkEvent *) event);
    while (w && !GTK_IS_MENU_ITEM (w))
        w = gtk_widget_get_parent (w);

    if (!w || w != (GtkWidget *) item)
        return FALSE;

    if ((gint) event->x <= priv->item_width
            && gtk_menu_item_get_submenu ((GtkMenuItem *) item))
    {
        /* on the item part while a submenu was attached. So we close the
         * clicked menu, and make sure an event gets out */
        g_signal_handler_block (w, priv->sid_button_release);
        gtk_widget_event (w, (GdkEvent *) event);
        g_signal_handler_unblock (w, priv->sid_button_release);
        popdown_menu (w);
    }

    return FALSE;
}

static void
donna_image_menu_item_parent_set (GtkWidget          *widget,
                                  GtkWidget          *old_parent)
{
    DonnaImageMenuItemPrivate *priv = ((DonnaImageMenuItem *) widget)->priv;
    GtkWidget *parent;

    if (priv->sid_parent_button_release)
        g_signal_handler_disconnect (old_parent, priv->sid_parent_button_release);

    parent = gtk_widget_get_parent (widget);
    if (G_LIKELY (parent && GTK_IS_MENU_SHELL (parent)))
        priv->sid_parent_button_release = g_signal_connect (parent,
                "button-release-event",
                (GCallback) parent_button_release_cb, widget);
    else
        priv->sid_parent_button_release = 0;

    ((GtkWidgetClass *) donna_image_menu_item_parent_class)
        ->parent_set (widget, old_parent);
}

static gboolean
delayed_emit (DonnaImageMenuItem *item)
{
    DonnaImageMenuItemPrivate *priv = item->priv;

    g_signal_emit (item, donna_image_menu_item_signals[SIGNAL_LOAD_SUBMENU], 0,
            FALSE);

    priv->sid_timeout = 0;
    return FALSE;
}

static void
donna_image_menu_item_select (GtkMenuItem        *menuitem)
{
    DonnaImageMenuItemPrivate *priv = ((DonnaImageMenuItem *) menuitem)->priv;

    if (!priv->is_combined || gtk_menu_item_get_submenu (menuitem))
        goto chain;

    if (!priv->sid_timeout)
    {
        gint delay;

        g_object_get (gtk_widget_get_settings ((GtkWidget *) menuitem),
                "gtk-menu-popup-delay", &delay,
                NULL);
        if (delay > 0)
            priv->sid_timeout = g_timeout_add (delay,
                    (GSourceFunc) delayed_emit, (GtkWidget *) menuitem);
        else
            g_signal_emit (menuitem,
                    donna_image_menu_item_signals[SIGNAL_LOAD_SUBMENU], 0,
                    FALSE);
    }

chain:
    ((GtkMenuItemClass *) donna_image_menu_item_parent_class)->select (menuitem);
}

static void
donna_image_menu_item_deselect (GtkMenuItem          *menuitem)
{
    DonnaImageMenuItemPrivate *priv = ((DonnaImageMenuItem *) menuitem)->priv;

    if (priv->sid_timeout)
    {
        g_source_remove (priv->sid_timeout);
        priv->sid_timeout = 0;
    }

    ((GtkMenuItemClass *) donna_image_menu_item_parent_class)->deselect (menuitem);
}

/* the following is a copy/paste from gtkmenuitem.c
 *
 * gtkmenuitem.c
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 * Modified by the GTK+ Team and others 1997-2000.
 * */
static void
get_arrow_size (GtkWidget *widget,
                GtkWidget *child,
                gint      *size,
                gint      *spacing)
{
    PangoContext     *context;
    PangoFontMetrics *metrics;
    gfloat            arrow_scaling;
    gint              arrow_spacing;

    g_assert (size);

    gtk_widget_style_get (widget,
            "arrow-scaling", &arrow_scaling,
            "arrow-spacing", &arrow_spacing,
            NULL);

    if (spacing != NULL)
        *spacing = arrow_spacing;

    context = gtk_widget_get_pango_context (child);

    metrics = pango_context_get_metrics (context,
            pango_context_get_font_description (context),
            pango_context_get_language (context));

    *size = (PANGO_PIXELS (pango_font_metrics_get_ascent (metrics) +
                pango_font_metrics_get_descent (metrics)));

    pango_font_metrics_unref (metrics);

    *size = *size * arrow_scaling;
}

struct draw
{
    GtkContainer *container;
    cairo_t *cr;
};

static void
draw_child (GtkWidget *widget, struct draw *data)
{
    gtk_container_propagate_draw (data->container, widget, data->cr);
}

/* the following is pretty much a copy/paste from gtkmenuitem.c, except for the
 * is_combined bit obviously.
 *
 * gtkmenuitem.c
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 * Modified by the GTK+ Team and others 1997-2000.
 * */
static gboolean
donna_image_menu_item_draw (GtkWidget          *widget,
                            cairo_t            *cr)
{
    DonnaImageMenuItemPrivate *priv = ((DonnaImageMenuItem *) widget)->priv;
    GtkStateFlags state;
    GtkStyleContext *context;
    GtkBorder padding;
    GtkWidget *child, *parent;
    gint x, y, w, h, width, height;
    guint border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));
    gint arrow_size;
    struct draw data;

    state = gtk_widget_get_state_flags (widget);
    context = gtk_widget_get_style_context (widget);
    width = gtk_widget_get_allocated_width (widget);
    height = gtk_widget_get_allocated_height (widget);

    x = border_width;
    y = border_width;
    w = width - border_width * 2;
    h = height - border_width * 2;

    child = gtk_bin_get_child (GTK_BIN (widget));
    parent = gtk_widget_get_parent (widget);

    gtk_style_context_get_padding (context, state, &padding);

    if (priv->is_combined)
    {
        get_arrow_size (widget, child, &arrow_size, NULL);

        /* item width == the first highlight rectangle, which excludes the arrow
         * and some padding: 1 padding right when the arrow is drawn, 1 padding
         * left we add before its own rectangle, and another 1 padding right
         * used as non-highlighted separation between the two */
        priv->item_width = w - arrow_size - 2 * padding.right - padding.left;

        gtk_render_background (context, cr, x, y, priv->item_width, h);
        gtk_render_frame (context, cr, x, y, priv->item_width, h);

        gtk_render_background (context, cr,
                x + priv->item_width + padding.right, y,
                padding.left + arrow_size + padding.right, h);
        gtk_render_frame (context, cr,
                x + priv->item_width + padding.right, y,
                padding.left + arrow_size + padding.right, h);
    }
    else
    {
        gtk_render_background (context, cr, x, y, w, h);
        gtk_render_frame (context, cr, x, y, w, h);
    }

    if (priv->is_combined
            || (gtk_menu_item_get_submenu ((GtkMenuItem *) widget)
                && !GTK_IS_MENU_BAR (parent)))
    {
        gint arrow_x, arrow_y;
        GtkTextDirection direction;
        gdouble angle;

        if (!priv->is_combined)
            get_arrow_size (widget, child, &arrow_size, NULL);

        direction = gtk_widget_get_direction (widget);

        if (direction == GTK_TEXT_DIR_LTR)
        {
            arrow_x = x + w - arrow_size - padding.right;
            angle = G_PI / 2;
        }
        else
        {
            arrow_x = x + padding.left;
            angle = (3 * G_PI) / 2;
        }

        arrow_y = y + (h - arrow_size) / 2;

        gtk_render_arrow (context, cr, angle, arrow_x, arrow_y, arrow_size);
    }
    else if (!child)
    {
        gboolean wide_separators;
        gint     separator_height;

        gtk_widget_style_get (widget,
                "wide-separators",    &wide_separators,
                "separator-height",   &separator_height,
                NULL);
        if (wide_separators)
            gtk_render_frame (context, cr,
                    x + padding.left,
                    y + padding.top,
                    w - padding.left - padding.right,
                    separator_height);
        else
            gtk_render_line (context, cr,
                    x + padding.left,
                    y + padding.top,
                    x + w - padding.right - 1,
                    y + padding.top);
    }

    /* we don't chain up because then the background is done over ours, so we
     * just call propagate_draw on all children */
    data.container = (GtkContainer *) widget;
    data.cr = cr;
    gtk_container_forall ((GtkContainer *) widget, (GtkCallback) draw_child, &data);

    return FALSE;
}

GtkWidget *
donna_image_menu_item_new_with_label (const gchar        *label)
{
    return g_object_new (DONNA_TYPE_IMAGE_MENU_ITEM, "label", label, NULL);
}

void
donna_image_menu_item_set_is_combined (DonnaImageMenuItem *item,
                                       gboolean            combined)
{
    DonnaImageMenuItemPrivate *priv;

    g_return_if_fail (DONNA_IS_IMAGE_MENU_ITEM (item));
    priv = item->priv;

    if (priv->is_combined != combined)
    {
        priv->is_combined = combined;
        gtk_menu_item_set_reserve_indicator ((GtkMenuItem *) item, priv->is_combined);
        g_object_notify ((GObject *) item, "is-combined");
    }
}

gboolean
donna_image_menu_item_get_is_combined (DonnaImageMenuItem *item)
{
    g_return_val_if_fail (DONNA_IS_IMAGE_MENU_ITEM (item), FALSE);
    return item->priv->is_combined;
}

void
donna_image_menu_item_set_label_bold (DonnaImageMenuItem *item,
                                      gboolean            is_bold)
{
    DonnaImageMenuItemPrivate *priv;

    g_return_if_fail (DONNA_IS_IMAGE_MENU_ITEM (item));
    priv = item->priv;

    if (priv->is_label_bold != is_bold)
    {
        GtkWidget *child;

        priv->is_label_bold = is_bold;
        child = gtk_bin_get_child ((GtkBin *) item);
        if (GTK_IS_LABEL (child))
        {
            PangoAttrList *attrs;

            attrs = pango_attr_list_new ();
            if (is_bold)
                pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
            gtk_label_set_attributes ((GtkLabel *) child, attrs);
            pango_attr_list_unref (attrs);
        }
        else
            g_warning ("ImageMenuItem: Cannot set label bold, child isn't a GtkLabel");

        g_object_notify ((GObject *) item, "is-label-bold");
    }
}

gboolean
donna_image_menu_item_get_label_bold (DonnaImageMenuItem *item)
{
    g_return_val_if_fail (DONNA_IS_IMAGE_MENU_ITEM (item), FALSE);
    return item->priv->is_label_bold;
}

void
donna_image_menu_item_set_loading_submenu (DonnaImageMenuItem   *item,
                                           const gchar          *label)
{
    GtkMenu *menu;
    GtkWidget *w;

    g_return_if_fail (DONNA_IS_IMAGE_MENU_ITEM (item));
    g_return_if_fail (gtk_menu_item_get_submenu ((GtkMenuItem *) item) == NULL);

    menu = (GtkMenu *) gtk_menu_new ();
    w = gtk_image_menu_item_new_with_label ((label) ? label : "Please wait...");
    gtk_widget_set_sensitive (w, FALSE);
    gtk_menu_attach (menu, w, 0, 1, 0, 1);
    gtk_widget_show (w);
    gtk_menu_item_set_submenu ((GtkMenuItem *) item, (GtkWidget *) menu);
    if ((GtkWidget *) item == gtk_menu_shell_get_selected_item (
                (GtkMenuShell *) gtk_widget_get_parent ((GtkWidget *) item)))
        gtk_menu_item_select ((GtkMenuItem *) item);
}
