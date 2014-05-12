/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * terminal.c
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
#include <gtk/gtkx.h>
#include "terminal.h"
#include "app.h"
#include "embedder.h"
#include "context.h"
#include "task-process.h"
#include "macros.h"
#include "closures.h"
#include "debug.h"

/**
 * SECTION:terminal
 * @Short_description: Embedded terminal
 *
 * A #DonnaTerminal is used to provide embedded terminal emulators within donna.
 * This is done using the XEMBED protocol.
 *
 * First, a terminal must be created and exists in the current layout. It will
 * not be visible unless there's an embedded terminal running.
 *
 * Option <systemitem>always_show_tabs</systemitem> will determine whether the
 * tab bar is visible even if there's only one tab (true), or only when there
 * are at least 2 tabs (false, the default).
 *
 * Double clicking a tab will send the focus to the embedded terminal.
 * By default, clicking in a terminal should also give it focus. Note that this
 * is done via #DonnaEmbedder as (most) terminal emulators do not implement a
 * click-to-focus model.
 * If this is causing issue with your terminal, you can disable it by setting
 * boolean option <systemitem>catch_events</systemitem> to false
 *
 * Option <systemitem>focusing_click</systemitem> makes it that a left click on
 * the terminal will only focus it, but the click (button press) won't be sent
 * to the terminal. This can be disabled by setting it to false. Also note that
 * this obviously only works when option <systemitem>catch_events</systemitem>
 * is true.
 *
 * As usual, options can be set under
 * <systemitem>terminals/&lt;TERMINAL&gt;/</systemitem> for terminal-specific
 * options, or under <systemitem>defaults/terminals</systemitem> for options
 * common to all terminals.
 *
 * By default, tabs will use the command line (ran inside the terminal, i.e.
 * argument @cmdline from donna_terminal_add_tab()) as title. donna will then
 * update the title as a window manager would, relying on the properties
 * <systemitem>_NET_WM_NAME</systemitem> (or <systemitem>WM_NAME</systemitem>)
 * set on the window by the emulator.
 *
 * Starting a new embedded terminal is done by adding a new tab (command
 * terminal_add_tab()) The tab will be automatically removed when the terminal
 * emulator process ends. To keep the window open even after the process has
 * finished, you need to ask the emulator to not close the window; e.g. urxvt
 * has an option <systemitem>-hold</systemitem> for this purpose. In this case,
 * you'll have to use command terminal_remove_page() or terminal_remove_tab() to
 * remove the tab; or cancel its running task.
 * Note that this process is handled as a task through the task manager, and can
 * therefore be cancelled that way.
 */

enum
{
    PROP_0,

    PROP_ACTIVE_TAB,

    NB_PROPS
};

enum
{
    SIGNAL_TAB_ADDED,
    SIGNAL_TAB_REMOVED,
    SIGNAL_TAB_TITLE_CHANGED,
    NB_SIGNALS
};

struct term
{
    DonnaTerminal *terminal;
    guint id;
    GtkSocket *socket;
    DonnaTask *task;
    gboolean is_plugged;
    gboolean focus_on_plug;
    gboolean has_net_name;
};

struct _DonnaTerminalPrivate
{
    DonnaApp    *app;
    gchar       *name;
    guint        active_tab;
    guint        last_id;
};

static GParamSpec * donna_terminal_props[NB_PROPS] = { NULL, };
static guint        donna_terminal_signals[NB_SIGNALS] = { 0, };


static void         donna_terminal_page_removed             (GtkNotebook    *nb,
                                                             GtkWidget      *child,
                                                             guint           page);
static gboolean     donna_terminal_button_press_event       (GtkWidget      *widget,
                                                             GdkEventButton *event);
static void         donna_terminal_finalize                 (GObject        *object);
static void         donna_terminal_set_property             (GObject        *object,
                                                             guint           prop_id,
                                                             const GValue   *value,
                                                             GParamSpec     *pspec);
static void         donna_terminal_get_property             (GObject        *object,
                                                             guint           prop_id,
                                                             GValue         *value,
                                                             GParamSpec     *pspec);

G_DEFINE_TYPE (DonnaTerminal, donna_terminal, GTK_TYPE_NOTEBOOK)


static void
donna_terminal_class_init (DonnaTerminalClass *klass)
{
    GObjectClass *o_class;
    GtkWidgetClass *w_class;
    GtkNotebookClass *nb_class;

    o_class = (GObjectClass *) klass;
    o_class->finalize       = donna_terminal_finalize;
    o_class->set_property   = donna_terminal_set_property;
    o_class->get_property   = donna_terminal_get_property;

    w_class = (GtkWidgetClass *) klass;
    w_class->button_press_event = donna_terminal_button_press_event;

    nb_class = (GtkNotebookClass *) klass;
    nb_class->page_removed  = donna_terminal_page_removed;

    /**
     * DonnaTerminal:active-tab:
     *
     * ID of the tab currently active.
     */
    donna_terminal_props[PROP_ACTIVE_TAB] =
        g_param_spec_uint ("active-tab", "ID of the active tab",
                "ID of the activate tab",
                0,
                G_MAXUINT,
                0,
                G_PARAM_READWRITE);

    g_object_class_install_properties (o_class, NB_PROPS, donna_terminal_props);

    /**
     * DonnaTerminal::tab-added:
     *
     * A new tab has been added
     */
    donna_terminal_signals[SIGNAL_TAB_ADDED] =
        g_signal_new ("tab-added",
                DONNA_TYPE_TERMINAL,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTerminalClass, tab_added),
                NULL,
                NULL,
                g_cclosure_user_marshal_VOID__UINT_BOOLEAN,
                G_TYPE_NONE,
                2,
                G_TYPE_UINT,
                G_TYPE_BOOLEAN);

    /**
     * DonnaTerminal::tab-removed:
     *
     * A tab has been removed
     */
    donna_terminal_signals[SIGNAL_TAB_REMOVED] =
        g_signal_new ("tab-removed",
                DONNA_TYPE_TERMINAL,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTerminalClass, tab_removed),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__UINT,
                G_TYPE_NONE,
                1,
                G_TYPE_UINT);

    /**
     * DonnaTerminal::tab-title-changed:
     *
     * The title of a tab has changed.
     *
     * This is the window title set by the embedded terminal.
     */
    donna_terminal_signals[SIGNAL_TAB_TITLE_CHANGED] =
        g_signal_new ("tab-title-changed",
                DONNA_TYPE_TERMINAL,
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (DonnaTerminalClass, tab_title_changed),
                NULL,
                NULL,
                g_cclosure_user_marshal_VOID__UINT_STRING,
                G_TYPE_NONE,
                2,
                G_TYPE_UINT,
                G_TYPE_STRING);

    g_type_class_add_private (klass, sizeof (DonnaTerminalPrivate));
}

static void
donna_terminal_init (DonnaTerminal *terminal)
{
    terminal->priv = G_TYPE_INSTANCE_GET_PRIVATE (terminal,
            DONNA_TYPE_TERMINAL, DonnaTerminalPrivate);
}

static void
donna_terminal_finalize (GObject *object)
{
    DonnaTerminalPrivate *priv = ((DonnaTerminal *) object)->priv;
    DONNA_DEBUG (MEMORY, NULL,
            g_debug ("Terminal '%s' finalizing", priv->name));

    g_free (priv->name);
    ((GObjectClass *) donna_terminal_parent_class)->finalize (object);
}

static void
donna_terminal_set_property (GObject        *object,
                             guint           prop_id,
                             const GValue   *value,
                             GParamSpec     *pspec)
{
    DonnaTerminal *terminal = (DonnaTerminal *) object;

    switch (prop_id)
    {
        case PROP_ACTIVE_TAB:
            donna_terminal_set_active_tab (terminal, g_value_get_uint (value),
                    FALSE, NULL);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
donna_terminal_get_property (GObject        *object,
                             guint           prop_id,
                             GValue         *value,
                             GParamSpec     *pspec)
{
    DonnaTerminal *terminal = (DonnaTerminal *) object;

    switch (prop_id)
    {
        case PROP_ACTIVE_TAB:
            g_value_set_uint (value, donna_terminal_get_active_tab (terminal));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static gboolean
donna_terminal_button_press_event (GtkWidget      *widget,
                                   GdkEventButton *event)
{
    if (event->type == GDK_2BUTTON_PRESS)
    {
        gint page;
        struct term *term;

        page = gtk_notebook_get_current_page ((GtkNotebook *) widget);
        if (page == -1)
            return FALSE;
        term = g_object_get_data ((GObject *) gtk_notebook_get_nth_page (
                    (GtkNotebook *) widget, page),
                "_terminal");
        gtk_widget_set_can_focus ((GtkWidget *) term->socket, TRUE);
        gtk_widget_grab_focus ((GtkWidget *) term->socket);
        return TRUE;
    }

    return GTK_WIDGET_CLASS (donna_terminal_parent_class)
        ->button_press_event (widget, event);
}

static gboolean
_config_get_boolean (DonnaTerminal  *terminal,
                     DonnaConfig    *config,
                     const gchar    *option,
                     gboolean        def)
{
    DonnaTerminalPrivate *priv = terminal->priv;
    gboolean val;

    if (!config)
        config = donna_app_peek_config (priv->app);

    if (!donna_config_get_boolean (config, NULL, &val,
                "terminals/%s/%s", priv->name, option))
        if (!donna_config_get_boolean (config, NULL, &val,
                    "defaults/terminals/%s", option))
        {
            donna_config_set_boolean (config, NULL, def,
                    "defaults/terminals/%s", option);
            val = def;
        }

    return val;
}

static gchar *
_config_get_string (DonnaTerminal   *terminal,
                    DonnaConfig     *config,
                    const gchar     *option,
                    const gchar     *extra)
{
    DonnaTerminalPrivate *priv = terminal->priv;
    gchar *val;

    if (!config)
        config = donna_app_peek_config (priv->app);

    if (!donna_config_get_string (config, NULL, &val,
                "terminals/%s/%s%s", priv->name, option, extra)
            && !donna_config_get_string (config, NULL, &val,
                "defaults/terminals/%s", option))
        val = NULL;

    return val;
}

#define cfg_get_always_show_tabs(t,c) \
    _config_get_boolean (t, c, "always_show_tabs", FALSE)
#define cfg_get_catch_events(t,c) \
    _config_get_boolean (t, c, "catch_events", TRUE)
#define cfg_get_focusing_click(t,c) \
    _config_get_boolean (t, c, "focusing_click", TRUE)
#define cfg_get_cmdline(t,c) \
    _config_get_string (t, c, "cmdline", "")
#define cfg_get_cmdline_extra(t,c,e) \
    _config_get_string (t, c, "cmdline_", e)

static void
free_term (struct term *term)
{
    g_debug("free tab %d",term->id);
    g_object_unref (term->task);
    g_object_unref (term->socket);
    g_slice_free (struct term, term);
}

static void
donna_terminal_page_removed (GtkNotebook    *nb,
                             GtkWidget      *child,
                             guint           page)
{
    DonnaTerminalPrivate *priv;
    struct term *term;
    gint num;

    priv = ((DonnaTerminal *) nb)->priv;
    term = g_object_get_data ((GObject *) child, "_terminal");

    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug ("Terminal '%s': Page %d (tab %d) removed",
                priv->name, page, (term) ? (gint) term->id : -1));

    if (!term)
        /* the page was added, then removed during add_tab() */
        return;

    if (!(donna_task_get_state (term->task) & DONNA_TASK_POST_RUN))
        donna_task_cancel (term->task);

    num = gtk_notebook_get_n_pages (nb);
    if (num == 0)
    {
        gtk_widget_hide ((GtkWidget *) nb);
        donna_app_set_focus (priv->app, "treeview", ":active", NULL);
        priv->last_id = 0;
    }
    else if (num == 1)
        if (!cfg_get_always_show_tabs ((DonnaTerminal *) nb, NULL))
            gtk_notebook_set_show_tabs (nb, FALSE);

    if (priv->last_id == term->id)
        --priv->last_id;
}

/**
 * donna_terminal_get_active_tab:
 * @terminal: A #DonnaTerminal
 *
 * Get the tab ID of the active tab.
 *
 * This is a fixed ID that can be used to refer to the tab even after reordering
 * tabs; However, once the tab has been removed the ID can then be
 * re-used/assigned to a new tab.
 *
 * Returns: The ID of the active tab, or -1 if there's no (active) tab
 */
guint
donna_terminal_get_active_tab (DonnaTerminal      *terminal)
{
    GtkNotebook *nb = (GtkNotebook *) terminal;
    gint page;
    struct term *term;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), (guint) -1);

    page = gtk_notebook_get_current_page (nb);
    if (page == -1)
        return (guint) -1;
    term = g_object_get_data ((GObject *) gtk_notebook_get_nth_page (nb, page),
            "_terminal");
    return term->id;
}

/**
 * donna_terminal_get_active_page:
 * @terminal: A #DonnaTerminal
 *
 * Get the page number of the active tab.
 *
 * Note that page numbers can change as pages are added/removed/reordered. If
 * you need a persistent idnetifier for a tab, use tab IDs (e.g. use
 * donna_terminal_get_active_tab() instead)
 *
 * Returns: The current page number of the active tab, or -1 if there's no
 * (active) tab
 */
gint
donna_terminal_get_active_page (DonnaTerminal      *terminal)
{
    GtkNotebook *nb = (GtkNotebook *) terminal;
    gint page;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), -1);

    page = gtk_notebook_get_current_page (nb);
    return (page == -1) ? -1 : page + 1;
}

/**
 * donna_terminal_set_active_tab:
 * @terminal: A #DonnaTerminal
 * @id: The tab ID of the tab to set active
 * @no_focus: Set to %TRUE not to focus the embedded terminal
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Sets tab @id to be the active tab. Unless @no_focus was %TRUE the focus will
 * be sent to the embedded terminal.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_terminal_set_active_tab (DonnaTerminal      *terminal,
                               guint               id,
                               gboolean            no_focus,
                               GError            **error)
{
    gint page;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), FALSE);

    page = donna_terminal_get_page (terminal, id, error);
    if (page == -1)
        return FALSE;

    DONNA_DEBUG (TERMINAL, terminal->priv->name,
            g_debug ("Terminal '%s': Setting active tab %u (page %d)",
                terminal->priv->name, id, page));
    g_object_set (terminal, "page", page - 1, NULL);

    if (!no_focus)
    {
        GtkWidget *w;

        w = gtk_notebook_get_nth_page ((GtkNotebook *) terminal, page - 1);
        gtk_widget_set_can_focus (w, TRUE);
        gtk_widget_grab_focus (w);
    }

    return TRUE;
}

/**
 * donna_terminal_set_active_page:
 * @terminal: A #DonnaTerminal
 * @page: The page number; -1 for last page
 * @no_focus: Set to %TRUE not to focus the embedded terminal
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Sets page @page to be the active one. Unless @no_focus was %TRUE the focus
 * will be sent to the embedded terminal.
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_terminal_set_active_page (DonnaTerminal      *terminal,
                                gint                page,
                                gboolean            no_focus,
                                GError            **error)
{
    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), FALSE);

    if (page == 0 || page < -1)
    {
        g_set_error (error, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Terminal '%s': Cannot get id of page %d; "
                "page numbers start at 1 (or -1 for last one)",
                terminal->priv->name, page);
        return FALSE;
    }

    DONNA_DEBUG (TERMINAL, terminal->priv->name,
            GtkWidget *w;
            struct term *term;

            w = gtk_notebook_get_nth_page ((GtkNotebook *) terminal,
                (page > 0) ? page - 1 : page);
            term = g_object_get_data ((GObject *) w, "_terminal");
            g_debug ("Terminal '%s': Setting active page %d (tab %u)",
                terminal->priv->name, page, term->id));

    g_object_set (terminal, "page", (page > 0) ? page - 1 : page, NULL);

    if (!no_focus)
    {
        GtkWidget *w;

        w = gtk_notebook_get_nth_page ((GtkNotebook *) terminal,
                (page > 0) ? page - 1 : page);
        gtk_widget_set_can_focus (w, TRUE);
        gtk_widget_grab_focus (w);
    }

    return TRUE;
}

/**
 * donna_terminal_get_tab:
 * @terminal: A #DonnaTerminal
 * @page: Page number, or -1 for the last page
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Get the tab ID from the page number.
 *
 * A page number is simply obtained by counting tabs on the terminal, in their
 * current order (starting at 1). So the same tab might change page number when
 * tabs are added/removed/reordered.
 * The ID on the other hand remains fixed for as long as the tab exists.
 *
 * Returns: The tab ID, or -1 on error
 */
guint
donna_terminal_get_tab (DonnaTerminal      *terminal,
                        gint                page,
                        GError            **error)
{
    GtkWidget *w;
    struct term *term;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), (guint) -1);

    if (page == 0 || page < -1)
    {
        g_set_error (error, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Terminal '%s': Cannot get id of page %d; "
                "page numbers start at 1 (or -1 for last one)",
                terminal->priv->name, page);
        return (guint) -1;
    }

    w = gtk_notebook_get_nth_page ((GtkNotebook *) terminal,
            (page > 0) ? page - 1 : page);
    if (!w)
    {
        g_set_error (error, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Terminal '%s': Page %d not found",
                terminal->priv->name, page);
        return (guint) -1;
    }

    term = g_object_get_data ((GObject *) w, "_terminal");
    return term->id;
}

/**
 * donna_terminal_get_page:
 * @terminal: A #DonnaTerminal
 * @id: ID of a tab
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Get the current page number of tab @id
 *
 * Note that page numbers start at 1, unlike in #GtkNotebook where they start at
 * 0.
 *
 * Returns: Page number of tab @id, or -1
 */
gint
donna_terminal_get_page (DonnaTerminal      *terminal,
                         guint               id,
                         GError            **error)
{
    GList *list, *l;
    gint page = -1;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), FALSE);

    list = gtk_container_get_children ((GtkContainer *) terminal);
    for (l = list; l; l = l->next)
    {
        struct term *term = g_object_get_data (l->data, "_terminal");
        if (term->id == id)
        {
            page = gtk_notebook_page_num ((GtkNotebook *) terminal,
                    (GtkWidget *) term->socket);
            break;
        }
    }
    g_list_free (list);

    if (page == -1)
        g_set_error (error, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Terminal '%s': Cannot get page number, no tab with id %u",
                terminal->priv->name, id);

    return page + 1;
}

static GdkFilterReturn
term_filter (GdkXEvent *gdk_xevent, GdkEvent *event, struct term *term)
{
#ifdef DONNA_DEBUG_ENABLED
    DonnaTerminalPrivate *priv = term->terminal->priv;
#endif
    XEvent *xevent = (XEvent *) gdk_xevent;
    Window window;

    window = GDK_WINDOW_XID (gtk_socket_get_plug_window (term->socket));

    if (xevent->type == PropertyNotify && xevent->xproperty.window == window)
    {
        GdkDisplay *display;

        display = gtk_widget_get_display ((GtkWidget *) term->socket);

        if (xevent->xproperty.atom == gdk_x11_get_xatom_by_name_for_display (
                    display, "_NET_WM_NAME"))
        {
            Status st;
            Atom type;
            int format;
            unsigned long nb, bytes_after;
            unsigned char *data;
            Atom atom_utf8;

            atom_utf8 = gdk_x11_get_xatom_by_name_for_display (display, "UTF8_STRING");
            DONNA_DEBUG (TERMINAL, priv->name,
                    g_debug2 ("Terminal '%s': Tab %u: PropertyNotify for _NET_WM_NAME",
                        priv->name, term->id));

            gdk_error_trap_push ();
            st = XGetWindowProperty (GDK_DISPLAY_XDISPLAY (display), window,
                    xevent->xproperty.atom,
                    0, G_MAXLONG, False,
                    atom_utf8, &type, &format,
                    &nb, &bytes_after, &data);
            gdk_error_trap_pop_ignored ();

            if (st == Success && type == atom_utf8 && format == 8 && nb > 0)
            {
                term->has_net_name = TRUE;
                DONNA_DEBUG (TERMINAL, priv->name,
                        g_debug2 ("Terminal '%s': Tab %u: _NET_WM_NAME='%s'",
                            priv->name, term->id, data));
                gtk_notebook_set_tab_label_text ((GtkNotebook *) term->terminal,
                        (GtkWidget *) term->socket, (const gchar *) data);
                gtk_notebook_set_menu_label_text ((GtkNotebook *) term->terminal,
                        (GtkWidget *) term->socket, (const gchar *) data);
            }
            XFree (data);
        }

        if (!term->has_net_name && xevent->xproperty.atom
                == gdk_x11_get_xatom_by_name_for_display (display, "WM_NAME"))
        {
            Status st;
            XTextProperty tp;

            DONNA_DEBUG (TERMINAL, priv->name,
                    g_debug2 ("Terminal '%s': Tab %u: PropertyNotify for WM_NAME",
                        priv->name, term->id));

            gdk_error_trap_push ();
            st = XGetTextProperty (GDK_DISPLAY_XDISPLAY (display), window,
                    &tp, xevent->xproperty.atom);
            gdk_error_trap_pop_ignored ();

            if (st != 0)
            {
                gint n;
                gchar **list;

                n = gdk_text_property_to_utf8_list_for_display (display,
                        gdk_x11_xatom_to_atom (tp.encoding),
                        tp.format, tp.value, (gint) tp.nitems, &list);
                if (n > 0)
                {
                    DONNA_DEBUG (TERMINAL, priv->name,
                            g_debug2 ("Terminal '%s': Tab %u: WM_NAME='%s'",
                                priv->name, term->id, *list));
                    gtk_notebook_set_tab_label_text ((GtkNotebook *) term->terminal,
                            (GtkWidget *) term->socket, *list);
                    gtk_notebook_set_menu_label_text ((GtkNotebook *) term->terminal,
                            (GtkWidget *) term->socket, *list);
                    g_strfreev (list);
                }

                if (tp.value && tp.nitems > 0)
                    XFree (tp.value);
            }
        }
    }

    return GDK_FILTER_CONTINUE;
}

static void
plugged (GtkSocket *socket, struct term *term)
{
#ifdef DONNA_DEBUG_ENABLED
    DonnaTerminalPrivate *priv = term->terminal->priv;
#endif
    GdkWindow *win;

    win = gtk_socket_get_plug_window (socket);
    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug2 ("Terminal '%s': Tab %u: Socket plugged (%p)",
                priv->name, term->id, win));
    if (G_UNLIKELY (!win))
        return;

    term->is_plugged = TRUE;
    gdk_window_add_filter (win, (GdkFilterFunc) term_filter, term);

    if (term->focus_on_plug)
    {
        gtk_widget_set_can_focus ((GtkWidget *) socket, TRUE);
        gtk_widget_grab_focus ((GtkWidget *) socket);
    }
}

static gboolean
unplugged (GtkSocket *socket, struct term *term)
{
#ifdef DONNA_DEBUG_ENABLED
    DonnaTerminalPrivate *priv = term->terminal->priv;
#endif

    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug ("Terminal '%s': Tab %u: Socket unplugged",
                priv->name, term->id));

    /* destroy the widget */
    return FALSE;
}

static gboolean
button_pressed (GtkSocket *socket, GdkEventButton *event, struct term *term)
{
    if (event->button == 1 && !gtk_widget_has_focus ((GtkWidget *) socket))
    {
        gtk_widget_set_can_focus ((GtkWidget *) socket, TRUE);
        gtk_widget_grab_focus ((GtkWidget *) socket);
        if (cfg_get_focusing_click ((DonnaTerminal *) term->terminal, NULL))
            return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

/* from task.c */
const gchar * state_name (DonnaTaskState state);

static void
task_cb (DonnaTask *task, gboolean timedout, struct term *term)
{
    DonnaTerminalPrivate *priv = term->terminal->priv;

    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug ("Terminal '%s': Tab %u: Task POST_RUN (%s)",
                priv->name, term->id, state_name (donna_task_get_state (task))));

    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (priv->app, donna_task_get_error (task),
                "Terminal '%s': Task process failed for tab %u (page %d)",
                priv->name,
                term->id,
                gtk_notebook_page_num ((GtkNotebook *) term->terminal,
                    (GtkWidget *) term->socket) + 1);

    /* were never plugged */
    if (!term->is_plugged)
    {
        GError *err = NULL;

        if (!donna_terminal_remove_tab (term->terminal, term->id, &err))
        {
            donna_app_show_error (priv->app, err,
                    "Terminal '%s': Failed to remove tab %d after failed task",
                    priv->name, term->id);
            g_clear_error (&err);
        }

        gtk_widget_destroy ((GtkWidget *) term->socket);
    }
}

static void
terminal_custom (const gchar         c,
                 gchar              *extra,
                 DonnaContextOptions options,
                 GString            *str,
                 Window             *wid)
{
    g_string_append_printf (str, "%lu", *wid);
}

static gboolean
terminal_conv (const gchar       c,
               gchar            *extra,
               DonnaArgType     *type,
               gpointer         *ptr,
               GDestroyNotify   *destroy,
               Window           *wid)
{
    if (G_UNLIKELY (c != 'w'))
        return FALSE;
    *type = _DONNA_ARG_TYPE_CUSTOM;
    *ptr = terminal_custom;
    return TRUE;
}

/**
 * donna_terminal_add_tab:
 * @terminal: A #DonnaTerminal
 * @cmdline: The command line to execute in a new emebedded terminal
 * @term_cmdline: (allow-none): The command line for the terminal emulator to
 * embed
 * @workdir: (allow-none): Working directory for the terminal, or %NULL to use
 * the current directory (i.e. what donna_app_get_current_dirname() returns)
 * @add_tab: How to handle the addition of the new tab
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Adds a new tab/embedded terminal into @terminal
 *
 * @term_cmdline can be %NULL, in which case option
 * "terminals/&lt;TERMINAL&gt;/cmdline" will be used. It can also be a string
 * starting with a colon, to load an alternative option. For example if
 * @term_cmdline is ":foobar" option "terminals/&lt;TERMINAL&gt;/cmdline_foobar"
 * would be used instead.  It can also be the actual command line to use.
 *
 * This command line must contain \%w which will be parsed to the window id to
 * be used by the terminal emulator, as per XEMBED protocol. It will be used as
 * prefix, adding to it @cmdline as the command line to run inside the terminal
 * (i.e. @term_cmdline probably ends with -e or -c)
 *
 * @add_tab specifies if the newly created tab should be made active, and
 * focused, or not. Note that when @DONNA_TERMINAL_FOCUS is used, it won't
 * happen until the terminal emulator has actually been embedded.
 *
 * Returns: The ID of the newly created tab, or -1 on error
 */
guint
donna_terminal_add_tab (DonnaTerminal      *terminal,
                        const gchar        *cmdline,
                        const gchar        *term_cmdline,
                        const gchar        *workdir,
                        DonnaTerminalAddTab add_tab,
                        GError            **error)
{
    DonnaTerminalPrivate *priv;
    DonnaConfig *config;
    GtkNotebook *nb = (GtkNotebook *) terminal;
    struct term *term;
    GtkSocket *socket;
    Window wid;
    DonnaContext context = { "w", FALSE, (conv_flag_fn) terminal_conv, &wid };
    gint num;
    GString *str = NULL;
    gchar *cl;
    DonnaTaskProcess *tp;
    GPtrArray *arr;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), (guint) -1);
    g_return_val_if_fail (cmdline != NULL, (guint) -1);
    priv = terminal->priv;
    config = donna_app_peek_config (priv->app);

    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug2 ("Terminal '%s': Adding tab for '%s' using '%s'",
                priv->name, cmdline, term_cmdline));

    if (!term_cmdline)
        cl = cfg_get_cmdline (terminal, config);
    else if (*term_cmdline == ':')
        cl = cfg_get_cmdline_extra (terminal, config, term_cmdline + 1);
    else
        cl = (gchar *) term_cmdline;
    if (!cl)
    {
        g_prefix_error (error, "Terminal '%s': Failed to get command line "
                "to launch embedded terminal: ",
                priv->name);
        return (guint) -1;
    }

    socket = (GtkSocket *) donna_embedder_new (cfg_get_catch_events (terminal, config));
    gtk_notebook_append_page (nb, (GtkWidget *) socket, NULL);
    gtk_notebook_set_tab_reorderable (nb, (GtkWidget *) socket, TRUE);
    gtk_widget_show ((GtkWidget *) socket);

    wid = gtk_socket_get_id (socket);
    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug2 ("Terminal '%s': Created socket; window %lu",
                priv->name, wid));
    donna_context_parse (&context, 0, priv->app, cl, &str, NULL);
    if (cl != term_cmdline)
        g_free (cl);
    if (!str)
    {
        gtk_notebook_remove_page (nb, -1);

        g_set_error (error, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_INVALID_CMDLINE,
                "Terminal '%s': Invalid terminal command line, did you forget to use %%w?",
                priv->name);
        return (guint) -1;
    }

    g_string_append_c (str, ' ');
    g_string_append (str, cmdline);
    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug2 ("Terminal '%s': Creating task: %s", priv->name, str->str));
    tp = (DonnaTaskProcess *) donna_task_process_new (workdir, str->str, TRUE,
            NULL, NULL, NULL);
    g_string_free (str, TRUE);
    if (G_UNLIKELY (!tp))
    {
        gtk_notebook_remove_page (nb, -1);
        g_object_unref (g_object_ref_sink (tp));

        g_set_error (error, DONNA_TERMINAL_ERROR, DONNA_TERMINAL_ERROR_OTHER,
                "Terminal '%s': Failed to create task process",
                priv->name);
        return (guint) -1;
    }

    if (!workdir && !donna_task_process_set_workdir_to_curdir (tp, priv->app))
    {
        gtk_notebook_remove_page (nb, -1);
        g_object_unref (g_object_ref_sink (tp));

        g_set_error (error, DONNA_TERMINAL_ERROR, DONNA_TERMINAL_ERROR_OTHER,
                "Terminal '%s': Failed to set workdir on task process",
                priv->name);
        return (guint) -1;
    }
    arr = g_ptr_array_new ();
    donna_task_set_devices ((DonnaTask *) tp, arr);
    g_ptr_array_unref (arr);
    donna_task_process_set_ui_msg (tp);
    donna_task_process_set_default_closer (tp);
    donna_task_process_import_environ (tp, priv->app);
    donna_task_process_setenv (tp, "DONNATELLA_EMBEDDED", "1", TRUE);

    num = gtk_notebook_get_n_pages (nb);
    if (num == 1)
        /* make terminal/notebook visible */
        gtk_widget_show ((GtkWidget *) nb);
    else if (num == 2)
        /* show tabs */
        gtk_notebook_set_show_tabs (nb, TRUE);

    /* default title */
    gtk_notebook_set_tab_label_text (nb, (GtkWidget *) socket, cmdline);
    gtk_notebook_set_menu_label_text (nb, (GtkWidget *) socket, cmdline);

    term = g_slice_new0 (struct term);
    term->terminal = terminal;
    term->id = ++priv->last_id;
    term->socket = g_object_ref (socket);
    term->task = (DonnaTask *) g_object_ref (tp);
    g_object_set_data_full ((GObject *) socket, "_terminal",
            term, (GDestroyNotify) free_term);
    donna_task_set_callback (term->task, (task_callback_fn) task_cb, term, NULL);

    g_signal_connect (socket, "plug-added", (GCallback) plugged, term);
    g_signal_connect (socket, "plug-removed", (GCallback) unplugged, term);
    g_signal_connect (socket, "button-press-event", (GCallback) button_pressed, term);

    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug ("Terminal '%s': Added tab %u (window %lu) for '%s' using '%s'",
                priv->name, term->id, wid, cmdline, term_cmdline));

    donna_app_run_task (priv->app, term->task);

    if (add_tab)
    {
        donna_terminal_set_active_tab (terminal, term->id, TRUE, NULL);
        if (add_tab == DONNA_TERMINAL_FOCUS)
            term->focus_on_plug = TRUE;
    }

    return term->id;
}

/**
 * donna_terminal_remove_tab:
 * @terminal: A #DonnaTerminal
 * @id: The tab ID
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Removes tab @id from @terminal
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_terminal_remove_tab (DonnaTerminal      *terminal,
                           guint               id,
                           GError            **error)
{
    gint page;

    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), FALSE);
    g_return_val_if_fail (id > 0, FALSE);

    page = donna_terminal_get_page (terminal, id, error);
    if (page == -1)
        return FALSE;

    gtk_notebook_remove_page ((GtkNotebook *) terminal, page - 1);
    return TRUE;
}

/**
 * donna_terminal_remove_page:
 * @terminal: A #DonnaTerminal
 * @page: Page number; or -1 for last one
 * @error: (allow-none): Return location of a #GError, or %NULL
 *
 * Removes page @page from @terminal
 *
 * Returns: %TRUE on success, else %FALSE
 */
gboolean
donna_terminal_remove_page (DonnaTerminal      *terminal,
                            gint                page,
                            GError            **error)
{
    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), FALSE);

    if (page == 0 || page < -1)
    {
        g_set_error (error, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Terminal '%s': Cannot remove page %d; "
                "page numbers start at 1 (or -1 for last one)",
                terminal->priv->name, page);
        return FALSE;
    }

    gtk_notebook_remove_page ((GtkNotebook *) terminal,
            (page > 0) ? page - 1 : page);
    return TRUE;
}

/**
 * donna_terminal_get_name:
 * @terminal: A #DonnaTerminal
 *
 * Return the name of @terminal
 *
 * Return: The name of @terminal
 */
const gchar *
donna_terminal_get_name (DonnaTerminal      *terminal)
{
    g_return_val_if_fail (DONNA_IS_TERMINAL (terminal), NULL);
    return terminal->priv->name;
}

enum
{
    OPTION_ALWAYS_SHOW_TABS,
    OPTION_CATCH_EVENTS
};

struct config_refresh
{
    DonnaTerminal *terminal;
    guint option;
};

static gboolean
config_refresh_real (struct config_refresh *cr)
{
    if (cr->option == OPTION_ALWAYS_SHOW_TABS)
    {
        if (gtk_notebook_get_n_pages ((GtkNotebook *) cr->terminal) <= 1)
            gtk_notebook_set_show_tabs ((GtkNotebook *) cr->terminal,
                    cfg_get_always_show_tabs (cr->terminal, NULL));
    }
    else /* OPTION_CATCH_EVENTS */
    {
        gboolean catch_events;
        gint i;

        catch_events = cfg_get_catch_events (cr->terminal, NULL);

        i = gtk_notebook_get_n_pages ((GtkNotebook *) cr->terminal);
        for (--i ; i >= 0; --i)
        {
            GtkWidget *w;

            w = gtk_notebook_get_nth_page ((GtkNotebook *) cr->terminal, i);
            g_object_set (w, "catch-events", catch_events, NULL);
        }
    }

    g_free (cr);
    return G_SOURCE_REMOVE;
}

static void
config_refresh (DonnaConfig *config, const gchar *name, DonnaTerminal *terminal)
{
    DonnaTerminalPrivate *priv = terminal->priv;
    gsize len;

    /* might not be in thread UI, but our priv->name isn't going anywhere */

    if (streqn (name, "terminals/", 10))
    {
        len = strlen (priv->name);
        if (!streqn (name + 10, priv->name, len) || name[10 + len] != '/')
            return;
        len += 11;
    }
    else if (streqn (name, "defaults/terminals/", 19))
        len = 19;
    else
        return;

    if (streq (name + len, "always_show_tabs")
            || streq (name + len, "catch_events"))
    {
        struct config_refresh *cr;

        cr = g_new (struct config_refresh, 1);
        cr->terminal = terminal;
        if (streq (name + len, "catch_events"))
            cr->option = OPTION_CATCH_EVENTS;
        else
            cr->option = OPTION_ALWAYS_SHOW_TABS;
        g_main_context_invoke (NULL, (GSourceFunc) config_refresh_real, cr);
    }
}

/**
 * donna_terminal_new:
 * @app: The #DonnaApp
 * @name: The name of the new terminal to create
 *
 * Creates a new #DonnaTerminal for terminal @name
 *
 * Returns: (transfer floating): The newly created #DonnaTerminal
 */
GtkWidget *
donna_terminal_new (DonnaApp           *app,
                    const gchar        *name)

{
    DonnaTerminalPrivate *priv;
    DonnaConfig *config;
    GtkNotebook *nb;

    nb = (GtkNotebook *) g_object_new (DONNA_TYPE_TERMINAL, NULL);
    priv = ((DonnaTerminal *) nb)->priv;
    priv->app = app;
    priv->name = g_strdup (name);

    config = donna_app_peek_config (app);

    /* don't show anything if there's no tab */
    gtk_widget_set_no_show_all ((GtkWidget *) nb, TRUE);
    gtk_notebook_popup_enable (nb);
    gtk_notebook_set_show_tabs (nb,
            cfg_get_always_show_tabs ((DonnaTerminal *) nb, config));

    g_signal_connect (config, "option-set", (GCallback) config_refresh, nb);
    g_signal_connect (config, "option-deleted", (GCallback) config_refresh, nb);

    DONNA_DEBUG (TERMINAL, priv->name,
            g_debug ("Loaded terminal '%s'", priv->name));

    return (GtkWidget *) nb;
}
