
#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "task-manager.h"
#include "provider.h"
#include "util.h"
#include "macros.h"
#include "debug.h"


/* helpers */

gint
_donna_get_choice_from_list (gint nb, const gchar *choices[], const gchar *sel)
{
    gchar to_lower = 'A' - 'a';
    gint *matches;
    gint i;

    if (!sel)
        return -1;

    matches = g_new (gint, nb + 1);
    for (i = 0; i < nb; ++i)
        matches[i] = i;
    matches[nb] = -1;

    for (i = 0; sel[i] != '\0'; ++i)
    {
        gchar a;
        gint *m;

        a = sel[i];
        if (a >= 'A' && a <= 'Z')
            a -= to_lower;

        for (m = matches; *m > -1; )
        {
            gchar c;

            c = choices[*m][i];
            if (c >= 'A' && c <= 'Z')
                c -= to_lower;

            if (c != a)
            {
                /* not a match */
                if (--nb == 0)
                {
                    /* no match, we're done */
                    g_free (matches);
                    return -1;
                }
                /* get the last index in current place, so no need to increment
                 * m in the current loop */
                *m = matches[nb];
                matches[nb] = -1;
            }
            else
                ++m;
        }
    }

    if (nb == 1)
        i = matches[0];
    else
        i = -2;
    g_free (matches);
    return i;
}

guint
_donna_get_flags_from_list (gint             nb,
                            const gchar     *choices[],
                            guint            flags[],
                            gchar           *sel)
{
    guint ret = 0;

    for (;;)
    {
        gchar *ss;
        gchar *start, *end, e;
        gint c;

        ss = strchr (sel, '+');
        if (ss)
            *ss = '\0';

        /* since we're allowing separators, we have do "trim" things */
        for (start = sel; isblank (*start); ++start)
            ;
        for (end = start; !isblank (*end) && *end != '\0'; ++end)
            ;
        if (*end == '\0')
            end = NULL;
        else
        {
            e = *end;
            *end = '\0';
        }

        c = _donna_get_choice_from_list (nb, choices, start);

        /* "undo trim" */
        if (ss)
            *ss = '+';
        if (end)
            *end = e;

        if (c < 0)
            return 0;
        ret |= flags[c];

        if (ss)
            sel = ss + 1;
        else
            break;
    }

    return ret;
}

/* commands */

static void
show_err_on_task_failed (DonnaTask  *task,
                         gboolean    timeout_called,
                         DonnaApp   *app)
{
    if (donna_task_get_state (task) != DONNA_TASK_FAILED)
        return;

    donna_app_show_error (app, donna_task_get_error (task),
            "Command 'node_activate': Failed to trigger node");
}

static DonnaTaskState
cmd_ask (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gchar *title = args[0];
    gchar *details = args[1]; /* opt */
    gchar *btn1_icon = args[2]; /* opt */
    gchar *btn1_label= args[3]; /* opt */
    gchar *btn2_icon = args[4]; /* opt */
    gchar *btn2_label= args[5]; /* opt */
    gchar *btn3_icon = args[6]; /* opt */
    gchar *btn3_label= args[7]; /* opt */
    gchar *btn4_icon = args[8]; /* opt */
    gchar *btn4_label= args[9]; /* opt */
    gchar *btn5_icon = args[10]; /* opt */
    gchar *btn5_label= args[11]; /* opt */

    gint r;
    GValue *value;

    r = donna_app_ask (app, title, details, btn1_icon, btn1_label,
            btn2_icon, btn2_label, btn3_icon, btn3_label,
            btn4_icon, btn4_label, btn5_icon, btn5_label, NULL);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, r);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_ask_text (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *title = args[0];
    gchar *details = args[1]; /* opt */
    gchar *main_default = args[2]; /* opt */
    GPtrArray *other = args[3]; /* gchar**, opt */
    GValue *v;
    gchar *s;

    if (other)
        /* we need to make it NULL-terminated for ask_text() */
        g_ptr_array_add (other, NULL);

    s = donna_app_ask_text (app, title, details, main_default,
            (other) ? (const gchar **) other->pdata : NULL,
            &err);
    if (other)
        g_ptr_array_remove_index_fast (other, other->len - 1);
    if (!s)
    {
        if (!err)
            return DONNA_TASK_CANCELLED;

        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_STRING);
    g_value_take_string (v, s);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;
    gboolean val;

    if (!donna_config_get_boolean (donna_app_peek_config (app), &err,
                &val, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;
    gint val;

    if (!donna_config_get_int (donna_app_peek_config (app), &err,
                &val, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_INT);
    g_value_set_int (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;
    gchar *val;

    if (!donna_config_get_string (donna_app_peek_config (app), &err,
                &val, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_STRING);
    g_value_take_string (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_has_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;

    if (!donna_config_has_boolean (donna_app_peek_config (app), &err,
            "%s", name))
    {
        /* this allows to know why: wrong type, is a category, etc */
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, TRUE);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_has_category (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;

    if (!donna_config_has_category (donna_app_peek_config (app), &err,
            "%s", name))
    {
        /* this allows to know why: wrong type, is a category, etc */
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, TRUE);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_has_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;

    if (!donna_config_has_int (donna_app_peek_config (app), &err,
            "%s", name))
    {
        /* this allows to know why: wrong type, is a category, etc */
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, TRUE);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_has_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];
    gchar *extra = args[1]; /* opt */

    const gchar *extra_name;
    GValue *v;

    if (!donna_config_has_option (donna_app_peek_config (app), &err,
                NULL, &extra_name, NULL, "%s", name))
    {
        /* this allows to know why: wrong type, is a category, etc */
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    /* there's an option, do we need to check the extra as well? */
    if (extra && !streq (extra, extra_name))
    {
        donna_task_set_error (task, DONNA_CONFIG_ERROR,
                DONNA_CONFIG_ERROR_OTHER,
                "Option '%s' isn't of extra '%s' (%s)",
                name, extra, (extra_name) ? extra_name : "not an extra");
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, TRUE);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_has_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    GValue *v;

    if (!donna_config_has_string (donna_app_peek_config (app), &err,
            "%s", name))
    {
        /* this allows to know why: wrong type, is a category, etc */
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, TRUE);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_new_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];
    gint   value = GPOINTER_TO_INT (args[1]);

    GValue *v;
    DonnaNode *node;

    if (!donna_config_new_boolean (donna_app_peek_config (app), &err, &node,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_new_category (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];

    GValue *v;
    DonnaNode *node;

    if (!donna_config_new_category (donna_app_peek_config (app), &err, &node,
                "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_new_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];
    gchar *extra = args[1]; /* opt */
    gint   value = GPOINTER_TO_INT (args[2]);

    GValue *v;
    DonnaNode *node;

    if (!donna_config_new_int (donna_app_peek_config (app), &err, &node,
                extra, value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_new_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];
    gchar *extra = args[1]; /* opt */
    gchar *value = args[2];

    GValue *v;
    DonnaNode *node;

    if (!donna_config_new_string (donna_app_peek_config (app), &err, &node,
                extra, value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_remove_category (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];

    if (!donna_config_remove_category (donna_app_peek_config (app), &err,
                "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_remove_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];

    if (!donna_config_remove_option (donna_app_peek_config (app), &err,
                "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_rename_category (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gchar *new_name = args[1];

    if (!donna_config_rename_category (donna_app_peek_config (app), &err,
                new_name, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_rename_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gchar *new_name = args[1];

    if (!donna_config_rename_option (donna_app_peek_config (app), &err,
                new_name, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gint value = GPOINTER_TO_INT (args[1]);

    if (!donna_config_set_boolean (donna_app_peek_config (app), &err,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gint value = GPOINTER_TO_INT (args[1]);

    if (!donna_config_set_int (donna_app_peek_config (app), &err,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}
static DonnaTaskState
cmd_config_set_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *parent = args[0];
    gchar *type = args[1]; /* opt */
    gchar *name = args[2]; /* opt */
    gchar *value = args[3]; /* opt */
    gboolean create_only = GPOINTER_TO_INT (args[4]); /* opt */
    gboolean ask_user = GPOINTER_TO_INT (args[5]); /* opt */

    DonnaNode *node;
    GValue *v;

    if (!donna_config_set_option (donna_app_peek_config (app), &err, &node,
                create_only, ask_user, type, name, value, "%s", parent))
    {
        if (err)
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
        else
            return DONNA_TASK_CANCELLED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];
    gchar *value = args[1];

    if (!donna_config_set_string (donna_app_peek_config (app), &err,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_menu_popup (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    gchar *menus = args[1]; /* opt */

    /* since we give our ref to show_menu() but our args get free-d, we need to
     * add one */
    if (!donna_app_show_menu (app, g_ptr_array_ref (nodes), menus, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_node_get_property (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    const gchar *name = args[1];
    gchar *options = args[2]; /* opt */

    DonnaNodeHasValue has = DONNA_NODE_VALUE_ERROR;
    GValue *value;
    gchar *s = NULL;
    guint64 nb;
    gboolean is_nb = FALSE;

    if (streq (name, "domain") || streq (name, "provider"))
        s = g_strdup (donna_node_get_domain (node));
    else if (streq (name, "location"))
        s = donna_node_get_location (node);
    else if (streq (name, "full-location"))
        s = donna_node_get_full_location (node);
    else if (streq (name, "name"))
        s = donna_node_get_name (node);
    else if (streq (name, "filename"))
        s = donna_node_get_filename (node);
    else if (streq (name, "full-name"))
        has = donna_node_get_full_name (node, TRUE, &s);
    else if (streq (name, "size"))
    {
        has = donna_node_get_size (node, TRUE, &nb);
        is_nb = has == DONNA_NODE_VALUE_SET;
    }
    else if (streq (name, "ctime"))
    {
        has = donna_node_get_ctime (node, TRUE, &nb);
        is_nb = has == DONNA_NODE_VALUE_SET;
    }
    else if (streq (name, "mtime"))
    {
        has = donna_node_get_ctime (node, TRUE, &nb);
        is_nb = has == DONNA_NODE_VALUE_SET;
    }
    else if (streq (name, "atime"))
    {
        has = donna_node_get_ctime (node, TRUE, &nb);
        is_nb = has == DONNA_NODE_VALUE_SET;
    }
    else if (streq (name, "node-type"))
    {
        if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
            s = g_strdup ("Item");
        else
            s = g_strdup ("Container");
    }
    else if (streq (name, "mode"))
    {
        has = donna_node_get_mode (node, TRUE, (guint *) &nb);
        if (has == DONNA_NODE_VALUE_SET)
            s = g_strdup_printf ("%u", (guint) nb);
    }
    else if (streq (name, "uid"))
    {
        has = donna_node_get_uid (node, TRUE, (guint *) &nb);
        if (has == DONNA_NODE_VALUE_SET)
            s = g_strdup_printf ("%u", (guint) nb);
    }
    else if (streq (name, "gid"))
    {
        has = donna_node_get_gid (node, TRUE, (guint *) &nb);
        if (has == DONNA_NODE_VALUE_SET)
            s = g_strdup_printf ("%u", (guint) nb);
    }
    else if (streq (name, "desc"))
        has = donna_node_get_desc (node, TRUE, &s);
    else
    {
        GValue v = G_VALUE_INIT;

        donna_node_get (node, TRUE, name, &has, &v, NULL);
        if (has == DONNA_NODE_VALUE_SET)
        {
            switch (G_VALUE_TYPE (&v))
            {
                case G_TYPE_STRING:
                    s = g_value_dup_string (&v);
                    break;

                case G_TYPE_UINT64:
                    nb = g_value_get_uint64 (&v);
                    is_nb = TRUE;
                    break;

                case G_TYPE_UINT:
                    nb = (guint64) g_value_get_uint (&v);
                    is_nb = TRUE;
                    break;

                case G_TYPE_INT64:
                    nb = (guint64) g_value_get_int64 (&v);
                    is_nb = TRUE;
                    break;

                case G_TYPE_INT:
                    nb = (guint64) g_value_get_int (&v);
                    is_nb = TRUE;
                    break;

                default:
                    if (g_value_type_transformable (G_VALUE_TYPE (&v), G_TYPE_STRING))
                    {
                        GValue v2 = G_VALUE_INIT;
                        g_value_transform (&v, &v2);
                        s = g_value_dup_string (&v2);
                        g_value_unset (&v2);
                    }
                    else
                    {
                        s = donna_node_get_full_location (node);
                        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                                DONNA_COMMAND_ERROR_OTHER,
                                "Command 'node_get_property': "
                                "Failed to retrieve property '%s' on node '%s': "
                                "Unsupported property type: %s",
                                name, s, G_VALUE_TYPE_NAME (&v));
                        g_free (s);
                        g_value_unset (&v);
                        return DONNA_TASK_FAILED;
                    }
            }
            g_value_unset (&v);
        }
    }

    if (is_nb && !options)
        s = g_strdup_printf ("%" G_GUINT64_FORMAT, nb);
    else if (is_nb)
    {
        DonnaConfig *config = donna_app_peek_config (app);
        gboolean is_time = streqn (options, "time", 4);

        if ((!is_time && !streqn (options, "size", 4))
                || (options[4] != '=' && options[4] != '@' && options[4] != '\0'))
        {
            s = donna_node_get_full_location (node);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'node_get_property': "
                    "Failed to format property '%s' on node '%s': "
                    "Invalid formatting options, neither 'date' nor 'size': %s",
                    name, s, options);
            g_free (s);
            return DONNA_TASK_FAILED;
        }

        if (is_time)
        {
            DonnaTimeOptions timeopts;
            const gchar *sce;
            gchar *fmt;

            if (options[4] == '@')
                sce = options + 5;
            else
                sce = "defaults/time";

            if (options[4] == '=')
                fmt = options + 5;
            else
                if (!donna_config_get_string (config, &err, &fmt,
                            "%s/format", sce))
                    goto err;

            if (!donna_config_get_int (config, &err,
                        (gint *) &timeopts.age_span_seconds,
                        "%s/age_span_seconds", sce))
                goto err;

            if (!donna_config_get_string (config, &err,
                        (gchar **) &timeopts.age_fallback_format,
                        "%s/age_fallback_format", sce))
                goto err;

            s = donna_print_time (nb, fmt, &timeopts);
        }
        else
        {
            const gchar *sce;
            gchar *fmt;
            gint digits;
            gboolean long_unit;
            gssize len;

            if (options[4] == '@')
                sce = options + 5;
            else
                sce = "defaults/size";

            if (options[4] == '=')
                fmt = options + 5;
            else
                if (!donna_config_get_string (config, &err, &fmt,
                            "%s/format", sce))
                    goto err;

            if (!donna_config_get_int (config, &err, &digits,
                        "%s/digits", sce))
                goto err;

            if (!donna_config_get_boolean (config, &err, &long_unit,
                        "%s/long_unit", sce))
                goto err;

            len = donna_print_size (NULL, 0, fmt, nb, digits, long_unit);
            s = g_new (gchar, ++len);
            donna_print_size (s, len, fmt, nb, digits, long_unit);
        }

        if (err)
        {
err:
            s = donna_node_get_full_location (node);
            g_prefix_error (&err, "Command 'node_get_property': "
                    "Failed to format property '%s' on node '%s': ",
                    name, s);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    if (!s)
    {
        s = donna_node_get_full_location (node);
        if (has == DONNA_NODE_VALUE_NONE)
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'node_get_property': Property '%s' doesn't exist on node '%s'",
                    name, s);
        else
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'node_get_property': Failed to retrieve property '%s' on node '%s'",
                    name, s);
        g_free (s);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, s);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_node_new_child (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    gchar *type = args[1];
    gchar *name = args[2];

    const GValue *v;
    GValue *value;
    DonnaTaskState ret;
    DonnaTask *t;
    const gchar *choices[] = { "item", "container" };
    DonnaNodeType types[] = { DONNA_NODE_ITEM, DONNA_NODE_CONTAINER };
    gint c;

    c = _get_choice (choices, type);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Cannot create new child, unknown type '%s'; "
                "Must be 'item' or 'container'",
                type);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_new_child_task (node, types[c], name, &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'node_new_child': Failed to run new_child task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    ret = donna_task_get_state (t);
    if (ret != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'node_new_child' failed: ");
            donna_task_take_error (task, err);
        }
        else
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'node_new_child' failed: Unable to create new child");
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_get_return_value (t);

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_set_object (value, g_value_get_object (v));
    donna_task_release_return_value (task);

    g_object_unref (t);
    return ret;
}

struct popup_children_data
{
    DonnaApp        *app;
    DonnaTreeView   *tree;
    gchar           *filter;
    GPtrArray       *nodes;
    gchar           *menus;
};

static DonnaTaskState
popup_children (DonnaTask *task, struct popup_children_data *data)
{
    GError *err = NULL;

    if (data->filter)
    {
        gboolean rc;

        if (data->tree)
            rc = donna_tree_view_filter_nodes (data->tree, data->nodes,
                    data->filter, &err);
        else
            rc = donna_app_filter_nodes (data->app, data->nodes,
                    data->filter, &err);
        if (!rc)
        {
            g_prefix_error (&err, "Command 'node_popup_children': Failed to filter children: ");
            donna_task_take_error (task, err);
            g_ptr_array_unref (data->nodes);
            return DONNA_TASK_FAILED;
        }
    }

    if (!donna_app_show_menu (data->app, data->nodes, data->menus, &err))
    {
        g_prefix_error (&err, "Command 'node_popup_children': Failed to show menu: ");
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_node_popup_children (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    gchar *children = args[1];
    gchar *menus = args[2]; /* opt */
    gchar *filter = args[3]; /* opt */
    DonnaTreeView *tree = args[4]; /* opt */

    const gchar *c_children[] = { "all", "item", "container" };
    DonnaNodeType childrens[]  = { DONNA_NODE_ITEM | DONNA_NODE_CONTAINER,
        DONNA_NODE_ITEM, DONNA_NODE_CONTAINER };
    DonnaTaskState state;
    DonnaTask *t;
    struct popup_children_data data;
    gint c;

    c = _get_choice (c_children, children);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid type of node children: '%s'; Must be 'item', 'container' or 'all'",
                children);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_get_children_task (node, childrens[c], &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'node_popup_children': "
                "Failed to run get_children task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    if (state != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'node_popup_children' failed: ");
            donna_task_take_error (task, err);
        }
        else
        {
            gchar *fl = donna_node_get_full_location (node);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'node_popup_children' failed: Unable to get children of '%s'",
                    fl);
            g_free (fl);
        }
        g_object_unref (t);
        return state;
    }

    data.app    = app;
    data.tree   = tree;
    data.filter = filter;
    data.menus  = menus;
    data.nodes  = g_value_dup_boxed (donna_task_get_return_value (t));
    g_object_unref (t);

    t = donna_task_new ((task_fn) popup_children, &data, NULL);
    donna_task_set_visibility (t, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'node_popup_children': "
                "Failed to run popup_children task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    if (state != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
            donna_task_take_error (task, g_error_copy (err));
    }
    g_object_unref (t);
    return state;
}

static DonnaTaskState
cmd_node_trigger (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    gchar *on_item = args[1]; /* opt */
    gchar *on_container = args[2]; /* opt */

    const gchar *s_item[] = { "trigger", "goto" };
    const gchar *s_container[] = { "trigger", "goto", "popup" };
    enum trg {
        TRG_TRIGGER    = (1 << 0),
        TRG_GOTO       = (1 << 1),
        TRG_POPUP      = (1 << 2)
    };
    enum trg trigger[] = { TRG_TRIGGER, TRG_GOTO, TRG_POPUP };
    gint c_item;
    guint trg_container;

    DonnaTreeView *tree;

    if (on_item)
    {
        c_item = _get_choice (s_item, on_item);
        if (c_item < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Invalid trigger mode for item: '%s'; "
                    "Must be 'trigger' or 'goto'",
                    on_item);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c_item = 0; /* TRG_TRIGGER */

    if (on_container)
    {
        trg_container = _get_flags (s_container, trigger, on_container);
        if (trg_container == 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Invalid trigger mode for container: '%s'; "
                    "Must be 'trigger', 'goto', 'popup', "
                    "or a '+'-separated combination of 'trigger' and another one.",
                    on_container);
            return DONNA_TASK_FAILED;
        }
    }
    else
        trg_container = TRG_GOTO;

    if (donna_node_get_node_type (node) == DONNA_NODE_ITEM)
    {
        if (trigger[c_item] == TRG_TRIGGER)
        {
            DonnaTask *trigger_task;

            trigger_task = donna_node_trigger_task (node, &err);
            if (!trigger_task)
            {
                donna_task_take_error (task, err);
                return DONNA_TASK_FAILED;
            }

            donna_task_set_callback (trigger_task,
                    (task_callback_fn) show_err_on_task_failed, app, NULL);
            donna_app_run_task (app, trigger_task);
            return DONNA_TASK_DONE;
        }
        /* fallthrough for GOTO */
    }
    else /* DONNA_NODE_CONTAINER */
    {
        if (trg_container & TRG_TRIGGER)
        {
            DonnaNodeHasValue has;
            GValue v = G_VALUE_INIT;

            donna_node_get (node, TRUE, "container-trigger", &has, &v, NULL);
            if (has == DONNA_NODE_VALUE_SET)
            {
                if (G_VALUE_TYPE (&v) == G_TYPE_STRING)
                {
                    if (!donna_app_trigger_node (app, g_value_get_string (&v), &err))
                    {
                        gchar *fl = donna_node_get_full_location (node);
                        g_prefix_error (&err, "Command 'node_trigger': "
                                "Failed to trigger container '%s'; "
                                "Trigger was '%s': ",
                                fl, g_value_get_string (&v));
                        donna_task_take_error (task, err);
                        g_free (fl);
                        g_value_unset (&v);
                        return DONNA_TASK_FAILED;
                    }
                    g_value_unset (&v);
                    return DONNA_TASK_DONE;
                }
                else if (G_VALUE_TYPE (&v) == DONNA_TYPE_NODE)
                {
                    DonnaTask *t;

                    t = donna_node_trigger_task (g_value_get_object (&v), &err);
                    if (!t)
                    {
                        g_prefix_error (&err, "Command 'node_trigger': "
                                "Failed to get trigger task from container-trigger node: ");
                        donna_task_take_error (task, err);
                        g_value_unset (&v);
                        return DONNA_TASK_FAILED;
                    }

                    donna_app_run_task (app, t);
                    g_value_unset (&v);
                    return DONNA_TASK_DONE;
                }
                g_value_unset (&v);
            }
        }

        if (trg_container & TRG_POPUP)
        {
            gpointer _args[5] = { node, "all", NULL, NULL, NULL };
            return cmd_node_popup_children (task, app, _args);
        }
        else if (!(trg_container & TRG_GOTO))
            return DONNA_TASK_DONE;
    }

    g_object_get (app, "active-list", &tree, NULL);
    if (!donna_tree_view_set_location (tree, node, &err))
    {
        donna_task_take_error (task, err);
        g_object_unref (tree);
        return DONNA_TASK_FAILED;
    }
    g_object_unref (tree);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_nodes_filter (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    const gchar *filter = args[1];
    DonnaTreeView *tree = args[2]; /* opt */
    gboolean dup_arr = GPOINTER_TO_INT (args[3]); /* opt */

    GPtrArray *arr;
    gboolean done;
    GValue *value;

    if (dup_arr)
    {
        guint i;

        arr = g_ptr_array_new_full (nodes->len, g_object_unref);
        for (i = 0; i < nodes->len; ++i)
            g_ptr_array_add (arr, g_object_ref (nodes->pdata[i]));
    }
    else
        arr = nodes;

    if (tree)
        done = donna_tree_view_filter_nodes (tree, arr, filter, &err);
    else
        done = donna_app_filter_nodes (app, arr, filter, &err);

    if (!done)
    {
        g_prefix_error (&err, "Command 'nodes_filter': ");
        donna_task_take_error (task, err);
        if (dup_arr)
            g_ptr_array_unref (arr);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    if (dup_arr)
        g_value_take_boxed (value, arr);
    else
        g_value_set_boxed (value, arr);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_nodes_io (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    gchar *io_type = args[1];
    DonnaNode *dest = args[2]; /* opt */
    gchar *new_name = args[3]; /* opt */

    const gchar *c_io_type[] = { "copy", "move", "delete" };
    DonnaIoType io_types[] = { DONNA_IO_COPY, DONNA_IO_MOVE, DONNA_IO_DELETE };
    gint c_io;
    DonnaTask *t;
    DonnaTaskState state;

    c_io = _get_choice (c_io_type, io_type);
    if (c_io < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid type of IO operation: '%s'; "
                "Must be 'copy', 'move' or 'delete'",
                io_type);
        return DONNA_TASK_FAILED;
    }

    t = donna_app_nodes_io_task (app, nodes, io_types[c_io], dest, new_name, &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'nodes_io': "
                "Failed to run nodes_io task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    if (state == DONNA_TASK_DONE)
    {
        GValue *value;

        value = donna_task_grab_return_value (task);
        g_value_init (value, G_TYPE_PTR_ARRAY);
        g_value_set_boxed (value, g_value_get_boxed (donna_task_get_return_value (t)));
        donna_task_release_return_value (task);
    }
    else
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'nodes_io': IO task failed: ");
            donna_task_take_error (task, err);
        }
        else
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'nodes_io': IO task failed without error message");
    }

    g_object_unref (t);
    return state;
}

static DonnaTaskState
cmd_nodes_remove_from (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    DonnaNode *source = args[1];

    DonnaProvider *provider;
    DonnaTask *t;
    DonnaTaskState state;

    provider = donna_node_peek_provider (source);
    t = donna_provider_remove_from_task (provider, nodes, source, &err);
    if (!t)
    {
        g_prefix_error (&err, "Command 'nodes_remove_from': Failed to get task: ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'nodes_remove_from': "
                "Failed to run remove_from task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    if (state != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'nodes_remove_from' failed: ");
            donna_task_take_error (task, err);
        }
        else if (state == DONNA_TASK_FAILED)
        {
            gchar *fl = donna_node_get_full_location (source);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'nodes_remove_from' failed: Unable to remove nodes from '%s'",
                    fl);
            g_free (fl);
        }
        g_object_unref (t);
        return state;
    }
    g_object_unref (t);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_task_cancel (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];

    if (!donna_task_manager_cancel (donna_app_peek_task_manager (app), node, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_task_set_state (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    gchar *state = args[1];

    const gchar *choices[] = { "run", "pause", "cancel", "stop", "wait" };
    DonnaTaskState states[] = { DONNA_TASK_RUNNING, DONNA_TASK_PAUSED,
        DONNA_TASK_CANCELLED, DONNA_TASK_STOPPED, DONNA_TASK_WAITING };
    gint c;

    if (!streq (donna_node_get_domain (node), "task"))
    {
        gchar *fl = donna_node_get_full_location (node);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command 'task_set_state' cannot be used on '%s', only works on node in domain 'task'",
                fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    c = _get_choice (choices, state);
    if (c < 0)
    {
        gchar *d = donna_node_get_name (node);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid state for task '%s': '%s' "
                "Must be 'run', 'pause', 'cancel', 'stop' or 'wait'",
                d, state);
        g_free (d);
        return DONNA_TASK_FAILED;
    }

    if (!donna_task_manager_set_state (donna_app_peek_task_manager (app), node,
                states[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_task_show_ui (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];

    if (!donna_task_manager_show_ui (donna_app_peek_task_manager (app), node, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_task_toggle (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];

    DonnaTask *t;

    if (!streq (donna_node_get_domain (node), "task"))
    {
        gchar *fl = donna_node_get_full_location (node);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command 'task_toggle' cannot be used on '%s', only works on node in domain 'task'",
                fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_trigger_task (node, &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'task_toggle': "
                "Failed to run trigger task: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    if (donna_task_get_state (t) != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
            donna_task_take_error (task, g_error_copy (err));
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    g_object_unref (t);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tasks_cancel (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    DonnaTaskManager *tm;
    guint i;

    tm = donna_app_peek_task_manager (app);
    for (i = 0; i < nodes->len; ++i)
    {
        if (!donna_task_manager_cancel (tm, nodes->pdata[i], &err))
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tasks_cancel_all (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    donna_task_manager_cancel_all (donna_app_peek_task_manager (app));
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tasks_pre_exit (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gboolean always_confirm = GPOINTER_TO_INT (args[0]); /* opt */

    GValue *value;
    gboolean ret;

    ret = donna_task_manager_pre_exit (donna_app_peek_task_manager (app),
            always_confirm);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, (ret) ? 1 : 0);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tasks_switch (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    gboolean switch_on = GPOINTER_TO_INT (args[1]); /* opt */
    gboolean fail_on_failure = GPOINTER_TO_INT (args[2]); /* opt */

    if (!donna_task_manager_switch_tasks (donna_app_peek_task_manager (app), nodes,
                switch_on, fail_on_failure, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_abort (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    donna_tree_view_abort (tree);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_activate_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_activate_row (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_add_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err= NULL;
    DonnaTreeView *tree = args[0];
    DonnaNode *node = args[1];

    if (!donna_tree_view_add_root (tree, node, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_column_edit (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gchar *col_name = args[2];

    if (!donna_tree_view_column_edit (tree, rid, col_name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_column_set_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *column = args[1];
    const gchar *option = args[2];
    const gchar *value  = args[3];
    const gchar *s_l    = args[4]; /* opt */

    const gchar *c_s_l[] = { "memory", "current", "ask", "arrangement", "tree",
        "column", "default" };
    DonnaColumnOptionSaveLocation save_location[] = {
        DONNA_COLUMN_OPTION_SAVE_IN_MEMORY,
        DONNA_COLUMN_OPTION_SAVE_IN_CURRENT,
        DONNA_COLUMN_OPTION_SAVE_IN_ASK,
        DONNA_COLUMN_OPTION_SAVE_IN_ARRANGEMENT,
        DONNA_COLUMN_OPTION_SAVE_IN_TREE,
        DONNA_COLUMN_OPTION_SAVE_IN_COLUMN,
        DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT };
    gint c;

    if (s_l)
    {
        c = _get_choice (c_s_l, s_l);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot set column option, invalid save location: '%s'; "
                    "Must be 'memory', 'current', 'ask', 'arrangement', 'tree', "
                    "'column' or 'default'",
                    s_l);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default: IN_MEMORY */
        c = 0;

    if (!donna_tree_view_column_set_option (tree, column, option, value,
                save_location[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_column_set_value (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gboolean to_focused = GPOINTER_TO_INT (args[2]); /* opt */
    const gchar *column = args[3];
    const gchar *value = args[4];
    DonnaTreeRowId *rid_ref = args[5]; /* opt */

    if (!donna_tree_view_column_set_value (tree, rid, to_focused, column,
                value, rid_ref, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_context_get_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rowid = args[1]; /* opt */
    const gchar *column = args[2]; /* opt */
    gchar *sections = args[3]; /* opt */

    GPtrArray *nodes;
    GValue *value;

    nodes = donna_tree_view_context_get_nodes (tree, rowid, column, sections, &err);
    if (!nodes)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_context_popup (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rowid = args[1]; /* opt */
    const gchar *column = args[2]; /* opt */
    gchar *sections = args[3]; /* opt */
    const gchar *menus = args[4]; /* opt */
    gboolean no_focus_grab = GPOINTER_TO_INT (args[5]); /* opt */

    if (!donna_tree_view_context_popup (tree, rowid, column, sections, menus,
                no_focus_grab, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_full_collapse (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_full_collapse (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_full_expand (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_full_expand (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_location (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    DonnaNode *node;
    GValue *v;

    node = donna_tree_view_get_location (tree);
    if (!node)
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_node_at_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    DonnaNode *node;
    GValue *v;

    node = donna_tree_view_get_node_at_row (tree, rid, &err);
    if (!node)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gboolean to_focused = GPOINTER_TO_INT (args[2]); /* opt */

    GPtrArray *arr;
    GValue *v;

    arr = donna_tree_view_get_nodes (tree, rid, to_focused, &err);
    if (!arr)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (v, arr);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_visual (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gchar *visual = args[2];
    gchar *source = args[3];

    const gchar *c_visual[] = { "name", "icon", "box", "highlight", "clicks" };
    DonnaTreeVisual visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT, DONNA_TREE_VISUAL_CLICKS };
    const gchar *c_source[] = { "any", "tree", "node" };
    DonnaTreeVisualSource sources[] = { DONNA_TREE_VISUAL_SOURCE_ANY,
        DONNA_TREE_VISUAL_SOURCE_TREE, DONNA_TREE_VISUAL_SOURCE_NODE };
    gchar *s;
    GValue *value;
    gint c_v;
    gint c_s;

    c_v = _get_choice (c_visual, visual);
    if (c_v < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'box', 'highlight' or 'clicks'",
                visual);
        return DONNA_TASK_FAILED;
    }

    c_s = _get_choice (c_source, source);
    if (c_s < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown source '%s'. "
                "Must be 'tree', 'node', or 'any'",
                source);
        return DONNA_TASK_FAILED;
    }

    s = donna_tree_view_get_visual (tree, rid, visuals[c_v], sources[c_s], &err);
    if (!s)
    {
        if (err)
            donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, s);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_node_down (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gint level = GPOINTER_TO_INT (args[1]); /* opt */

    DonnaNode *node;
    GValue *value;

    if (level == 0)
        level = 1;

    node = donna_tree_view_get_node_down (tree, level, &err);
    if (!node)
    {
        g_prefix_error (&err, "Command 'tree_get_node_down': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_go_down (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gint level = GPOINTER_TO_INT (args[1]); /* opt */

    if (level == 0)
        level = 1;

    if (!donna_tree_view_go_down (tree, level, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_node_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    DonnaNode *node;
    GValue *value;

    node = donna_tree_view_get_node_root (tree, &err);
    if (!node)
    {
        g_prefix_error (&err, "Command 'tree_get_node_root': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_go_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    if (!donna_tree_view_go_root (tree, &err))
    {
        g_prefix_error (&err, "Command 'tree_go_root': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_node_up (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gint level = GPOINTER_TO_INT (args[1]); /* opt */

    DonnaNode *node;
    GValue *value;

    /* we cannot differentiate between the level arg being set to 0, and not
     * specified. So, we assume not set and default to 1.
     * In order to go up to the root, use a negative value */
    if (level == 0)
        level = 1;

    node = donna_tree_view_get_node_up (tree, level, &err);
    if (!node)
    {
        g_prefix_error (&err, "Command 'tree_get_node_up': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_go_up (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gint level = GPOINTER_TO_INT (args[1]); /* opt */
    gchar *s_set = args[2]; /* opt */

    const gchar *c_set[] = { "scroll", "focus", "cursor" };
    DonnaTreeSet sets[] = { DONNA_TREE_SET_SCROLL, DONNA_TREE_SET_FOCUS,
        DONNA_TREE_SET_CURSOR };
    DonnaTreeSet set;

    /* we cannot differentiate between the level arg being set to 0, and not
     * specified. So, we assume not set and default to 1.
     * In order to go up to the root, use a negative value */
    if (level == 0)
        level = 1;

    if (s_set)
        set = _get_flags (c_set, sets, s_set);
    else
        set = 0;

    if (!donna_tree_view_go_up (tree, level, set, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_goto_line (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *s_set = args[1];
    DonnaTreeRowId *rid = args[2];
    gint nb = GPOINTER_TO_INT (args[3]); /* opt */
    gchar *nb_type = args[4]; /* opt */
    gchar *action = args[5]; /* opt */
    gboolean to_focused = GPOINTER_TO_INT (args[6]); /* opt */

    const gchar *c_set[] = { "scroll", "focus", "cursor" };
    DonnaTreeSet sets[] = { DONNA_TREE_SET_SCROLL, DONNA_TREE_SET_FOCUS,
        DONNA_TREE_SET_CURSOR };
    const gchar *c_nb_type[] = { "repeat", "line", "percent" };
    DonnaTreeGoto nb_types[] = { DONNA_TREE_GOTO_REPEAT, DONNA_TREE_GOTO_LINE,
        DONNA_TREE_GOTO_PERCENT };
    DonnaTreeSet set;
    const gchar *c_action[] = { "select", "unselect", "invert", "define" };
    DonnaTreeSelAction actions[] = { DONNA_TREE_SEL_SELECT, DONNA_TREE_SEL_UNSELECT,
        DONNA_TREE_SEL_INVERT, DONNA_TREE_SEL_DEFINE };
    gint c_n;
    gint c_a;

    set = _get_flags (c_set, sets, s_set);
    if (set == 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot go to line, unknown set type '%s'. "
                "Must be (a '+'-separated combination of) 'scroll', 'focus' and/or 'cursor'",
                s_set);
        return DONNA_TASK_FAILED;
    }

    if (nb_type)
    {
        c_n = _get_choice (c_nb_type, nb_type);
        if (c_n < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot goto line, invalid type: '%s'; "
                    "Must be 'repeat', 'line' or 'percent'",
                    nb_type);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c_n = 0;

    if (action)
    {
        c_a = _get_choice (c_action, action);
        if (c_a < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot goto line, invalid selection action: '%s'; "
                    "Must be 'select', 'unselect' or 'invert'",
                    action);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c_a = -1;

    if (!donna_tree_view_goto_line (tree, set, rid, nb, nb_types[c_n],
                (c_a < 0) ? 0 : actions[c_a], to_focused, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_history_clear (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */

    const gchar *s_directions[] = { "backward", "forward" };
    DonnaHistoryDirection directions[] = { DONNA_HISTORY_BACKWARD,
        DONNA_HISTORY_FORWARD };
    guint dir;

    if (direction)
    {
        dir = _get_flags (s_directions, directions, direction);
        if (dir == 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Invalid argument direction '%s'; "
                    "Must be (a '+'-separated combination of) 'backward' and/or 'forward'",
                    direction);
            return DONNA_TASK_FAILED;
        }
    }
    else
        dir = DONNA_HISTORY_BACKWARD;

    if (!donna_tree_view_history_clear (tree, dir, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_history_get (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */
    guint nb = GPOINTER_TO_UINT (args[2]); /* opt */

    const gchar *s_directions[] = { "backward", "forward" };
    DonnaHistoryDirection directions[] = { DONNA_HISTORY_BACKWARD,
        DONNA_HISTORY_FORWARD };
    guint dir;

    GValue *value;
    GPtrArray *arr;

    if (direction)
    {
        dir = _get_flags (s_directions, directions, direction);
        if (dir == 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Invalid argument direction '%s'; "
                    "Must be (a '+'-separated combination of) 'backward' and/or 'forward'",
                    direction);
            return DONNA_TASK_FAILED;
        }
    }
    else
        dir = DONNA_HISTORY_BACKWARD | DONNA_HISTORY_FORWARD;

    arr = donna_tree_view_history_get (tree, dir, nb, &err);
    if (!arr)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, arr);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_history_get_node (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */
    guint nb = GPOINTER_TO_UINT (args[2]); /* opt */

    const gchar *s_directions[] = { "backward", "forward" };
    DonnaHistoryDirection directions[] = { DONNA_HISTORY_BACKWARD,
        DONNA_HISTORY_FORWARD };
    gint dir;

    GValue *value;
    DonnaNode *node;

    if (direction)
    {
        dir = _get_choice (s_directions, direction);
        if (dir < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Invalid argument direction '%s'; "
                    "Must be 'backward' or 'forward'",
                    direction);
            return DONNA_TASK_FAILED;
        }
    }
    else
        dir = 0;

    if (nb == 0)
        nb = 1;

    node = donna_tree_view_history_get_node (tree, directions[dir], nb, &err);
    if (!node)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_history_move (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */
    guint nb = GPOINTER_TO_UINT (args[2]); /* opt */

    const gchar *s_directions[] = { "backward", "forward" };
    DonnaHistoryDirection directions[] = { DONNA_HISTORY_BACKWARD,
        DONNA_HISTORY_FORWARD };
    guint dir;

    if (direction)
    {
        dir = _get_flags (s_directions, directions, direction);
        if (dir == 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Invalid argument direction '%s'; "
                    "Must be (a '+'-separated combination of) 'backward' and/or 'forward'",
                    direction);
            return DONNA_TASK_FAILED;
        }
    }
    else
        dir = DONNA_HISTORY_BACKWARD;

    /* since 0 has no sense here, we'll just assume this was not specified, and
     * default to 1 */
    if (nb == 0)
        nb = 1;

    if (!donna_tree_view_history_move (tree, dir, nb, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_load_list_file (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *file = args[1];
    gchar *s_elements = args[2]; /* opt */

    const gchar *_s_elements[] = { "focus", "sort", "scroll", "selection" };
    DonnaListFileElements _elements[] = { DONNA_LIST_FILE_FOCUS,
        DONNA_LIST_FILE_SORT, DONNA_LIST_FILE_SCROLL, DONNA_LIST_FILE_SELECTION };
    guint elements;

    if (s_elements)
    {
        elements = _get_flags (_s_elements, _elements, s_elements);
        if (elements == (guint) -1)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'tree_load_list_file': Invalid elements : '%s'; "
                    "Must be (a '+'-separated combination of) 'focus', 'sort', "
                    "'scroll', and/or 'selection'",
                    s_elements);
            return DONNA_TASK_FAILED;
        }
    }
    else
        elements = 0;

    if (!donna_tree_view_load_list_file (tree, file, elements, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_maxi_collapse (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_maxi_collapse (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_maxi_expand (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_maxi_expand (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_move_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gint move = GPOINTER_TO_INT (args[2]);

    if (!donna_tree_view_move_root (tree, rid, move, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_refresh (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *mode = args[1];

    const gchar *choices[] = { "visible", "simple", "normal", "reload" };
    DonnaTreeRefreshMode modes[] = { DONNA_TREE_REFRESH_VISIBLE,
        DONNA_TREE_REFRESH_SIMPLE, DONNA_TREE_REFRESH_NORMAL,
        DONNA_TREE_REFRESH_RELOAD };
    gint c;

    c = _get_choice (choices, mode);
    if (c < 0)
    {
        donna_task_set_error (task,
                DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid argument 'mode': '%s'; "
                "Must be 'visible', 'simple', 'normal' or 'reload'",
                mode);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_refresh (tree, modes[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_remove_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_remove_row (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_reset_keys (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    donna_tree_view_reset_keys (tree);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_save_list_file (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *file = args[1];
    gchar *s_elements = args[2]; /* opt */

    const gchar *_s_elements[] = { "focus", "sort", "scroll", "selection" };
    DonnaListFileElements _elements[] = { DONNA_LIST_FILE_FOCUS,
        DONNA_LIST_FILE_SORT, DONNA_LIST_FILE_SCROLL, DONNA_LIST_FILE_SELECTION };
    guint elements;

    if (s_elements)
    {
        elements = _get_flags (_s_elements, _elements, s_elements);
        if (elements == (guint) -1)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'tree_save_list_file': Invalid elements : '%s'; "
                    "Must be (a '+'-separated combination of) 'focus', 'sort', "
                    "'scroll', and/or 'selection'",
                    s_elements);
            return DONNA_TASK_FAILED;
        }
    }
    else
        elements = 0;

    if (!donna_tree_view_save_list_file (tree, file, elements, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_selection (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *action = args[1];
    DonnaTreeRowId *rid = args[2];
    gboolean to_focused = GPOINTER_TO_INT (args[3]); /* opt */

    const gchar *choices[] = { "select", "unselect", "invert", "define" };
    DonnaTreeSelAction actions[] = { DONNA_TREE_SEL_SELECT,
        DONNA_TREE_SEL_UNSELECT, DONNA_TREE_SEL_INVERT, DONNA_TREE_SEL_DEFINE };
    gint c;

    c = _get_choice (choices, action);
    if (c < 0)
    {
        donna_task_set_error (task,
                DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid argument 'action': '%s'; "
                "Must be 'select', 'unselect' or 'invert'",
                action);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_selection (tree, actions[c], rid, to_focused, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_selection_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *action = args[1];
    GPtrArray *nodes = args[2];

    const gchar *choices[] = { "select", "unselect", "invert", "define" };
    DonnaTreeSelAction actions[] = { DONNA_TREE_SEL_SELECT,
        DONNA_TREE_SEL_UNSELECT, DONNA_TREE_SEL_INVERT, DONNA_TREE_SEL_DEFINE };
    gint c;

    c = _get_choice (choices, action);
    if (c < 0)
    {
        donna_task_set_error (task,
                DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid argument 'action': '%s'; "
                "Must be 'select', 'unselect' or 'invert'",
                action);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_selection_nodes (tree, actions[c], nodes, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_cursor (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gboolean no_scroll = GPOINTER_TO_INT (args[2]);

    if (!donna_tree_view_set_cursor (tree, rid, no_scroll, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_focus (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];

    if (!donna_tree_view_set_focus (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_key_mode (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];
    gchar *key_mode = args[1];

    donna_tree_view_set_key_mode (tree, key_mode);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_location (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaNode *node = args[1];

    if (!donna_tree_view_set_location (tree, node, &err))
    {
        if (err)
            donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *option = args[1];
    const gchar *value  = args[2];
    const gchar *s_l    = args[3]; /* opt */

    const gchar *c_s_l[] = { "memory", "current", "ask", "tree", "default" };
    DonnaTreeviewOptionSaveLocation save_location[] = {
        DONNA_COLUMN_OPTION_SAVE_IN_MEMORY,
        DONNA_COLUMN_OPTION_SAVE_IN_CURRENT,
        DONNA_COLUMN_OPTION_SAVE_IN_ASK,
        DONNA_COLUMN_OPTION_SAVE_IN_TREE,
        DONNA_COLUMN_OPTION_SAVE_IN_DEFAULT };
    gint c;

    if (s_l)
    {
        c = _get_choice (c_s_l, s_l);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot set tree option, invalid save location: '%s'; "
                    "Must be 'memory', 'current', 'ask', 'tree' or 'default'",
                    s_l);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default: IN_MEMORY */
        c = 0;

    if (!donna_tree_view_set_option (tree, option, value, save_location[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_second_sort (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *column = args[1];
    const gchar *s_order = args[2]; /* opt */

    const gchar *c_order[] = { "asc", "desc", "unknown" };
    DonnaSortOrder order[] = { DONNA_SORT_ASC, DONNA_SORT_DESC, DONNA_SORT_UNKNOWN };
    gint c;

    if (s_order)
    {
        c = _get_choice (c_order, s_order);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'tree_set_second_sort': Invalid sort order '%s'; "
                    "Must be 'asc', 'desc' or 'unknown'",
                    s_order);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default UNKNOWN */
        c = 2;

    if (!donna_tree_view_set_second_sort_order (tree, column, order[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_sort (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *column = args[1];
    const gchar *s_order = args[2]; /* opt */

    const gchar *c_order[] = { "asc", "desc", "unknown" };
    DonnaSortOrder order[] = { DONNA_SORT_ASC, DONNA_SORT_DESC, DONNA_SORT_UNKNOWN };
    gint c;

    if (s_order)
    {
        c = _get_choice (c_order, s_order);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'tree_set_sort': Invalid sort order '%s'; "
                    "Must be 'asc', 'desc' or 'unknown'",
                    s_order);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default UNKNOWN */
        c = 2;

    if (!donna_tree_view_set_sort_order (tree, column, order[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_visual (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gchar *visual = args[2];
    gchar *value = args[3];

    const gchar *choices[] = { "name", "icon", "box", "highlight", "clicks" };
    DonnaTreeVisual visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT,
        DONNA_TREE_VISUAL_CLICKS };
    gint c;

    c = _get_choice (choices, visual);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'icon', 'box', 'highlight' or 'clicks'",
                visual);
        return DONNA_TASK_FAILED;
    }

    /* empty string as value is turned into NULL to means unset the visual */
    if (*value == '\0')
        value = NULL;

    if (!donna_tree_view_set_visual (tree, rid, visuals[c], value, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_toggle_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gchar *toggle = args[2];

    const gchar *choices[] = { "standard", "full", "maxi" };
    DonnaTreeToggle toggles[] = { DONNA_TREE_TOGGLE_STANDARD,
        DONNA_TREE_TOGGLE_FULL, DONNA_TREE_TOGGLE_MAXI };
    gint c;

    c = _get_choice (choices, toggle);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Cannot toggle row, unknown toggle type '%s'; "
                "Must be 'standard', 'full' or 'maxi'",
                toggle);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_toggle_row (tree, rid, toggles[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_void (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    return DONNA_TASK_DONE;
}



#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_type) \
command.name           = g_strdup (#cmd_name); \
command.argc           = cmd_argc; \
command.return_type    = cmd_return_type; \
command.visibility     = cmd_visibility; \
command.func           = (command_fn) cmd_##cmd_name; \
_command = g_slice_new (struct command); \
memcpy (_command, &command, sizeof (struct command)); \
_command->arg_type = g_new (DonnaArgType, command.argc); \
memcpy (_command->arg_type, arg_type, sizeof (DonnaArgType) * command.argc); \
g_hash_table_insert (commands, command.name, _command);

void
_donna_add_commands (GHashTable *commands)
{
    struct command *_command;
    struct command command;
    DonnaArgType arg_type[12];
    command.arg_type = arg_type;
    gint i;

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (ask, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL | DONNA_ARG_IS_ARRAY;
    add_command (ask_text, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_get_boolean, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_get_int, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_get_string, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_has_boolean, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_has_category, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_has_int, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (config_has_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_has_string, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_new_boolean, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_new_category, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_new_int, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_new_string, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_remove_category, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_remove_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_rename_category, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_rename_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_set_boolean, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_set_int, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (config_set_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_set_string, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (menu_popup, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (node_get_property, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (node_new_child, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW | DONNA_ARG_IS_OPTIONAL;
    add_command (node_popup_children, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (node_trigger, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (nodes_filter, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (nodes_io, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (nodes_remove_from, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (task_cancel, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (task_set_state, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (task_show_ui, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (task_toggle, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    add_command (tasks_cancel, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    add_command (tasks_cancel_all, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tasks_pre_exit, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tasks_switch, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    add_command (tree_abort, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_activate_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (tree_add_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tree_column_edit, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_column_set_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_column_set_value, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_context_get_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_context_popup, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_full_collapse, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_full_expand, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    add_command (tree_get_location, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_get_node_at_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_get_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tree_get_visual, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_get_node_down, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_go_down, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    add_command (tree_get_node_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    add_command (tree_go_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_get_node_up, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_go_up, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_goto_line, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_history_clear, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_history_get, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_history_get_node, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_history_move, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_load_list_file, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_maxi_collapse, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_maxi_expand, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (tree_move_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tree_refresh, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_remove_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    add_command (tree_reset_keys, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_save_list_file, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_selection, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    add_command (tree_selection_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_set_cursor, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tree_set_focus, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tree_set_key_mode, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (tree_set_location, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_set_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_set_second_sort, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_set_sort, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tree_set_visual, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tree_toggle_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (void, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);
}
