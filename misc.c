
#include <gtk/gtk.h>
#include "misc.h"

gboolean
_key_press_ctrl_a_cb (GtkEntry *entry, GdkEventKey *event)
{
    if ((event->keyval == GDK_KEY_A || event->keyval == GDK_KEY_a)
            && (event->state & GDK_CONTROL_MASK))
    {
        GtkEditable *editable = (GtkEditable *) entry;
        gint start, end;

        /* special handling of Ctrl+A:
         * - if no selection, select all;
         * - if basename (i.e. without .extension) is selected, select all
         * - else select basename
         */

        if (!gtk_editable_get_selection_bounds (editable, &start, &end))
            /* no selection, select all */
            gtk_editable_select_region (editable, 0, -1);
        else
        {
            const gchar *name;
            gsize  len;
            gsize  dot;
            gchar *s;

            /* locate the dot before the extension */
            name = gtk_entry_get_text (entry);
            dot = 0;
            for (len = 1, s = g_utf8_next_char (name);
                    *s != '\0';
                    ++len, s = g_utf8_next_char (s))
            {
                if (*s == '.')
                    dot = len;
            }
            if (start == 0 && end == dot)
                /* already selected, toggle back to all */
                gtk_editable_select_region (editable, 0, -1);
            else if (dot > 0)
                /* select only up to the .ext */
                gtk_editable_select_region (editable, 0, dot);
        }

        return TRUE;
    }
    return FALSE;
}
