
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

G_DEFINE_TYPE_WITH_CODE (DonnaTaskUiMessages, donna_task_ui_messages,
        G_TYPE_INITIALLY_UNOWNED,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_TASKUI, tui_messages_taskui_init)
        )

static void
tui_messages_finalize (GObject *object)
{
    DonnaTaskUiMessagesPrivate *priv;

    priv = DONNA_TASKUI_MESSAGES (object)->priv;
    g_free (priv->title);
    if (priv->window)
        gtk_widget_destroy (priv->window);

    /* chain up */
    G_OBJECT_CLASS (donna_task_ui_messages_parent_class)->finalize (object);
}

static void
tui_messages_take_title (DonnaTaskUi        *tui,
                         gchar              *title)
{
    DonnaTaskUiMessagesPrivate *priv = ((DonnaTaskUiMessages *) tui)->priv;

    g_free (priv->title);
    priv->title = title;
    if (priv->window)
        gtk_window_set_title ((GtkWindow *) priv->window, priv->title);
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

void
donna_task_ui_messages_add (DonnaTaskUiMessages    *tui,
                            GLogLevelFlags          level,
                            const gchar            *message)
{
    DonnaTaskUiMessagesPrivate *priv;
    GtkTextIter iter;
    GDateTime *dt;
    gchar *s;

    g_return_if_fail (DONNA_IS_TASKUI_MESSAGES (tui));
    g_return_if_fail (level == G_LOG_LEVEL_INFO || level == G_LOG_LEVEL_ERROR);
    priv = tui->priv;

    gtk_text_buffer_get_end_iter (priv->buffer, &iter);

    dt = g_date_time_new_now_local ();
    s = g_date_time_format (dt, "[%H:%M:%S] ");
    gtk_text_buffer_insert_with_tags_by_name (priv->buffer, &iter, s, -1,
            "timestamp", NULL);
    g_free (s);

    gtk_text_buffer_insert_with_tags_by_name (priv->buffer, &iter, message, -1,
            (level == G_LOG_LEVEL_ERROR) ? "error" : "info", NULL);
    gtk_text_buffer_insert (priv->buffer, &iter, "\n", -1);
}
