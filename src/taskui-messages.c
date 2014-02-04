/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * taskui-messages.c
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
#include "taskui-messages.h"


struct _DonnaTaskUiMessagesPrivate
{
    gchar           *title;
    GtkWidget       *window;
    GtkTextBuffer   *buffer;
};

static void             tui_messages_finalize       (GObject            *object);

/* TaskUi */
static void             tui_messages_take_title     (DonnaTaskUi        *tui,
                                                     gchar              *title);
static void             tui_messages_show           (DonnaTaskUi        *tui);

static void
tui_messages_taskui_init (DonnaTaskUiInterface *interface)
{
    interface->take_title   = tui_messages_take_title;
    interface->show         = tui_messages_show;
}

G_DEFINE_TYPE_WITH_CODE (DonnaTaskUiMessages, donna_task_ui_messages,
        G_TYPE_INITIALLY_UNOWNED,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_TASKUI, tui_messages_taskui_init)
        )

static void
donna_task_ui_messages_class_init (DonnaTaskUiMessagesClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->finalize = tui_messages_finalize;

    g_type_class_add_private (klass, sizeof (DonnaTaskUiMessagesPrivate));
}

static void
donna_task_ui_messages_init (DonnaTaskUiMessages *tuimsg)
{
    DonnaTaskUiMessagesPrivate *priv;

    priv = tuimsg->priv = G_TYPE_INSTANCE_GET_PRIVATE (tuimsg,
            DONNA_TYPE_TASKUI_MESSAGES, DonnaTaskUiMessagesPrivate);

    priv->buffer = gtk_text_buffer_new (NULL);
    gtk_text_buffer_create_tag (priv->buffer, "timestamp",
            "foreground",   "gray",
            NULL);
    gtk_text_buffer_create_tag (priv->buffer, "info", NULL);
    gtk_text_buffer_create_tag (priv->buffer, "error",
            "foreground",   "red",
            NULL);
}

static void
tui_messages_finalize (GObject *object)
{
    DonnaTaskUiMessagesPrivate *priv;

    priv = DONNA_TASKUI_MESSAGES (object)->priv;
    g_free (priv->title);
    if (priv->window)
        gtk_widget_destroy (priv->window);
    g_object_unref (priv->buffer);

    /* chain up */
    G_OBJECT_CLASS (donna_task_ui_messages_parent_class)->finalize (object);
}

static gboolean
refresh_window_title (DonnaTaskUiMessages *tmsg)
{
    DonnaTaskUiMessagesPrivate *priv = tmsg->priv;
    gtk_window_set_title ((GtkWindow *) priv->window, priv->title);
    g_object_unref (tmsg);
    return G_SOURCE_REMOVE;
}

static void
tui_messages_take_title (DonnaTaskUi        *tui,
                         gchar              *title)
{
    DonnaTaskUiMessagesPrivate *priv = ((DonnaTaskUiMessages *) tui)->priv;
    gchar *old;

    old = priv->title;
    priv->title = title;
    g_free (old);
    if (priv->window)
        g_idle_add ((GSourceFunc) refresh_window_title, g_object_ref (tui));
}

static void
toggle_timestamps (GtkToggleToolButton *btn, DonnaTaskUiMessages *tm)
{
    GtkTextTagTable *table;
    GtkTextTag *tag;
    gboolean active;

    table = gtk_text_buffer_get_tag_table (tm->priv->buffer);
    tag = gtk_text_tag_table_lookup (table, "timestamp");

    g_object_get (btn, "active", &active, NULL);
    g_object_set (tag, "invisible", !active, NULL);
}

static void
tui_messages_show (DonnaTaskUi        *tui)
{
    DonnaTaskUiMessagesPrivate *priv = ((DonnaTaskUiMessages *) tui)->priv;

    if (!priv->window)
    {
        GtkWidget *win;
        GtkBox *box;
        GtkToolbar *tb;
        GtkToolItem *ti;
        GtkWidget *sw;
        GtkWidget *w;

        win = priv->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        if (priv->title)
            gtk_window_set_title ((GtkWindow *) win, priv->title);
        g_signal_connect (win, "delete-event",
                (GCallback) gtk_widget_hide_on_delete, NULL);
        gtk_window_set_default_size ((GtkWindow *) win, 420, 230);

        box = (GtkBox *) gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add ((GtkContainer *) win, (GtkWidget *) box);

        tb = (GtkToolbar *) gtk_toolbar_new ();
        gtk_box_pack_start (box, (GtkWidget *) tb, FALSE, FALSE, 0);

        ti = gtk_toggle_tool_button_new ();
        g_object_set (ti, "label", "Show timestamps", "active", TRUE, NULL);
        g_signal_connect (ti, "toggled", (GCallback) toggle_timestamps, tui);
        gtk_toolbar_insert (tb, ti, -1);

        sw = gtk_scrolled_window_new (NULL, NULL);
        gtk_box_pack_start (box, sw, TRUE, TRUE, 0);

        w = gtk_text_view_new_with_buffer (priv->buffer);
        g_object_set (w, "cursor-visible", FALSE, "editable", FALSE, NULL);
        gtk_container_add ((GtkContainer *) sw, w);

        gtk_widget_show_all (priv->window);
    }
    else
        gtk_window_present ((GtkWindow *) priv->window);
}

struct message
{
    DonnaTaskUiMessages *tmsg;
    GLogLevelFlags level;
    gchar *message;
    gboolean is_heap;
};

static gboolean
real_messages_add (struct message *m)
{
    DonnaTaskUiMessagesPrivate *priv = m->tmsg->priv;
    GtkTextIter iter;
    GDateTime *dt;
    gchar *s;

    gtk_text_buffer_get_end_iter (priv->buffer, &iter);

    dt = g_date_time_new_now_local ();
    s = g_date_time_format (dt, "[%H:%M:%S] ");
    gtk_text_buffer_insert_with_tags_by_name (priv->buffer, &iter, s, -1,
            "timestamp", NULL);
    g_free (s);

    gtk_text_buffer_insert_with_tags_by_name (priv->buffer, &iter, m->message, -1,
            (m->level == G_LOG_LEVEL_ERROR) ? "error" : "info", NULL);
    gtk_text_buffer_insert (priv->buffer, &iter, "\n", -1);

    if (m->is_heap)
    {
        g_object_unref (m->tmsg);
        g_free (m->message);
        g_free (m);
    }

    return G_SOURCE_REMOVE;
}

void
donna_task_ui_messages_add (DonnaTaskUiMessages    *tui,
                            GLogLevelFlags          level,
                            const gchar            *message)
{
    g_return_if_fail (DONNA_IS_TASKUI_MESSAGES (tui));
    g_return_if_fail (level == G_LOG_LEVEL_INFO || level == G_LOG_LEVEL_ERROR);

    /* if the window doesn't exists, let's assume it won't be created while we
     * add the message (or that if it does it won't cause any issue) */
    if (!tui->priv->window)
    {
        struct message m = { tui, level, (gchar *) message, FALSE };
        real_messages_add (&m);
    }
    else
    {
        struct message *m;
        m = g_new (struct message, 1);
        m->tmsg = g_object_ref (tui);
        m->level = level;
        m->message = g_strdup (message);
        m->is_heap = TRUE;
        g_idle_add ((GSourceFunc) real_messages_add, m);
    }
}
