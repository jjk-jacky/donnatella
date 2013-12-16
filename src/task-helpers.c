
#include "config.h"

#include <gtk/gtk.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include "task-helpers.h"
#include "common.h"

struct _DonnaTaskHelper
{
    GMutex mutex;
    gint fd;
    gint has_destroy;

    task_helper_ui show_ui;
    task_helper_ui destroy_ui;
    gpointer data;

    DonnaTaskHelperRc rc;
};


/* base functions for helpers */

void
donna_task_helper_done (DonnaTaskHelper    *th)
{
    guint64 one;

    g_mutex_lock (&th->mutex);
    if (th->has_destroy == 0)
        /* unblock thread if there's no destroy_ui() pending */
        write (th->fd, &one, sizeof (one));
    else
        /* flag that UI is done, to abort the pending call to destroy_ui() */
        ++th->has_destroy;
    g_mutex_unlock (&th->mutex);
}

static gboolean
do_show_ui (DonnaTaskHelper *th)
{
    th->show_ui (th, th->data);
    return FALSE;
}

static gboolean
do_destroy_ui (DonnaTaskHelper *th)
{
    g_mutex_lock (&th->mutex);

    if (th->has_destroy == 1)
    {
        th->has_destroy = 0;
        g_mutex_unlock (&th->mutex);
        th->destroy_ui (th, th->data);
    }
    else
    {
        guint64 one;

        /* user actually already, so UI is done: just unblock thread */
        th->rc = DONNA_TASK_HELPER_RC_SUCCESS;
        write (th->fd, &one, sizeof (one));
        g_mutex_unlock (&th->mutex);
    }

    return FALSE;
}

DonnaTaskHelperRc
donna_task_helper (DonnaTask          *task,
                   task_helper_ui      show_ui,
                   task_helper_ui      destroy_ui,
                   gpointer            data)
{
    DonnaTaskHelper th;
    fd_set fds;
    gint fd_task;

    g_return_val_if_fail (DONNA_IS_TASK (task), DONNA_TASK_HELPER_RC_ERROR);
    g_return_val_if_fail (show_ui != NULL, DONNA_TASK_HELPER_RC_ERROR);
    g_return_val_if_fail (destroy_ui != NULL, DONNA_TASK_HELPER_RC_ERROR);

    /* create our eventfd */
    th.fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (th.fd == -1)
        return DONNA_TASK_HELPER_RC_ERROR;

    /* init th */
    g_mutex_init (&th.mutex);
    th.has_destroy = 0;
    th.show_ui = show_ui;
    th.destroy_ui = destroy_ui;
    th.data = data;
    th.rc = DONNA_TASK_HELPER_RC_SUCCESS;

    /* get the task-s fd, in case it gets paused/cancelled */
    fd_task = donna_task_get_fd (task);

    g_idle_add ((GSourceFunc) do_show_ui, &th);

    for (;;)
    {
        gint ret;

        FD_ZERO (&fds);
        FD_SET (th.fd, &fds);
        if (fd_task >= 0)
            FD_SET (fd_task, &fds);

        /* block thread until user "answered" (helper_done() was called,
         * unblocking our fd) or the task gets paused/cancelled */
        ret = select (MAX (th.fd, fd_task) + 1, &fds, NULL, NULL, 0);
        if (ret < 0)
        {
            gint _errno = errno;

            if (errno == EINTR)
                continue;

            g_warning ("TaskHelper: Call to select() failed: %s",
                    g_strerror (_errno));
            continue;
        }

        g_mutex_lock (&th.mutex);

        /* normal ending, i.e. user did "answer" -- meaning the UI was
         * destroyed, nothing more to do but free stuff & return */
        if (FD_ISSET (th.fd, &fds))
        {
            g_mutex_unlock (&th.mutex);
            break;
        }

        /* task was paused/cancelled */
        if (FD_ISSET (fd_task, &fds))
        {
            th.rc = DONNA_TASK_HELPER_RC_CANCELLING;
            /* flag that there is (will be) a pending call to destroy_ui. This
             * is used to handle race condition where the user answers while
             * we're doing this */
            th.has_destroy = 1;
            /* disable this for now */
            fd_task = -1;
            /* install the call to destroy the ui */
            g_idle_add ((GSourceFunc) do_destroy_ui, &th);
            /* and block again on our fd (waiting for UI to be destroyed) */
        }

        g_mutex_unlock (&th.mutex);
    }

    close (th.fd);
    g_mutex_clear (&th.mutex);
    return th.rc;
}


/* actual helpers */

struct ask
{
    DonnaTaskHelper *th;
    GtkWidget       *widget;
    const gchar     *question;
    const gchar     *details;
    gboolean         details_markup;
    gint             btn_default;
    GPtrArray       *buttons;
    gint             btn_pressed;
};

static void
ask_response (GtkWidget *w, gint r, struct ask *ask)
{
    ask->btn_pressed = r;
    gtk_widget_destroy (w);
    donna_task_helper_done (ask->th);
}

static void
ask_show_ui (DonnaTaskHelper    *th,
             struct ask         *ask)
{
    GtkWidget *w;
    guint i;
    gint r;

    ask->th = th;

    w = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "%s", ask->question);
    if (ask->details_markup)
        gtk_message_dialog_format_secondary_markup ((GtkMessageDialog *) w,
                "%s", ask->details);
    else
        gtk_message_dialog_format_secondary_text ((GtkMessageDialog *) w,
                "%s", ask->details);

    for (i = 0, r = 1; i < ask->buttons->len; i += 2, ++r)
    {
        GtkButton *btn;

        btn = (GtkButton *) gtk_button_new_with_label (ask->buttons->pdata[i]);
        if (ask->buttons->pdata[i + 1])
        {
            GdkPixbuf *pb;

            pb = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                    ask->buttons->pdata[i + 1], /*FIXME*/16, 0, NULL);
            if (pb)
            {
                gtk_button_set_image (btn, gtk_image_new_from_pixbuf (pb));
                g_object_unref (pb);
            }
        }

        gtk_widget_set_can_default ((GtkWidget *) btn, TRUE);
        gtk_dialog_add_action_widget ((GtkDialog *) w, (GtkWidget *) btn, r);
    }
    gtk_dialog_set_default_response ((GtkDialog *) w, ask->btn_default);

    g_signal_connect (w, "delete-event", (GCallback) gtk_true, NULL);
    g_signal_connect (w, "response", (GCallback) ask_response, ask);
    ask->widget = w;

    gtk_widget_show_all (w);
}

static void
ask_destroy_ui (DonnaTaskHelper    *th,
                struct ask         *ask)
{
    gtk_widget_destroy (ask->widget);
    donna_task_helper_done (th);
}

gint
donna_task_helper_ask (DonnaTask          *task,
                       const gchar        *question,
                       const gchar        *details,
                       gboolean            details_markup,
                       gint                btn_default,
                       const gchar        *btn_label,
                       ...)
{
    DonnaTaskHelperRc rc;
    struct ask ask = { NULL, };

    g_return_val_if_fail (DONNA_IS_TASK (task), -1);
    g_return_val_if_fail (question != NULL, -1);

    ask.question = question;
    ask.details = details;
    ask.details_markup = details_markup;
    ask.btn_default = btn_default;
    ask.buttons = g_ptr_array_new ();

    if (!btn_label)
    {
        /* default buttons */
        ask.btn_default = 2;
        g_ptr_array_add (ask.buttons, (gpointer) "Cancel");
        g_ptr_array_add (ask.buttons, (gpointer) "gtk-cancel");
        g_ptr_array_add (ask.buttons, (gpointer) "No");
        g_ptr_array_add (ask.buttons, (gpointer) "gtk-no");
        g_ptr_array_add (ask.buttons, (gpointer) "Yes");
        g_ptr_array_add (ask.buttons, (gpointer) "gtk-yes");
    }
    else
    {
        va_list va_args;
        gchar *s;

        va_start (va_args, btn_label);
        s = (gchar *) btn_label;
        while (s)
        {
            g_ptr_array_add (ask.buttons, s);
            s = va_arg (va_args, gchar *);
            g_ptr_array_add (ask.buttons, s);
            s = va_arg (va_args, gchar *);
        }
        va_end (va_args);
    }

    rc = donna_task_helper (task,
            (task_helper_ui) ask_show_ui,
            (task_helper_ui) ask_destroy_ui,
            &ask);
    g_ptr_array_unref (ask.buttons);

    if (rc == DONNA_TASK_HELPER_RC_CANCELLING)
        return DONNA_TASK_HELPER_ASK_RC_CANCELLING;
    else if (rc == DONNA_TASK_HELPER_RC_ERROR)
        return DONNA_TASK_HELPER_ASK_RC_ERROR;
    else if (ask.btn_pressed <= 0)
        return DONNA_TASK_HELPER_ASK_RC_NO_ANSWER;
    else
        return ask.btn_pressed;
}
