/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * embedder.c
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

#include <gtk/gtkx.h>
#include "embedder.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:embedder
 * @Short_description: Extend GtkSocket to catch mouse events
 *
 * When embedding an external application via XEMBED (i.e. using #GtkSocket),
 * mouse events aren't processed by the #GtkSocket/embedder, as they're sent
 * directly to subwindows of the plugged window.
 *
 * When the application implements a click-to-focus model, it will send the
 * XEMBED_REQUEST_FOCUS message as needed to the embedder, so the focus can be
 * set to the #GtkSocket appropriately.
 *
 * However, not all applications do, and specifically terminal emulator usually
 * do not (e.g. urxvt doesn't, neither dos xterm). To handle this,
 * #DonnaEmbedder can place an invisible window in front of the plugged window
 * to catch mouse events. See donna_embedder_set_catch_events() for more.
 */

enum
{
    PROP_0,

    PROP_CATCH_EVENTS,

    NB_PROPS
};

struct window
{
    Window id;
    Window above;
    int x;
    int y;
    int width;
    int height;
    int is_mapped;
};

struct _DonnaEmbedderPrivate
{
    GdkWindow       *window;
    gboolean         catch_events;
    GArray          *plug_windows;
    struct window   *w_grab;
};

static GParamSpec * donna_embedder_props[NB_PROPS] = { NULL, };


static void         donna_embedder_set_property         (GObject        *object,
                                                         guint           prop_id,
                                                         const GValue   *value,
                                                         GParamSpec     *pspec);
static void         donna_embedder_get_property         (GObject        *object,
                                                         guint           prop_id,
                                                         GValue         *value,
                                                         GParamSpec     *pspec);
static void         donna_embedder_realize              (GtkWidget      *widget);
static void         donna_embedder_unrealize            (GtkWidget      *widget);
static void         donna_embedder_map                  (GtkWidget      *widget);
static void         donna_embedder_unmap                (GtkWidget      *widget);
static void         donna_embedder_size_allocate        (GtkWidget      *widget,
                                                         GtkAllocation  *allocation);
static gboolean     donna_embedder_button_event         (GtkWidget      *widget,
                                                         GdkEventButton *event);
static gboolean     donna_embedder_motioned             (GtkWidget      *widget,
                                                         GdkEventMotion *event);
static gboolean     donna_embedder_scrolled             (GtkWidget      *widget,
                                                         GdkEventScroll *event);
static void         donna_embedder_plug_added           (GtkSocket      *socket);

G_DEFINE_TYPE (DonnaEmbedder, donna_embedder, GTK_TYPE_SOCKET)


static void
donna_embedder_class_init (DonnaEmbedderClass *klass)
{
    GObjectClass *o_class;
    GtkWidgetClass *w_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property           = donna_embedder_set_property;
    o_class->get_property           = donna_embedder_get_property;

    w_class = (GtkWidgetClass *) klass;
    w_class->realize                = donna_embedder_realize;
    w_class->unrealize              = donna_embedder_unrealize;
    w_class->map                    = donna_embedder_map;
    w_class->unmap                  = donna_embedder_unmap;
    w_class->size_allocate          = donna_embedder_size_allocate;
    w_class->button_press_event     = donna_embedder_button_event;
    w_class->button_release_event   = donna_embedder_button_event;
    w_class->motion_notify_event    = donna_embedder_motioned;
    w_class->scroll_event           = donna_embedder_scrolled;

    /**
     * DonnaEmbedder:catch-events:
     *
     * Whether to catch mouse events or not. See
     * donna_embedder_set_catch_events() for more.
     */
    donna_embedder_props[PROP_CATCH_EVENTS] =
        g_param_spec_boolean ("catch-events", "catch-events",
                "Whether to catch mouse events or not",
                TRUE,
                G_PARAM_READWRITE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_embedder_props);

    g_type_class_add_private (klass, sizeof (DonnaEmbedderPrivate));
}

static void
donna_embedder_init (DonnaEmbedder *embedder)
{
    embedder->priv = G_TYPE_INSTANCE_GET_PRIVATE (embedder,
            DONNA_TYPE_EMBEDDER, DonnaEmbedderPrivate);
    embedder->priv->catch_events = TRUE;
    gtk_widget_add_events ((GtkWidget *) embedder,
            GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
            | GDK_SCROLL_MASK | GDK_MOTION_NOTIFY);
    /* we connect to be triggered first/asap */
    g_signal_connect (embedder, "plug-added",
            (GCallback) donna_embedder_plug_added, NULL);
}

static void
donna_embedder_set_property (GObject        *object,
                             guint           prop_id,
                             const GValue   *value,
                             GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_CATCH_EVENTS))
    {
        donna_embedder_set_catch_events ((DonnaEmbedder *) object,
                g_value_get_boolean (value));
    }
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
donna_embedder_get_property (GObject        *object,
                             guint           prop_id,
                             GValue         *value,
                             GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_CATCH_EVENTS))
        g_value_set_boolean (value,
                donna_embedder_get_catch_events ((DonnaEmbedder *) object));
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
donna_embedder_realize (GtkWidget      *widget)
{
    DonnaEmbedderPrivate *priv = ((DonnaEmbedder *) widget)->priv;
    GtkAllocation allocation;
    GdkWindowAttr attributes;
    gint attributes_mask;

    ((GtkWidgetClass *) donna_embedder_parent_class)->realize (widget);

    gtk_widget_get_allocation (widget, &allocation);

    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.wclass = GDK_INPUT_ONLY;
    attributes_mask = GDK_WA_X | GDK_WA_Y;
    attributes.event_mask = gtk_widget_get_events (widget)
        | GDK_EXPOSURE_MASK     | GDK_BUTTON_MOTION_MASK
        | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
        | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK;

    priv->window = gdk_window_new (gtk_widget_get_parent_window (widget),
            &attributes, attributes_mask);
    gtk_widget_register_window (widget, priv->window);

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): Created window %lu",
                widget, GDK_WINDOW_XID (priv->window)));
}

static void
donna_embedder_unrealize (GtkWidget      *widget)
{
    DonnaEmbedderPrivate *priv = ((DonnaEmbedder *) widget)->priv;
    if (priv->window)
    {
        gtk_widget_unregister_window (widget, priv->window);
        gdk_window_destroy (priv->window);
        DONNA_DEBUG (EMBEDDER, NULL,
                g_debug ("Embedder (%p): Destroyed window %lu",
                    widget, GDK_WINDOW_XID (priv->window)));
        priv->window = NULL;
    }
    if (priv->plug_windows)
    {
        g_array_free (priv->plug_windows, TRUE);
        priv->plug_windows = NULL;
    }
    ((GtkWidgetClass *) donna_embedder_parent_class)->unrealize (widget);
}

static void
donna_embedder_map (GtkWidget      *widget)
{
    DonnaEmbedderPrivate *priv = ((DonnaEmbedder *) widget)->priv;
    ((GtkWidgetClass *) donna_embedder_parent_class)->map (widget);
    if (priv->window && priv->catch_events)
        gdk_window_show (priv->window);
}

static void
donna_embedder_unmap (GtkWidget      *widget)
{
    DonnaEmbedderPrivate *priv = ((DonnaEmbedder *) widget)->priv;
    if (priv->window && priv->catch_events)
        gdk_window_hide (priv->window);
    ((GtkWidgetClass *) donna_embedder_parent_class)->unmap (widget);
}

static void
donna_embedder_size_allocate (GtkWidget      *widget,
                              GtkAllocation  *allocation)
{
    DonnaEmbedderPrivate *priv = ((DonnaEmbedder *) widget)->priv;

    ((GtkWidgetClass *) donna_embedder_parent_class)->size_allocate (widget, allocation);
    if (!priv->window || !gtk_widget_get_realized (widget))
        return;

    gdk_window_move_resize (priv->window,
            allocation->x, allocation->y, allocation->width, allocation->height);
    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): Window %lu at %dx%d is %dx%d",
                widget, GDK_WINDOW_XID (priv->window),
                allocation->x, allocation->y,
                allocation->width, allocation->height));
}

static struct window *
get_window (DonnaEmbedder *embedder, Window window)
{
    GArray *arr = embedder->priv->plug_windows;
    struct window *w;

    if (arr)
    {
        guint i;

        for (i = 0; i < arr->len; ++i)
        {
            w = &g_array_index (arr, struct window, i);
            if (w->id == window)
                break;
        }

        if (i >= arr->len)
            w = NULL;
    }
    else
    {
        arr = embedder->priv->plug_windows = g_array_new (FALSE, TRUE,
                sizeof (struct window));
        w = NULL;
    }

    if (!w)
    {
        DONNA_DEBUG (EMBEDDER, NULL,
                g_debug2 ("Embedder (%p): Adding window %lu",
                    embedder, window));
        g_array_set_size (arr, arr->len + 1);
        w = &g_array_index (arr, struct window, arr->len - 1);
        w->id = window;
    }

    return w;
}

static gint
cmp_window (struct window *w1, struct window *w2, gpointer _window)
{
    Window window = GPOINTER_TO_UINT (_window);

    if (w1->above == w2->id || w2->id == window)
        return -1;
    else if (w2->above == w1->id || w1->id == window)
        return 1;
    return 0;
}

static void
get_attr_and_add_window (DonnaEmbedder *embedder, Window window, Window above)
{
    struct window *w;
    XWindowAttributes attr;
    Status st;

    gdk_error_trap_push ();
    st = XGetWindowAttributes (GDK_DISPLAY_XDISPLAY (
                gtk_widget_get_display ((GtkWidget *) embedder)),
            window, &attr);
    gdk_error_trap_pop_ignored ();
    if (st == 0)
        /* this could happen if e.g. the window has already been destroyed,
         * e.g. because the program launch inside the terminal is already
         * finished (and there was no -hold) */
        return;

    w = get_window (embedder, window);
    if (above != 0 && above != (Window) -1)
        w->above = above;
    w->x         = attr.x;
    w->y         = attr.y;
    w->width     = attr.width;
    w->height    = attr.height;
    w->is_mapped = attr.map_state != IsUnmapped;

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): %s: Window %lu at %dx%d is %dx%d",
                embedder,
                (above == (Window) -1) ? "MapNotify" : "Children",
                w->id, w->x, w->y, w->width, w->height));

    /* we keep the array sorted (according to w->above, putting socket's
     * plug window last), so that when looking for the window to send event
     * to, we can use the first match */
    g_array_sort_with_data (embedder->priv->plug_windows,
            (GCompareDataFunc) cmp_window, GUINT_TO_POINTER (GDK_WINDOW_XID (
                    gtk_socket_get_plug_window ((GtkSocket *) embedder))));
}

static GdkFilterReturn
event_filter (GdkXEvent *gdk_xevent, GdkEvent *event, GtkSocket *socket)
{
    XEvent *xevent = (XEvent *) gdk_xevent;
    Window window;

    window = GDK_WINDOW_XID (gtk_socket_get_plug_window (socket));

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug2 ("Embedder (%p): event %d on window %lu (plug window=%lu)",
                socket, xevent->type, xevent->xany.window, window));

    if (xevent->type == MapNotify)
        get_attr_and_add_window ((DonnaEmbedder *) socket,
                xevent->xmap.window, (Window) -1);
    else if (xevent->type == UnmapNotify && xevent->xany.window == window)
    {
        GArray *arr = ((DonnaEmbedder *) socket)->priv->plug_windows;
        guint i;

        for (i = 0; i < arr->len; ++i)
        {
            struct window *w = &g_array_index (arr, struct window, i);
            if (w->id == xevent->xunmap.window)
            {
                w->is_mapped = 0;
                break;
            }
        }
    }
    else if (xevent->type == ConfigureNotify && xevent->xany.window == window)
    {
        struct window *w;

        w = get_window ((DonnaEmbedder *) socket, xevent->xconfigure.window);
        w->above    = xevent->xconfigure.above;
        w->x        = xevent->xconfigure.x;
        w->y        = xevent->xconfigure.y;
        w->width    = xevent->xconfigure.width;
        w->height   = xevent->xconfigure.height;

        DONNA_DEBUG (EMBEDDER, NULL,
                g_debug ("Embedder (%p): ConfigureNotify: "
                    "Window %lu at %dx%d is %dx%d (above %lu)",
                    socket, w->id, w->x, w->y, w->width, w->height, w->above));

        /* see above */
        g_array_sort_with_data (((DonnaEmbedder *) socket)->priv->plug_windows,
                (GCompareDataFunc) cmp_window, GUINT_TO_POINTER (window));
    }

    return GDK_FILTER_CONTINUE;
}

static void
donna_embedder_plug_added (GtkSocket      *socket)
{
    DonnaEmbedderPrivate *priv = ((DonnaEmbedder *) socket)->priv;
    GdkDisplay *display;
    GdkWindow *win;
    XWindowAttributes attr;
    Window root;
    Window parent;
    Window *children;
    guint n_children;
    Status st;

    win = gtk_socket_get_plug_window (socket);
    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): Socket plugged (%lu)",
                socket, GDK_WINDOW_XID (win)));
    if (G_UNLIKELY (!win))
        return;

    /* to keep track of plug's windows (to send events) */
    display = gtk_widget_get_display ((GtkWidget *) socket);
    gdk_error_trap_push ();
    st = XGetWindowAttributes (GDK_DISPLAY_XDISPLAY (display),
            GDK_WINDOW_XID (win), &attr);
    if (st != 0)
    {
        XSetWindowAttributes set_attr;

        set_attr.event_mask = attr.your_event_mask | SubstructureNotifyMask;
        XChangeWindowAttributes(GDK_DISPLAY_XDISPLAY (display), GDK_WINDOW_XID (win),
                CWEventMask, &set_attr);
    }
    else
        g_warning ("Embedder (%p): Failed (%d) to get window attributes "
                "of plugged window %lu",
                socket, st, GDK_WINDOW_XID (win));
    gdk_error_trap_pop_ignored ();
    gdk_window_add_filter (win, (GdkFilterFunc) event_filter, socket);

    /* let's get the already created/configured windows */
    gdk_error_trap_push ();
    st = XQueryTree (GDK_DISPLAY_XDISPLAY (display), GDK_WINDOW_XID (win),
            &root, &parent, &children, &n_children);
    gdk_error_trap_pop_ignored ();
    if (st != 0 && n_children > 0)
    {
        guint i;

        for (i = 0; i < n_children; ++i)
            get_attr_and_add_window ((DonnaEmbedder *) socket,
                    children[i], (i > 0) ? children[i - 1] : 0);
        XFree (children);
    }
    else if (st == 0)
        g_warning ("Embedder (%p): Failed to get list of child windows of %lu",
                socket, GDK_WINDOW_XID (win));

    /* for our window to be on top, so we get mouse events */
    gdk_window_raise (priv->window);
    gdk_window_ensure_native (priv->window);
}

static struct window *
get_window_at_pos (DonnaEmbedder *embedder, gint x, gint y)
{
    GArray *arr = embedder->priv->plug_windows;
    struct window *w = NULL;
    guint i;

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug3 ("Embedder (%p): Looking for window at %dx%d",
                embedder, x, y));

    /* array is sorted, so we use the first match */
    for (i = 0; i < arr->len; ++i)
    {
        w = &g_array_index (arr, struct window, i);
        DONNA_DEBUG (EMBEDDER, NULL,
                g_debug3 ("Embedder (%p): Test window %lu (at %dx%d is %dx%d; mapped=%d)",
                    embedder, w->id, w->x, w->y, w->width, w->height, w->is_mapped));
        if (w->is_mapped && x >= w->x && x <= w->x + w->width
                && y >= w->y && y <= w->y + w->height)
            break;
    }

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug2 ("Embedder (%p): Found window %lu at %dx%d",
                embedder, w->id, x, y));
    return w;
}

static gboolean
donna_embedder_button_event (GtkWidget      *widget,
                             GdkEventButton *event)
{
    GdkDisplay *display;
    XButtonEvent e;
    struct window *w;

    display = gtk_widget_get_display (widget);
    if (((DonnaEmbedder *) widget)->priv->w_grab)
        w = ((DonnaEmbedder *) widget)->priv->w_grab;
    else
        w = get_window_at_pos ((DonnaEmbedder *) widget, (gint) event->x, (gint) event->y);

    e.type = (event->type == GDK_BUTTON_PRESS) ? ButtonPress : ButtonRelease;
    e.serial = 0;
    e.send_event = True;
    e.display = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (widget));
    e.window = w->id;
    e.root = GDK_WINDOW_XID (event->window);
    e.subwindow = 0;
    e.time = event->time;
    e.x = (int) event->x - w->x;
    e.y = (int) event->y - w->y;
    e.x_root = (int) event->x_root;
    e.y_root = (int) event->y_root;
    e.state = event->state;
    e.button = event->button;
    e.same_screen = True;

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): send %s to %lu at %dx%d",
                widget,
                (e.type == ButtonPress) ? "ButtonPress" : "ButtonRelease",
                e.window,
                e.x, e.y));

    gdk_error_trap_push ();
    XSendEvent (GDK_DISPLAY_XDISPLAY (display), e.window, True,
            (e.type == ButtonPress) ? ButtonPressMask : ButtonReleaseMask,
            (XEvent *) &e);
    gdk_error_trap_pop_ignored ();

    if (event->type == GDK_BUTTON_PRESS)
        ((DonnaEmbedder *) widget)->priv->w_grab = w;
    else
        ((DonnaEmbedder *) widget)->priv->w_grab = NULL;

    return GDK_EVENT_STOP;
}

static gboolean
donna_embedder_motioned (GtkWidget      *widget,
                         GdkEventMotion *event)
{
    GdkDisplay *display;
    XMotionEvent e;
    struct window *w;
    long mask;

    display = gtk_widget_get_display (widget);
    if (((DonnaEmbedder *) widget)->priv->w_grab)
        w = ((DonnaEmbedder *) widget)->priv->w_grab;
    else
        w = get_window_at_pos ((DonnaEmbedder *) widget, (gint) event->x, (gint) event->y);

    e.type = MotionNotify;
    e.serial = 0;
    e.send_event = True;
    e.display = GDK_DISPLAY_XDISPLAY (display);
    e.window = w->id;
    e.root = GDK_WINDOW_XID (event->window);
    e.subwindow = 0;
    e.time = event->time;
    e.x = (int) event->x - w->x;
    e.y = (int) event->y - w->y;
    e.x_root = (int) event->x_root;
    e.y_root = (int) event->y_root;
    e.state = event->state;
    e.is_hint = NotifyNormal;
    e.same_screen = True;

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): send MotionNotify to %lu at %dx%d",
                widget,
                e.window,
                e.x, e.y));

    mask = PointerMotionMask;
    if (event->state & GDK_BUTTON1_MASK)
        mask |= ButtonMotionMask | Button1MotionMask;
    if (event->state & GDK_BUTTON2_MASK)
        mask |= ButtonMotionMask | Button2MotionMask;
    if (event->state & GDK_BUTTON3_MASK)
        mask |= ButtonMotionMask | Button3MotionMask;
    if (event->state & GDK_BUTTON4_MASK)
        mask |= ButtonMotionMask | Button4MotionMask;
    if (event->state & GDK_BUTTON5_MASK)
        mask |= ButtonMotionMask | Button5MotionMask;

    gdk_error_trap_push ();
    XSendEvent (GDK_DISPLAY_XDISPLAY (display), e.window, True,
            mask, (XEvent *) &e);
    gdk_error_trap_pop_ignored ();

    return GDK_EVENT_STOP;
}

static gboolean
donna_embedder_scrolled (GtkWidget      *widget,
                         GdkEventScroll *event)
{
    GdkDisplay *display;
    XButtonEvent e;
    struct window *w;

    if (event->direction != GDK_SCROLL_UP && event->direction != GDK_SCROLL_DOWN)
        return GDK_EVENT_STOP;

    display = gtk_widget_get_display (widget);
    w = get_window_at_pos ((DonnaEmbedder *) widget, (gint) event->x, (gint) event->y);

    e.type = ButtonPress;
    e.serial = 0;
    e.send_event = True;
    e.display = GDK_DISPLAY_XDISPLAY (display);
    e.window = w->id;
    e.root = GDK_WINDOW_XID (event->window);
    e.subwindow = 0;
    e.time = event->time;
    e.x = (int) event->x - w->x;
    e.y = (int) event->y - w->y;
    e.x_root = (int) event->x_root;
    e.y_root = (int) event->y_root;
    e.state = event->state;
    e.button = (event->direction == GDK_SCROLL_UP) ? Button4 : Button5;
    e.same_screen = True;

    DONNA_DEBUG (EMBEDDER, NULL,
            g_debug ("Embedder (%p): send ButtonPress/ButtonRelease %s to %lu at %dx%d",
                widget,
                (event->direction == GDK_SCROLL_UP) ? "up" : "down",
                e.window,
                e.x, e.y));

    gdk_error_trap_push ();
    XSendEvent (GDK_DISPLAY_XDISPLAY (display), e.window, True,
            ButtonPressMask, (XEvent *) &e);
    e.type = ButtonRelease;
    XSendEvent (GDK_DISPLAY_XDISPLAY (display), e.window, True,
            ButtonReleaseMask, (XEvent *) &e);
    gdk_error_trap_pop_ignored ();

    return GDK_EVENT_STOP;
}

/**
 * donna_embedder_set_catch_events:
 * @embedder: A #DonnaEmbedder
 * @catch_events: Whether to catch mouse events or not
 *
 * When embedding, mouse events aren't usually caught by the #GtkSocket (here
 * @embedder), and cannot be when send to a subwindow of the plugged window.
 * While this might not always be an issue, when embedding a terminal emulator
 * it can be, as they usually don't implement a click-to-focus model, IOW while
 * they process mouse evens (e.g. for selection) they will not send an
 * XEMBED_REQUEST_FOCUS message asking to be focused.
 *
 * This can be problematic, so a #DonnaEmbedder is used, to place an invisible
 * #GdkWindow in front of the plugged window, so as to catch mouse events (e.g.
 * button press).
 *
 * Unless events were handled, they'll be send to the window which would
 * have received them had there not be an invisible window in the way.
 *
 * This might not be perfect, but it should allow to e.g. embed a terminal
 * emulator, and handle a click-to-focus model without breaking any
 * functionnality.
 *
 * This #GdkWindow is always created, but will only be visible when catching
 * events, and you can change this setting while the terminal (or whatever is
 * plugged) is running.
 */
void
donna_embedder_set_catch_events (DonnaEmbedder  *embedder,
                                 gboolean        catch_events)
{
    DonnaEmbedderPrivate *priv;

    g_return_if_fail (DONNA_IS_EMBEDDER (embedder));
    priv = embedder->priv;

    if (priv->catch_events == catch_events)
        return;

    priv->catch_events = catch_events;
    if (priv->window && gtk_widget_get_mapped ((GtkWidget *) embedder))
    {
        if (priv->catch_events)
            gdk_window_show (priv->window);
        else
            gdk_window_hide (priv->window);
    }
}

/**
 * donna_embedder_get_catch_events:
 * @embedder: A #DonnaEmbedder
 *
 * Returns whether mouse events are being caught or not. See
 * donna_embedder_set_catch_events() for more.
 *
 * Returns: Whether mouse events are caught by @embedder or not
 */
gboolean
donna_embedder_get_catch_events (DonnaEmbedder  *embedder)
{
    g_return_val_if_fail (DONNA_IS_EMBEDDER (embedder), FALSE);
    return embedder->priv->catch_events;
}

/**
 * donna_embedder_new:
 * @catch_events: Whether to catch (mouse) events or not. See
 * donna_embedder_set_catch_events() for more.
 *
 * Creates a new #DonnaEmbedder
 *
 * Returns: (transfer floating): The newly created #DonnaEmbedder
 */
GtkWidget *
donna_embedder_new (gboolean        catch_events)
{
    return g_object_new (DONNA_TYPE_EMBEDDER, "catch-events", catch_events, NULL);
}
