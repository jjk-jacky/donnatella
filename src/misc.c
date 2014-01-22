
#include "config.h"

#include <gtk/gtk.h>
#include <string.h>
#include "misc.h"
#include "node.h"
#include "macros.h"

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
            gint len;
            gint dot;
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

/**
 * _resolve_path:
 * @node: (allow none): A #DonnaNode of the parent of the (relative) @path
 * @path: The (relative) path to resolve
 *
 * This will "resolve" @path (located in @node, if specified), that is remove
 * the "./" and handle the "../" (i.e. go up one level). This all happens on the
 * string, assuming non-flat hierarchy.
 *
 * It will also turn multiple slashes into one, and remove any trailing slashes.
 *
 * Returns: (transfer full): A newly allocated string (use g_free() when done),
 * or %NULL if nothing needed to be done
 */
gchar *
_resolve_path (DonnaNode *node, const gchar *_path)
{
    GString *str = NULL;
    gchar *first = NULL;
    gchar *path  = (gchar *) _path;
    gchar *s;

    for (s = path; *s != '\0'; ++s)
    {
        if (*s == '/')
        {
            if (s == _path)
                continue;

            if (!str)
            {
                if (node)
                {
                    gchar *ss = donna_node_get_full_location (node);
                    str = g_string_new (ss);
                    if (ss[strlen (ss) - 1] != '/')
                        g_string_append_c (str, '/');
                    g_free (ss);
                }
                else
                    str = g_string_new (NULL);
            }
            if (path)
                g_string_append_len (str, path, s - path);
            path = NULL;
            continue;
        }

        if (!path)
            path = s;

        if (*s == '.')
        {
            gint flg = 0;

            if (s[1] == '/' || s[1] == '\0')
                flg = 1;
            else if (s[1] == '.' && (s[2] == '/' || s[2] == '\0'))
                flg = 2;

            if (flg > 0)
            {
                if (!str)
                {
                    if (node)
                    {
                        gchar *ss = donna_node_get_full_location (node);
                        str = g_string_new (ss);
                        if (ss[strlen (ss) - 1] != '/')
                            g_string_append_c (str, '/');
                        g_free (ss);
                    }
                    else
                        str = g_string_new (NULL);
                }
                g_string_append_len (str, path, s - path);
                path = NULL;
            }

            if (flg == 2)
            {
                gchar *last;

                if (!first)
                {
                    first = strchr (str->str, '/');
                    if (!first)
                        first = str->str;
                }

                /* -2: 1 to get to last char, 1 to skip last '/' */
                for (last = str->str + str->len - 2; last > first; --last)
                    if (*last == '/')
                        break;

                if (last < first)
                    last = first;
                /* +1: keep '/' */
                g_string_truncate (str, (gsize) (last - str->str + 1));
            }
        }

        for ( ; *s != '/' && *s != '\0'; ++s)
            ;
    }

    if (str)
    {
        /* set up new path */
        if (path && *path != '\0')
            g_string_append (str, path);
        else
        {
            s = strchr (str->str, ':');
            if (s)
            {
                if (!streq (s + 1, "/") && str->str[str->len - 1] == '/')
                    g_string_truncate (str, str->len - 1);
            }
            else
            {
                if (str->len > 1 && str->str[str->len - 1] == '/')
                    g_string_truncate (str, str->len - 1);
            }
        }

        return g_string_free (str, FALSE);
    }

    if (!node)
    {
        gchar *ss;
        gboolean strip_trailing = FALSE;

        ss = strchr (path, ':');
        if (ss)
            strip_trailing = (!streq (ss + 1, "/") && s[-1] == '/');
        else
            strip_trailing = (s - 1 > path && s[-1] == '/');

        if (strip_trailing)
            return g_strndup (path, (gsize) (s - path - 1));
        else
            return NULL;
    }
    else
    {
        gchar *ss;

        ss = donna_node_get_full_location (node);
        if (*path == '\0')
            return ss;
        str = g_string_new (ss);
        g_free (ss);

        if (str->str[str->len - 1] == '/')
        {
            if (*path == '/')
                g_string_append (str, path + 1);
            else
                g_string_append (str, path);
        }
        else
        {
            if (*path != '/')
                g_string_append_c (str, '/');
            g_string_append (str, path);
        }

        if (str->str[str->len - 1] == '/')
            g_string_truncate (str, str->len - 1);

        return g_string_free (str, FALSE);
    }
}
