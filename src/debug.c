
#include "config.h"

#include <glib.h>
#include <gdk/gdk.h>
#include <string.h>
#include "app.h"        /* DONNA_APP_ERROR */
#include "debug.h"
#include "macros.h"

void
_donna_debug_ungrab (void)
{
    GdkDeviceManager *devmngr;
    GList *list, *l;

    devmngr = gdk_display_get_device_manager (gdk_display_get_default ());
    list = gdk_device_manager_list_devices (devmngr, GDK_DEVICE_TYPE_MASTER);
    for (l = list; l; l = l->next)
    {
        GdkDevice *dev = l->data;

        if (gdk_device_get_source (dev) != GDK_SOURCE_MOUSE
                && gdk_device_get_source (dev) != GDK_SOURCE_KEYBOARD)
            continue;

        gdk_device_ungrab (dev, GDK_CURRENT_TIME);
    }
    g_list_free (list);
}

static gchar *_def = NULL;
static gpointer _valid = NULL;
struct valid
{
    DonnaDebugFlags flag;
    gchar *names;
};

gboolean
donna_debug_set_valid (gchar *def, GError **error)
{
    GArray *array = NULL;
    gchar *s;

    if (G_UNLIKELY (_valid))
    {
        g_critical ("Tried to set debug message filters more than once");
        g_free (def);
        return FALSE;
    }

    _def = def;
    donna_debug_flags = 0;

    for (;;)
    {
        struct valid valid;
        gchar *e;

        s = strchr (def, ',');
        if (s)
            *s = '\0';

        e = strchr (def, ':');
        if (e)
            *e = '\0';

        if (streq (def, "node"))
            valid.flag = DONNA_DEBUG_NODE;
        else if (streq (def, "task"))
            valid.flag = DONNA_DEBUG_TASK;
        else if (streq (def, "treeview"))
            valid.flag = DONNA_DEBUG_TREE_VIEW;
        else if (streq (def, "task-manager"))
            valid.flag = DONNA_DEBUG_TASK_MANAGER;
        else if (streq (def, "provider"))
            valid.flag = DONNA_DEBUG_PROVIDER;
        else if (streq (def, "config"))
            valid.flag = DONNA_DEBUG_CONFIG;
        else if (streq (def, "app"))
            valid.flag = DONNA_DEBUG_APP;
        else if (streq (def, "all"))
            valid.flag = DONNA_DEBUG_ALL;
        else
        {
            g_set_error (error, DONNA_APP_ERROR, DONNA_APP_ERROR_OTHER,
                    "Unknown debug flag '%s'", def);
            g_free (_def);
            _def = NULL;
            if (array)
                g_array_free (array, TRUE);
            return FALSE;
        }

        donna_debug_flags |= valid.flag;
        if (e)
        {
            /* move back over the ':' so we can have an extra NUL as EO-list */
            memmove (e, e + 1, sizeof (gchar) * (strlen (e + 1) + 1));
            valid.names = e;
            /* turn '+'-s into NUL-s */
            for (;;)
            {
                e = strchr (valid.names, '+');
                if (e)
                    *e = '\0';
                else
                    break;
            }

            if (!array)
                array = g_array_new (TRUE, FALSE, sizeof (struct valid));
            g_array_append_val (array, valid);
        }

        if (s)
            def = s + 1;
        else
            break;
    }

    if (array)
        _valid = g_array_free (array, FALSE);
    else
    {
        g_free (_def);
        _def = NULL;
    }

    return TRUE;
}

gboolean
_donna_debug_is_valid (DonnaDebugFlags flag, const gchar *name)
{
    struct valid *v = (struct valid *) _valid;
    gboolean ret = TRUE;

    /* this is only used from DONNA_DEBUG() so we can assume that name in not
     * NULL, and that flag is set in donna_debug_flags */

    if (!v)
        return TRUE;

    for (;;)
    {
        if (v->flag == flag)
        {
            gchar *s = v->names;

            for (;;)
            {
                if (streq (name, s))
                    return TRUE;
                s += strlen (s) + 1;
                if (*s == '\0')
                    break;
            }

            /* we'll keep iterating, in case the same flag was defined more than
             * once. And if not (or no match), we'll return FALSE */
            ret = FALSE;
        }

        ++v;
        if (v->flag == 0)
            break;
    }

    return ret;
}

void
donna_debug_reset_valid (void)
{
    g_free (_def);
    g_free (_valid);
    _def = _valid = NULL;
}
