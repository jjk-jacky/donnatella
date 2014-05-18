/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * command.c
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

#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "task-manager.h"
#include "provider.h"
#include "task-process.h"   /* for command exec() */
#include "util.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:commands
 * @Short_description: Supported commands
 *
 * As you probably know, about everything that can be done in donna is done via
 * commands, which you can use from keys, clicks, on (context) menus, etc
 *
 * For more about commands and their syntax, please see
 * #DonnaProviderCommand.description
 *
 * Below is the list of all supported commands. For each argument as well as the
 * return value, annotations can be found. "allow-none" means the argument is
 * optional and can be omitted (defaulting then to 0/nothing, unless specified
 * otherwise); "array" means the argument/return value is an array. For
 * arguments, you can use either one element (e.g. node), or an array.
 *
 * Some commands allow for a string argument either one of many strings, or a
 * combination of strings (using plus sign (<systemitem>+</systemitem>) as
 * separator). In such cases, you don't have to specify the entire string, but
 * only as many characters as needed to uniquely identify it.
 *
 * For example, command tv_goto_line() has an argument @set that must be one or
 * more of "scroll", "focus" and "cursor"
 * So you could use e.g. "scroll+focus", "scro+foc" or simply "s+f"
 * Similarly, its argument @action can be one of "select", "unselect", "invert"
 * or "define"; So you could as well use "s", "u", "i" or "d" respectively.
 *
 * Note that lots of commands are "wrappers" around internal functions, so a
 * more detailed documentation is to be found at the documentation of the
 * function itself.
 */


/* internal, used by app.c */
void _donna_add_commands (GHashTable *commands);


/* helpers */

gint
_donna_get_choice_from_list (guint nb, const gchar *choices[], const gchar *sel)
{
    gint to_lower = 'A' - 'a';
    gint *matches;
    gint i;

    if (!sel)
        return -1;

    matches = g_new (gint, nb + 1);
    for (i = 0; i < (gint) nb; ++i)
        matches[i] = i;
    matches[nb] = -1;

    for (i = 0; sel[i] != '\0'; ++i)
    {
        gint a;
        gint *m;

        a = sel[i];
        if (a >= 'A' && a <= 'Z')
            a -= to_lower;

        for (m = matches; *m > -1; )
        {
            gint c;

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
_donna_get_flags_from_list (guint            nb,
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
            return (guint) -1;
        ret |= flags[c];

        if (ss)
            sel = ss + 1;
        else
            break;
    }

    return ret;
}

#define ensure_uint(cmd_name, arg_nb, arg_name, arg_val) do {                \
    if (arg_val < 0)                                                         \
    {                                                                        \
        donna_task_set_error (task, DONNA_COMMAND_ERROR,                     \
                DONNA_COMMAND_ERROR_OTHER,                                   \
                "Command '%s': Argument #%d (%s) must be positive (was %d)", \
                cmd_name, arg_nb, arg_name, arg_val);                        \
        return DONNA_TASK_FAILED;                                            \
    }                                                                        \
} while (0)


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

/**
 * ask:
 * @title: Title, the question to ask
 * @details: (allow-none): Details below the title
 * @btn1_icon: (allow-none): Name of the icon for button 1
 * @btn1_label: (allow-none): Label of button 1
 * @btn2_icon: (allow-none): Name of the icon for button 2
 * @btn2_label: (allow-none): Label of button 2
 * @btn3_icon: (allow-none): Name of the icon for button 3
 * @btn3_label: (allow-none): Label of button 3
 * @btn4_icon: (allow-none): Name of the icon for button 4
 * @btn4_label: (allow-none): Label of button 4
 * @btn5_icon: (allow-none): Name of the icon for button 5
 * @btn5_label: (allow-none): Label of button 5
 *
 * Shows a popup with at least 2 buttons. Useful for confirmations, etc
 *
 * See donna_app_ask() for more.
 *
 * Returns: Number of the pressed button
 */
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

/**
 * ask_text:
 * @title: Title
 * @details: (allow-none): Details below the title
 * @main_default: (allow-none): Default value pre-set in the entry
 * @other_defaults: (allow-none) (array): Other default value(s)
 *
 * Shows a dialog asking user for an input. Pressing button "Cancel" will have
 * the command end as %DONNA_TASK_CANCELLED
 *
 * See donna_app_ask_text() for more.
 *
 * Returns: The text
 */
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

/**
 * config_get_boolean:
 * @name: Name of the option
 *
 * Returns the value of a boolean option. If the option doesn't exist or isn't a
 * boolean, command will fail.
 *
 * See donna_config_get_boolean()
 *
 * Returns: The value (%TRUE or %FALSE) of the option
 */
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

/**
 * config_get_int:
 * @name: Name of the option
 *
 * Returns the value of an integer option. If the option doesn't exist or isn't
 * an integer, command will fail.
 *
 * See donna_config_get_int()
 *
 * Returns: The value of the option
 */
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

/**
 * config_get_string:
 * @name: Name of the option
 *
 * Returns the value of a string option. If the option doesn't exist or isn't a
 * string, command will fail.
 *
 * See donna_config_get_string()
 *
 * Returns: The value of the option
 */
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

/**
 * config_has_boolean:
 * @name: Name of the option
 *
 * Returns %TRUE if the option exists and is a boolean.
 *
 * Note that if the option doesn't exist or is not a boolean, the command will
 * fail; Hence, the command can only ever return %TRUE
 *
 * See donna_config_has_boolean()
 *
 * Returns: %TRUE
 */
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

/**
 * config_has_category:
 * @name: Name of the option
 *
 * Returns %TRUE if the option exists and is a category.
 *
 * Note that if the option doesn't exist or is not a category, the command will
 * fail; Hence, the command can only ever return %TRUE
 *
 * See donna_config_has_category()
 *
 * Returns: %TRUE
 */
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

/**
 * config_has_int:
 * @name: Name of the option
 *
 * Returns %TRUE if the option exists and is an integer.
 *
 * Note that if the option doesn't exist or is not an integer, the command will
 * fail; Hence, the command can only ever return %TRUE
 *
 * See donna_config_has_int()
 *
 * Returns: %TRUE
 */
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

/**
 * config_has_option:
 * @name: Name of the option
 * @extra: (allow-none): Name of the extra
 *
 * Returns %TRUE if the option exists and is of extra @extra (if specified)
 *
 * Note that if the option doesn't exist or is not of the specified extra, the
 * command will fail; Hence, it can only ever return %TRUE
 *
 * If @extra was not specified, the command will succeed/return %TRUE if the
 * option exists, regardless of its type (it will however fail if it's a
 * category).
 *
 * Returns: %TRUE
 */
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

/**
 * config_has_string:
 * @name: Name of the option
 *
 * Returns %TRUE if the option exists and is a string.
 *
 * Note that if the option doesn't exist or is not a string, the command will
 * fail; Hence, the command can only ever return %TRUE
 *
 * See donna_config_has_string()
 *
 * Returns: %TRUE
 */
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

/**
 * config_new_boolean:
 * @name: Name of the option
 * @value: Value to set
 *
 * Creates a new boolean option @name and sets it to @value. Will fail if the
 * option already exists.
 *
 * See donna_config_new_boolean()
 *
 * Returns: The #DonnaNode of the newly created option
 */
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

/**
 * config_new_category:
 * @name: Name of the option
 *
 * Creates a new category @name. Will fail if the category already exists.
 *
 * See donna_config_new_category()
 *
 * Returns: The #DonnaNode of the newly created category
 */
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

/**
 * config_new_int:
 * @name: Name of the option
 * @extra: (allow-none): Name of the extra for the new option
 * @value: Value to set
 *
 * Creates a new integer option @name (of extra @extra) and sets it to @value.
 * Will fail if the option already exists.
 *
 * See donna_config_new_int()
 *
 * Returns: The #DonnaNode of the newly created option
 */
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

/**
 * config_new_string:
 * @name: Name of the option
 * @extra: (allow-none): Name of the extra for the new option
 * @value: Value to set
 *
 * Creates a new string option @name (of extra @extra) and sets it to @value.
 * Will fail if the option already exists.
 *
 * See donna_config_new_string()
 *
 * Returns: The #DonnaNode of the newly created option
 */
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

/**
 * config_remove_category:
 * @name: Name of the category
 *
 * Remove category @name
 *
 * See donna_config_remove_category()
 */
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

/**
 * config_remove_option:
 * @name: Name of the option
 *
 * Remove option @name
 *
 * See donna_config_remove_option()
 */
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

/**
 * config_rename_category:
 * @category: Full name of the category to rename
 * @new_name: New name for the category
 *
 * Renames category @category to @new_name
 *
 * See donna_config_rename_category()
 */
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

/**
 * config_rename_option:
 * @option: Full name of the option to rename
 * @new_name: New name for the option
 *
 * Renames option @option to @new_name
 *
 * See donna_config_rename_option()
 */
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

/**
 * config_save:
 * @filename: (allow-none): Filename to save configuration to
 *
 * Saves the current configuration to @filename
 *
 * @filename can be either a full path to a file, or it will be processed
 * through donna_app_get_conf_filename()
 */
static DonnaTaskState
cmd_config_save (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    const gchar *filename = args[0]; /* opt */

    gchar *file;
    gchar *data;

    if (filename && *filename == '/')
    {
        if (!g_get_filename_charsets (NULL))
            file = g_filename_from_utf8 (filename, -1, NULL, NULL, NULL);
        else
            file = (gchar *) filename;
    }
    else
        file = donna_app_get_conf_filename (app, (filename) ? filename : "donnatella.conf");

    data = donna_config_export_config (donna_app_peek_config (app));
    if (!g_file_set_contents (file, data, -1, &err))
    {
        g_prefix_error (&err, "Command 'config_save': Failed to save configuration "
                "to '%s': ", (filename) ? filename : "donnatella.conf");
        g_free (data);
        if (file != filename)
            g_free (file);
        return DONNA_TASK_FAILED;
    }

    if (!g_get_filename_charsets (NULL))
    {
        gchar *s = g_filename_to_utf8 (file, -1, NULL, NULL, NULL);
        g_info ("Configuration saved to file '%s'", s);
        g_free (s);
    }
    else
        g_info ("Configuration saved to file '%s'", file);

    g_free (data);
    if (file != filename)
        g_free (file);
    return DONNA_TASK_DONE;
}

/**
 * config_set_boolean:
 * @name: Name of the option to set
 * @value: Value to set
 *
 * Sets boolean option @name to @value, creating it if needed
 *
 * See donna_config_set_boolean()
 *
 * Returns: The @value just set
 */
static DonnaTaskState
cmd_config_set_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gint value = GPOINTER_TO_INT (args[1]);

    GValue *v;

    if (!donna_config_set_boolean (donna_app_peek_config (app), &err,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_INT);
    g_value_set_int (v, (value) ? 1 : 0);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * config_set_int:
 * @name: Name of the option to set
 * @value: Value to set
 *
 * Sets integer option @name to @value, creating it if needed
 *
 * See donna_config_set_int()
 *
 * Returns: The @value just set
 */
static DonnaTaskState
cmd_config_set_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gint value = GPOINTER_TO_INT (args[1]);

    GValue *v;

    if (!donna_config_set_int (donna_app_peek_config (app), &err,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_INT);
    g_value_set_int (v, value);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * config_set_option:
 * @parent: Full name of the category to create/set the option in
 * @type: (allow-none): Name of the type of the option
 * @name: (allow-none): Name of the option to create/set
 * @value: (allow-none): String representation of the value to set
 * @create_only: (allow-none): Whether to perform option creation only or not
 * @ask_user: (allow-none): Whether to show a window or not
 *
 * Creates/sets an option to the value represented by @value, optionally showing
 * a window to change the option name/type/value.
 *
 * See donna_config_set_option() for more.
 *
 * Returns: The node of set/created option
 */
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

/**
 * config_set_string:
 * @name: Name of the option to set
 * @value: Value to set
 *
 * Sets string option @name to @value, creating it if needed
 *
 * See donna_config_set_string()
 *
 * Returns: The @value just set
 */
static DonnaTaskState
cmd_config_set_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err  = NULL;
    gchar *name  = args[0];
    gchar *value = args[1];

    GValue *v;

    if (!donna_config_set_string (donna_app_peek_config (app), &err,
                value, "%s", name))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_STRING);
    g_value_set_string (v, value);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * config_try_get_boolean:
 * @name: Name of the option
 * @default: Value to return if option doesn't exist
 *
 * Returns the value of a boolean option. If the option doesn't exist, @default
 * is returned (command fails on other errors, e.g. option exists but is of
 * different type)
 *
 * See donna_config_get_boolean()
 *
 * Returns: The value of the option, or @default
 */
static DonnaTaskState
cmd_config_try_get_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gboolean def = GPOINTER_TO_INT (args[1]);

    GValue *v;
    gboolean val;

    if (!donna_config_get_boolean (donna_app_peek_config (app), &err,
                &val, "%s", name))
    {
        if (g_error_matches (err, DONNA_CONFIG_ERROR, DONNA_CONFIG_ERROR_NOT_FOUND))
            val = def;
        else
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

/**
 * config_try_get_int:
 * @name: Name of the option
 * @default: Value to return if option doesn't exist
 *
 * Returns the value of an integer option. If the option doesn't exist, @default
 * is returned (command fails on other errors, e.g. option exists but is of
 * different type)
 *
 * See donna_config_get_int()
 *
 * Returns: The value of the option, or @default
 */
static DonnaTaskState
cmd_config_try_get_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gint def = GPOINTER_TO_INT (args[1]);

    GValue *v;
    gint val;

    if (!donna_config_get_int (donna_app_peek_config (app), &err,
                &val, "%s", name))
    {
        if (g_error_matches (err, DONNA_CONFIG_ERROR, DONNA_CONFIG_ERROR_NOT_FOUND))
            val = def;
        else
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_INT);
    g_value_set_int (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

/**
 * config_try_get_string:
 * @name: Name of the option
 * @default: Value to return if option doesn't exist
 *
 * Returns the value of a string option. If the option doesn't exist, @default
 * is returned (command fails on other errors, e.g. option exists but is of
 * different type)
 *
 * See donna_config_get_string()
 *
 * Returns: The value of the option, or @default
 */
static DonnaTaskState
cmd_config_try_get_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    gchar *name = args[0];
    gchar *def = args[1];

    GValue *v;
    gchar *val;

    if (!donna_config_get_string (donna_app_peek_config (app), &err,
                &val, "%s", name))
    {
        if (g_error_matches (err, DONNA_CONFIG_ERROR, DONNA_CONFIG_ERROR_NOT_FOUND))
            val = g_strdup (def);
        else
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_STRING);
    g_value_take_string (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

enum exec_mode
{
    EXEC_STANDARD = 0,
    EXEC_ERROR,
    EXEC_NOFAIL,
    EXEC_DONE,
    EXEC_FAILED
};

struct exec
{
    DonnaTask *task;
    enum exec_mode mode;
    gint rc;
    gboolean mixed;
    GString *str_out;
    GString *str_err;
};

static void
free_exec (struct exec *exec)
{
    if (exec->str_out)
        g_string_free (exec->str_out, TRUE);
    if (exec->str_err)
        g_string_free (exec->str_err, TRUE);
}

static DonnaTaskState
exec_closer (DonnaTask      *task,
             gint            rc,
             DonnaTaskState  state,
             struct exec    *exec)
{
    if (state != DONNA_TASK_DONE)
        goto free;

    switch (exec->mode)
    {
        case EXEC_STANDARD:
            state = (rc == 0) ? DONNA_TASK_DONE : DONNA_TASK_FAILED;
            break;

        case EXEC_ERROR:
            state = (exec->str_err) ? DONNA_TASK_FAILED : DONNA_TASK_DONE;
            break;

        case EXEC_NOFAIL:
            state = DONNA_TASK_DONE;
            break;

        case EXEC_DONE:
            state = (rc == exec->rc) ? DONNA_TASK_DONE : DONNA_TASK_FAILED;
            break;

        case EXEC_FAILED:
            state = (rc == exec->rc) ? DONNA_TASK_FAILED : DONNA_TASK_DONE;
            break;
    }

    if (state == DONNA_TASK_DONE)
    {
        GValue *v;
        gchar *s;

        if (exec->str_out)
            s = g_string_free (exec->str_out, FALSE);
        else
            s = g_strdup ("");

        v = donna_task_grab_return_value (exec->task);
        g_value_init (v, G_TYPE_STRING);
        g_value_take_string (v, s);
        donna_task_release_return_value (exec->task);
        exec->str_out = NULL;
    }
    else
    {
        gchar *s;

        if (exec->mixed && exec->str_out)
            s = exec->str_out->str;
        else if (!exec->mixed && exec->str_err)
            s = exec->str_err->str;
        else
            s = (gchar *) "(No error message)";

        donna_task_set_error (exec->task, DONNA_TASK_PROCESS_ERROR,
                DONNA_TASK_PROCESS_ERROR_OTHER,
                "%s", s);
    }

free:
    free_exec (exec);
    return state;
}

static void
exec_pipe_new_line (DonnaTaskProcess *taskp,
                    DonnaPipe         pipe,
                    const gchar      *line,
                    struct exec      *exec)
{
    GString **str;

    if (!line)
        /* EOF */
        return;

    if (exec->mixed || pipe == DONNA_PIPE_OUTPUT)
        str = &exec->str_out;
    else
        str = &exec->str_err;

    if (!*str)
        *str = g_string_new (NULL);
    else
        g_string_append_c (*str, '\n');
    g_string_append (*str, line);
}

/**
 * exec:
 * @cmdline: Command line to execute
 * @workdir: (allow-none): Working directory for @cmdline, or %NULL to use the
 * current directory
 * @mode: (allow-none): Mode to determine whether it failed or not
 * @rc: (allow-none); Return code used with @mode "done" and "failed"
 * @mixed: (allow-none): Set to 1 to use mixed stdout and stderr (as return
 * value or error message)
 *
 * Executes @cmdline and returns its output (or mixed stdout and stderr if
 * @mixed is 1)
 *
 * @mode must be one of:
 * - standard : Success when process returned zero, else failure
 * - error : Failed when something was sent to stderr, else success
 * - nofail : Always a success
 * - done : Success when process returned @rc, else failure
 * - failed : Failure when process returned @rc, else success
 *
 * On success the return value will be the content of stdout, and on error the
 * error message will be the content of stderr (or the mixed of the two (in
 * either case) with @mixed was 1).
 *
 * Returns: Output of the process (stdout, miwed with stderr if @mixed is 1)
 */
static DonnaTaskState
cmd_exec (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    const gchar *cmdline = args[0];
    const gchar *workdir = args[1]; /* opt */
    const gchar *mode_s = args[2]; /* opt */
    gint rc = GPOINTER_TO_INT (args[3]); /* opt */
    gboolean mixed = GPOINTER_TO_INT (args[4]); /* opt */

    const gchar *c_mode[] = { "standard", "error", "nofail", "done", "failed" };
    enum exec_mode mode[] = { EXEC_STANDARD, EXEC_ERROR, EXEC_NOFAIL, EXEC_DONE,
        EXEC_FAILED };
    gint c;

    DonnaTask *t;
    DonnaTaskState state;
    struct exec exec = { NULL, };
    GPtrArray *arr;

    if (mode_s)
    {
        c = _get_choice (c_mode, mode_s);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'exec': Invalid mode: '%s'; "
                    "Must be one of 'standard', 'error', 'nofail', 'done' or 'failed'",
                    mode_s);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* STANDARD */
        c = 0;

    /* so the return value/error message can be set directly on the command's
     * task, and not the task-process */
    exec.task = task;
    exec.mode = mode[c];
    exec.rc = rc;
    exec.mixed = mixed;

    t = donna_task_process_new (workdir, cmdline, TRUE,
            (task_closer_fn) exec_closer, &exec, NULL);
    g_object_ref (t);
    if (!workdir
            && !donna_task_process_set_workdir_to_curdir ((DonnaTaskProcess *) t, app))
    {
        g_object_unref (t);
        donna_task_set_error (task, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                "Failed to set task-process's workdir to curdir");
        return DONNA_TASK_FAILED;
    }

    /* set taskui messages */
    donna_task_process_set_ui_msg ((DonnaTaskProcess *) t);
    g_signal_connect (t, "pipe-new-line", (GCallback) exec_pipe_new_line, &exec);
    /* let's assume it's a RAM-only command, or make it run in parallel to any
     * other operation */
    arr = g_ptr_array_new ();
    donna_task_set_devices (t, arr);
    g_ptr_array_unref (arr);

    donna_app_run_task (app, t);
    if (!donna_task_wait_for_it (t, task, &err))
    {
        g_prefix_error (&err, "Command 'exec': Failed to run and wait for task-process: ");
        donna_task_take_error (task, err);
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    state = donna_task_get_state (t);
    g_object_unref (t);
    return state;
}


/**
 * focus_move:
 * @move: (allow-none): How to move focus
 *
 * Moves the focus around in main window
 *
 * See donna_app_move_focus() for more
 */
static DonnaTaskState
cmd_focus_move (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gint move = GPOINTER_TO_INT (args[0]); /* opt */

    donna_app_move_focus (app, (move == 0) ? 1 : move);
    return DONNA_TASK_DONE;
}

/**
 * focus_set:
 * @type: Type of GUI element to focus
 * @name: Name of GUI element to focus
 *
 * Focus the element @name of type @type
 *
 * @type must be one of "treeview"
 *
 * See donna_app_set_focus() for more
 */
static DonnaTaskState
cmd_focus_set (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    const gchar *type = args[0];
    const gchar *name = args[1];

    const gchar *c_type[] = { "treeview" };
    gint c;

    c = _get_choice (c_type, type);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid type of GUI element: '%s'; "
                "Must be 'treeview'",
                type);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_set_focus (app, c_type[c], name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * intref_free:
 * @intref: String representation of the intref to free
 *
 * Free the memory from @intref
 *
 * See donna_app_free_int_ref() for more.
 */
static DonnaTaskState
cmd_intref_free (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    const gchar *intref = args[0];

    if (donna_app_free_int_ref (app, intref))
        return DONNA_TASK_DONE;

    donna_task_set_error (task, DONNA_COMMAND_ERROR,
            DONNA_COMMAND_ERROR_NOT_FOUND,
            "Command 'intref_free': Cannot free '%s': no such intref",
            intref);
    return DONNA_TASK_FAILED;
}

/**
 * menu_popup:
 * @nodes: (array): The nodes to show in a menu
 * @menu: (allow-none): The menu definition to use
 *
 * Show @nodes in a popup menu
 *
 * See donna_app_show_menu() for more.
 */
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

/**
 * node_get_property:
 * @node: The node to get @property from
 * @property: The name of the property to get
 * @options: (allow-none): Formatting options
 *
 * Retrieve the value of @property on @node
 *
 * This can only work on properties that can have a string representation of
 * their value, so it won't work for icons.
 *
 * For properties holding a timestamp (e.g. mtime, etc) or size (e.g. size) you
 * can use @options to specify how it should be formatted.
 *
 * @options should be either "time" or "size" to use the corresponding from
 * "defaults/time" or "defaults/size" respectively. You can also add "@" then
 * the name of the category to load options from, or use "=" and specify the
 * actual format to use directly.
 *
 * Property "node-type" will return either "Item" or "Container"
 *
 * Returns: String representation of the value of @property on @node
 */
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
                    "Invalid formatting options, neither 'time' nor 'size': %s",
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
            gsize len;

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

/**
 * node_new_child:
 * @node: The parent node to create a child in
 * @type: The type of node to create
 * @name: The name of the node to create
 *
 * Creates a new node named @name under @node.
 *
 * @type must be either "item" or "container"
 *
 * Returns: The newly-created node
 */
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

        rc = donna_app_filter_nodes (data->app, data->nodes, data->filter,
                data->tree, &err);
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

/**
 * node_popup_children:
 * @node: The node to popup children of in a menu
 * @children: Which types of children to show
 * @menus: (allow-none): Menu definition to use
 * @filter: (allow-none): Filter to use to filter which children to show
 * @tree: (allow-none): Name of the treeview to filter children through
 *
 * Show a popup menu with the children of @node
 *
 * @children must be one of "all", "item" or "container"
 *
 * @filter can be a string used to filter children, only including in the menu
 * the ones that match. Note that to only show non-hidden (dot files) children
 * you can use options via @menus
 *
 * See donna_app_show_menu() for more on menus; See donna_app_filter_nodes() for
 * more on node filtering.
 */
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

struct node_trigger_goto
{
    DonnaApp *app;
    DonnaNode *node;
};

static DonnaTaskState
nt_goto (DonnaTask *task, struct node_trigger_goto *ntg)
{
    GError *err = NULL;
    DonnaTreeView *tree;

    g_object_get (ntg->app, "active-list", &tree, NULL);
    if (!donna_tree_view_set_location (tree, ntg->node, &err))
    {
        donna_task_take_error (task, err);
        g_object_unref (tree);
        return DONNA_TASK_FAILED;
    }
    g_object_unref (tree);
    return DONNA_TASK_DONE;
}

/**
 * node_trigger:
 * @node: The node to trigger
 * @on_item: (allow-none): What to do when @node is an item
 * @on_container: (allow-none): What to do when @node is a container
 *
 * Triggers @node.
 *
 * @on_item must be one of "trigger" or "goto" and defines what will be done if
 * @node is an item. "trigger" will run the trigger task for the item; What is
 * actually done depends on the node/its provider. For example, on "fs" items
 * will be executed/opened with associated application.
 * "goto" will set @node as new current location of the active-list
 *
 * @on_container must be a combination of "trigger", "goto" and "popup" and
 * defines what will be done if @node is a container. "trigger" will look for a
 * property "container-trigger" on @node, which can be a string (full location
 * of the node to trigger) or a node to trigger.
 * "popup" will call node_popup_children() for all children of @node.
 * Finally "goto" will set @node as new current location of the active-list.
 *
 * For containers, if "trigger" was set it will be tried. If there was no
 * property "container-trigger" then if "popup" was set it'll be used, else if
 * "goto" was set it'll be used.
 */
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

    struct node_trigger_goto ntg;
    DonnaTask *t;
    DonnaTaskState state;

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
        if (trg_container == (guint) -1)
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
                    if (!donna_app_trigger_node (app, g_value_get_string (&v), FALSE, &err))
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
            gpointer _args[5] = { node, (gchar *) "all", NULL, NULL, NULL };
            return cmd_node_popup_children (task, app, _args);
        }
        else if (!(trg_container & TRG_GOTO))
            return DONNA_TASK_DONE;
    }

    ntg.app  = app;
    ntg.node = node;

    t = donna_task_new ((task_fn) nt_goto, &ntg, NULL);
    donna_task_set_visibility (t, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    if (!donna_app_run_task_and_wait (app, g_object_ref (t), task, &err))
    {
        g_prefix_error (&err, "Command 'node_trigger': "
                "Failed to set location of active-list: ");
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

/**
 * nodes_filter:
 * @nodes: (array): The nodes to filter
 * @filter: The filter to use
 * @tree: (allow-none): Name of the treeview to filter children through
 * @duplicate: (allow-none): Whether to duplicate the array @nodes or not
 *
 * Filters @nodes, removing from the array all nodes not matching @filter
 *
 * @filter is the string used to filter @nodes, which will be applied using
 * column options from @tree is specified, else the (non tree-specific)
 * defaults.
 *
 * If @duplicate is %TRUE the array of nodes will be duplicated, else nodes will
 * be removed from the array directly.
 *
 * <note><para>If you want to filter nodes from an array obtained e.g. from an
 * event, i.e. where you don't own the array and it could be used elsewhere, it
 * is important to set @duplicate so a copy is used.</para></note>
 *
 * See donna_app_filter_nodes() for more on node filtering.
 *
 * Returns: (array): The filtered array
 */
static DonnaTaskState
cmd_nodes_filter (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    const gchar *filter = args[1];
    DonnaTreeView *tree = args[2]; /* opt */
    gboolean dup_arr = GPOINTER_TO_INT (args[3]); /* opt */

    GPtrArray *arr;
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

    if (!donna_app_filter_nodes (app, arr, filter, tree, &err))
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

/**
 * nodes_io:
 * @nodes: (array): The source nodes for the operation
 * @io_type: The type of IO operation to perform
 * @dest: (allow-none): The destination of the operation
 * @new_name: (allow-none): The new name to use in the operation
 *
 * Performs the specified IO operation
 *
 * @io_type must be one of "copy", "move" or "delete"
 *
 * See donna_app_nodes_io_task() for more.
 *
 * Returns: (array) (allow-none): For copy/move operations, the resulting nodes
 * will be returned. For delete operation, there won't be no return value
 */
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
        if (io_types[c_io] != DONNA_IO_DELETE)
        {
            GValue *value;

            value = donna_task_grab_return_value (task);
            g_value_init (value, G_TYPE_PTR_ARRAY);
            g_value_set_boxed (value, g_value_get_boxed (donna_task_get_return_value (t)));
            donna_task_release_return_value (task);
        }
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

/**
 * nodes_remove_from:
 * @nodes: (array): Nodes to remove from @source
 * @source: Source where to removes @nodes from
 *
 * Removes @nodes from @source
 *
 * See donna_provider_remove_from_task() for more
 */
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

/**
 * task_cancel:
 * @node: The node of the task to cancel
 *
 * Cancel the task behind @node
 *
 * See donna_task_manager_cancel() for more
 */
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

/**
 * task_set_state:
 * @node: The node of the task
 * @state: The state to set
 *
 * Change the state of the task behind @node to @state
 *
 * @state must be one of "run", "pause", "cancel", "stop" or "wait"
 *
 * See donna_task_manager_set_state() for more
 */
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

/**
 * task_show_ui:
 * @node: Node of the task
 *
 * Shows the TasKUI (details) for the task
 *
 * See donna_task_manager_show_ui() for more
 */
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

/**
 * task_toggle:
 * @node: The node of the task
 *
 * Toggles the task behind @node (e.g. resume a paused task, pause a
 * running/waiting task, etc)
 *
 * Note that this can also be done by using node_trigger() as this is what is
 * done here, except with ensuring first that @node belongs in "task"
 */
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

/**
 * tasks_cancel:
 * @nodes: (array): The nodes of tasks to cancel
 *
 * Cancels all the tasks behind @nodes
 *
 * See donna_task_manager_cancel() for more
 */
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

/**
 * tasks_cancel_all:
 *
 * Cancels all tasks in task manager
 *
 * See donna_task_manager_cancel_all() for more
 */
static DonnaTaskState
cmd_tasks_cancel_all (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    donna_task_manager_cancel_all (donna_app_peek_task_manager (app));
    return DONNA_TASK_DONE;
}

/**
 * tasks_pre_exit:
 * @always_confirm: (allow-none): Whether to always ask for confirmation
 *
 * Intended to be used from event "pre-exit" to ask for confirmation
 *
 * See donna_task_manager_pre_exit() for more
 *
 * Returns: 1 to abort the event (i.e. user didn't confirm), else 0
 */
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

/**
 * tasks_switch:
 * @nodes: (array): Nodes representing the tasks to switch
 * @switch_on: (allow-none): 1 to switch tasks on, else switch them off
 * @fail_on_failure: (allow-none): 1 for the command to fail if at least one
 * state change request failed
 *
 * Switches the tasks behind @nodes according to @switch_on
 *
 * See donna_task_manager_switch_tasks() for more
 */
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

/**
 * terminal_add_tab:
 * @terminal: A terminal
 * @cmdline: The command line to execute inside the embedded terminal
 * @term_cmdline: (allow-none): The command line to start the embedded terminal
 * @workdir: (allow-none): Working directory for the terminal; Else (or if
 * @workdir is an empty string) the current directory will be used
 * @add_tab: (allow-none): Extra action to perform on the newly created tab
 *
 * Starts a new embedded terminal in @terminal, launching @cmdline
 *
 * See donna_terminal_add_tab() for more
 *
 * Returns: The ID of the newly created tab
 */
static DonnaTaskState
cmd_terminal_add_tab (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    const gchar *cmdline = args[1];
    const gchar *term_cmdline = args[2]; /* opt */
    const gchar *workdir = args[3]; /* opt */
    gchar *s_add_tab = args[4]; /* opt */

    const gchar *c_add_tab[] = { "nothing", "active", "focus" };
    DonnaTerminalAddTab add_tab[] = { DONNA_TERMINAL_NOTHING,
        DONNA_TERMINAL_MAKE_ACTIVE, DONNA_TERMINAL_FOCUS };
    gint c;

    GValue *value;
    guint id;

    if (s_add_tab)
    {
        c = _get_choice (c_add_tab, s_add_tab);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'terminal_add_tab': Invalid add_tab argument '%s': "
                    "Must be one of 'nothing', 'active' or 'focus'",
                    s_add_tab);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default: FOCUS */
        c = 2;

    if (workdir && *workdir == '\0')
        workdir = NULL;

    id = donna_terminal_add_tab (terminal, cmdline, term_cmdline, workdir,
            add_tab[c], &err);
    if (id == (guint) -1)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, (gint) id);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * terminal_get_active_page:
 * @terminal: A terminal
 *
 * Returns the page number of the active/current tab in @terminal
 *
 * See donna_terminal_get_active_page() for more
 *
 * Returns: The page number of the active tab
 */
static DonnaTaskState
cmd_terminal_get_active_page (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTerminal *terminal = args[0];

    GValue *value;
    gint page;

    page = donna_terminal_get_active_page (terminal);
    if (page == -1)
    {
        donna_task_set_error (task, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Command 'terminal_get_active_page': "
                "No active page in terminal '%s'",
                donna_terminal_get_name (terminal));
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, page);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * terminal_get_active_tab:
 * @terminal: A terminal
 *
 * Returns the tab ID of the active/current tab in @terminal
 *
 * See donna_terminal_get_active_tab() for more
 *
 * Returns: The tab ID of the active tab
 */
static DonnaTaskState
cmd_terminal_get_active_tab (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTerminal *terminal = args[0];

    GValue *value;
    guint id;

    id = donna_terminal_get_active_tab (terminal);
    if (id == (guint) -1)
    {
        donna_task_set_error (task, DONNA_TERMINAL_ERROR,
                DONNA_TERMINAL_ERROR_NOT_FOUND,
                "Command 'terminal_get_active_tab': "
                "No active tab in terminal '%s'",
                donna_terminal_get_name (terminal));
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, (gint) id);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * terminal_get_page:
 * @terminal: A terminal
 * @id: A tab ID
 *
 * Get the current page number for tab @id
 *
 * See donna_terminal_get_page() for more
 *
 * Returns: The page number of tab @id
 */
static DonnaTaskState
cmd_terminal_get_page (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    gint id = GPOINTER_TO_INT (args[1]);

    GValue *value;
    gint page;

    ensure_uint ("terminal_get_page", 2, "id", id);
    page = donna_terminal_get_page (terminal, (guint) id, &err);
    if (page == -1)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, page);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * terminal_get_tab:
 * @terminal: A terminal
 * @page: A page number
 *
 * Get the tab ID of page @page
 *
 * See donna_terminal_get_tab() for more
 *
 * Returns: The tab ID of page @page
 */
static DonnaTaskState
cmd_terminal_get_tab (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    gint page = GPOINTER_TO_INT (args[1]);

    GValue *value;
    guint id;

    id = donna_terminal_get_tab (terminal, page, &err);
    if (id == (guint) -1)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, (gint) id);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * terminal_remove_page:
 * @terminal: A terminal
 * @page: The page to remove
 *
 * Remove page @page from @terminal
 *
 * See donna_terminal_remove_page() for more
 */
static DonnaTaskState
cmd_terminal_remove_page (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    gint page = GPOINTER_TO_INT (args[1]);

    if (!donna_terminal_remove_page (terminal, page, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * terminal_remove_tab:
 * @terminal: A terminal
 * @id: The tab ID
 *
 * Remove tab @id from @terminal
 *
 * See donna_terminal_remove_tab() for more
 */
static DonnaTaskState
cmd_terminal_remove_tab (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    gint id = GPOINTER_TO_INT (args[1]);

    ensure_uint ("terminal_remove_tab", 2, "id", id);
    if (!donna_terminal_remove_tab (terminal, (guint) id, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * terminal_set_active_page:
 * @terminal: A terminal
 * @page: A page number; or -1 for last one
 * @no_focus: (allow-none): 1 not to set the focus to the embedded terminal
 *
 * Sets the active tab of @terminal to page @page
 *
 * See donna_terminal_set_active_page() for more
 */
static DonnaTaskState
cmd_terminal_set_active_page (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    gint page = GPOINTER_TO_INT (args[1]);
    gboolean no_focus = GPOINTER_TO_INT (args[2]); /* opt */

    if (!donna_terminal_set_active_page (terminal, page, no_focus, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * terminal_set_active_tab:
 * @terminal: A terminal
 * @id: A tab ID
 * @no_focus: (allow-none): 1 not to set the focus to the embedded terminal
 *
 * Sets the active tab of @terminal to tab @id
 *
 * See donna_terminal_set_active_tab for more
 */
static DonnaTaskState
cmd_terminal_set_active_tab (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTerminal *terminal = args[0];
    gint id = GPOINTER_TO_INT (args[1]);
    gboolean no_focus = GPOINTER_TO_INT (args[2]); /* opt */

    ensure_uint ("terminal_set_active_tab", 2, "id", id);
    if (!donna_terminal_set_active_tab (terminal, (guint) id, no_focus, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_abort:
 * @tree: A treeview
 *
 * Abort any running task to change @tree's location
 *
 * See donna_tree_view_abort() for more
 */
static DonnaTaskState
cmd_tv_abort (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    donna_tree_view_abort (tree);
    return DONNA_TASK_DONE;
}

/**
 * tv_activate_row:
 * @tree: A treeview
 * @rid: A #rowid
 *
 * Activates the row at @rowid
 *
 * See donna_tree_view_activate_row() for more
 */
static DonnaTaskState
cmd_tv_activate_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_activate_row (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_add_root:
 * @tree: A treeview
 * @node: The node to add as new root
 *
 * Adds a new root @node to @tree
 *
 * See donna_tree_view_add_root() for more
 */
static DonnaTaskState
cmd_tv_add_root (DonnaTask *task, DonnaApp *app, gpointer *args)
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

/**
 * tv_column_edit:
 * @tree: A treeview
 * @rowid: A #rowid
 * @col_name: The name of a column
 *
 * Start editing for column @col_name of row @rowid in @tree
 *
 * See donna_tree_view_column_edit() for more
 */
static DonnaTaskState
cmd_tv_column_edit (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gchar *col_name = args[2];

    if (!donna_tree_view_column_edit (tree, rid, col_name, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_column_refresh_nodes:
 * @tree: A treeview
 * @rowid: A #rowid
 * @to_focused: (allow-none): When 1 rows will be the range from @rowid to the
 * focused row
 * @column: Name of a column
 *
 * Refreshes all properties used by @column on specified nodes
 *
 * See donna_tree_view_column_refresh_nodes() for more
 */
static DonnaTaskState
cmd_tv_column_refresh_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gboolean to_focused = GPOINTER_TO_INT (args[2]); /* opt */
    const gchar *column = args[3];

    if (!donna_tree_view_column_refresh_nodes (tree, rid, to_focused, column, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_column_set_option:
 * @tree: A treeview
 * @column: The name of a column
 * @option: The name of the column option to set
 * @value: (allow-none): The value to set
 * @location: (allow-none): Save location for the option
 *
 * Sets the column option @option for @column in @tree to @value
 *
 * @location can be one of "memory", "current"," ask", "arrangement", "tree",
 * "mode", "default" or "save-location" It defaults to "save-location"
 *
 * See donna_tree_view_column_set_option() for more
 */
static DonnaTaskState
cmd_tv_column_set_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *column = args[1];
    const gchar *option = args[2];
    const gchar *value  = args[3]; /* opt */
    const gchar *s_l    = args[4]; /* opt */

    const gchar *c_s_l[] = { "memory", "current", "ask", "arrangement", "tree",
        "mode", "default", "save-location" };
    DonnaTreeViewOptionSaveLocation save_location[] = {
        DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_ARRANGEMENT,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_DEFAULT,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION };
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
                    "'mode', 'default' or 'save-location'",
                    s_l);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default: SAVE_LOCATION */
        c = 7;

    if (!donna_tree_view_column_set_option (tree, column, option, value,
                save_location[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_column_set_value:
 * @tree: A treeview
 * @rowid: A #rowid
 * @to_focused: (allow-none): If 1 then rows affected will be the range from
 * @rowid to the focused row
 * @column: Name of the column
 * @value: Value to set
 * @rid_ref: (allow-none): A #rowid to use as reference
 *
 * Set @value for the property handled by @column on the node(s) represented by
 * @rowid
 *
 * See donna_tree_view_column_set_value() for more
 */
static DonnaTaskState
cmd_tv_column_set_value (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gboolean to_focused = GPOINTER_TO_INT (args[2]); /* opt */
    const gchar *column = args[3];
    const gchar *value = args[4];
    DonnaRowId *rid_ref = args[5]; /* opt */

    if (!donna_tree_view_column_set_value (tree, rid, to_focused, column,
                value, rid_ref, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_context_get_nodes:
 * @tree: A treeview
 * @rowid: (allow-none): A #rowid to be used as reference
 * @column: (allow-none): A column name
 * @items: (allow-none): Items to load nodes from
 *
 * Returns the nodes to be used in context menu, as defined on @items
 *
 * See donna_tree_view_context_get_nodes() for more
 *
 * Returns: (array): The nodes to be used in a context menu
 */
static DonnaTaskState
cmd_tv_context_get_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rowid = args[1]; /* opt */
    const gchar *column = args[2]; /* opt */
    gchar *items = args[3]; /* opt */

    GPtrArray *nodes;
    GValue *value;

    nodes = donna_tree_view_context_get_nodes (tree, rowid, column, items, &err);
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

/**
 * tv_context_popup:
 * @tree: A treeview
 * @rowid: (allow-none): A #rowid to be used as reference
 * @column: (allow-none): A column name
 * @items: (allow-none): Items to load nodes from
 * @menus: (allow-none): Menu definition to use
 * @no_focus_grab: (allow-none): If %TRUE @tree won't grab focus first
 *
 * Gets the nodes to be used in context menu, as defined on @items, and show
 * them in a popup menu.
 *
 * See donna_tree_view_context_popup() for more
 */
static DonnaTaskState
cmd_tv_context_popup (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rowid = args[1]; /* opt */
    const gchar *column = args[2]; /* opt */
    gchar *items = args[3]; /* opt */
    const gchar *menus = args[4]; /* opt */
    gboolean no_focus_grab = GPOINTER_TO_INT (args[5]); /* opt */

    if (!donna_tree_view_context_popup (tree, rowid, column, items, menus,
                no_focus_grab, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_full_collapse:
 * @tree: A treeview
 * @rowid: A #rowid to full collapse
 *
 * Full collapse the row at @rowid
 *
 * See donna_tree_view_full_collapse() for more
 */
static DonnaTaskState
cmd_tv_full_collapse (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_full_collapse (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_full_expand:
 * @tree: A treeview
 * @rowid: A #rowid to full expand
 *
 * Full expand the row at @rowid
 *
 * See donna_tree_view_full_expand() for more
 */
static DonnaTaskState
cmd_tv_full_expand (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_full_expand (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_get_location:
 * @tree: A treeview
 *
 * Returns the current location of @tree; If @tree has no current location set,
 * the command will fail
 *
 * Returns: The node of the current location of @tree
 */
static DonnaTaskState
cmd_tv_get_location (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    DonnaNode *node;
    GValue *v;

    node = donna_tree_view_get_location (tree);
    if (!node)
    {
        donna_task_set_error (task, DONNA_TREE_VIEW_ERROR,
                DONNA_TREE_VIEW_ERROR_NOT_FOUND,
                "Command 'tv_get_location': TreeView '%s' has no current location",
                donna_tree_view_get_name (tree));
        return DONNA_TASK_FAILED;
    }

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

/**
 * tv_get_node_at_row:
 * @tree: A treeview
 * @rid: A #rowid
 *
 * Returns the node behind @rowid
 *
 * See donna_tree_view_get_node_at_row() for more
 *
 * Returns: The node behind @rowid
 */
static DonnaTaskState
cmd_tv_get_node_at_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

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

/**
 * tv_get_nodes:
 * @tree: A treeview
 * @rowid: A #rowid
 * @to_focused: (allow-none): If 1 then rows affected will be the range from
 * @rowid to the focused row
 *
 * Returns the nodes behind the row(s) at @rowid
 *
 * See donna_tree_view_get_nodes() for more
 *
 * Returns: (array): The nodes behind the specified row(s)
 */
static DonnaTaskState
cmd_tv_get_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
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

/**
 * tv_get_visual:
 * @tree: A treeview
 * @rowid: A #rowid
 * @visual: Which tree visual to get the value of
 * @source: Where to get the visual from
 *
 * Returns the value of specified tree visual for @rowid
 *
 * @visual must be one of "name", "icon", "box", "highlight" or "click_mode"
 * @source must be one of "any", "tree" or "node"
 *
 * See donna_tree_view_get_visual() for more
 *
 * Returns: The value of the tree visual
 */
static DonnaTaskState
cmd_tv_get_visual (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gchar *visual = args[2];
    gchar *source = args[3];

    const gchar *c_visual[] = { "name", "icon", "box", "highlight", "click_mode" };
    DonnaTreeVisual visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT,
        DONNA_TREE_VISUAL_CLICK_MODE };
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
                "Must be 'name', 'box', 'highlight' or 'click_mode'",
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

/**
 * tv_get_node_down:
 * @tree: A treeview
 * @level: (allow-none): Level wanted
 *
 * Returns a node from @tree's history that is a descendant of current location.
 *
 * See donna_tree_view_get_node_down() for more
 *
 * Returns: A node descendant of current location from @tree's history
 */
static DonnaTaskState
cmd_tv_get_node_down (DonnaTask *task, DonnaApp *app, gpointer *args)
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
        g_prefix_error (&err, "Command 'tv_get_node_down': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * tv_get_visual_filter:
 * @tree: A treeview
 *
 * Returns the current visual filter of @tree (Empty string if there's none)
 *
 * See donna_tree_view_get_visual_filter() for more
 *
 * Returns: The current visual filter on @tree
 */
static DonnaTaskState
cmd_tv_get_visual_filter (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    GValue *value;
    gchar *vf;

    vf = donna_tree_view_get_visual_filter (tree, &err);
    if (!vf && err)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    if (vf)
        g_value_take_string (value, vf);
    else
        g_value_take_string (value, g_strdup (""));
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * tv_go_down:
 * @tree: A treeview
 * @level: (allow-none): Level wanted (Defaults to 1)
 *
 * Changes location to that from @tree's history that is a descendant of current
 * location.
 *
 * See donna_tree_view_go_down() for more
 */
static DonnaTaskState
cmd_tv_go_down (DonnaTask *task, DonnaApp *app, gpointer *args)
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

/**
 * tv_get_node_root:
 * @tree: A treeview
 *
 * Returns the node of the root of the current branch
 *
 * See donna_tree_view_get_node_root() for more
 *
 * Returns: The node of the root of the current branch
 */
static DonnaTaskState
cmd_tv_get_node_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    DonnaNode *node;
    GValue *value;

    node = donna_tree_view_get_node_root (tree, &err);
    if (!node)
    {
        g_prefix_error (&err, "Command 'tv_get_node_root': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * tv_go_root:
 * @tree: A treeview
 *
 * Change location to that of the root of the current location
 *
 * See donna_tree_view_go_root() for more
 */
static DonnaTaskState
cmd_tv_go_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    if (!donna_tree_view_go_root (tree, &err))
    {
        g_prefix_error (&err, "Command 'tv_go_root': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_get_node_up:
 * @tree: A treeview
 * @level: (allow-none): Level wanted
 *
 * Returns a node that is the @level-nth ascendant of current location
 *
 * See donna_tree_view_get_node_up() for more
 *
 * Returns: A node, @level-nth ascendant of current location
 */
static DonnaTaskState
cmd_tv_get_node_up (DonnaTask *task, DonnaApp *app, gpointer *args)
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
        g_prefix_error (&err, "Command 'tv_get_node_up': ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/**
 * tv_go_up:
 * @tree: A treeview
 * @level: (allow-none): Level wanted; Defaults to 1
 * @set: (allow-none): For lists only: What to set on child after location
 * change
 *
 * Changes location to the @level-nth ascendant of current location
 *
 * @set can be one or more of "scroll", "focus" and "cursor"
 *
 * Note that to go the the root you must use -1 as @level, because when using 0
 * it will be reset to its default of 1.
 *
 * See donna_tree_view_go_up() for more
 */
static DonnaTaskState
cmd_tv_go_up (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gint level = GPOINTER_TO_INT (args[1]); /* opt */
    gchar *s_set = args[2]; /* opt */

    const gchar *c_set[] = { "scroll", "focus", "cursor" };
    DonnaTreeViewSet sets[] = { DONNA_TREE_VIEW_SET_SCROLL,
        DONNA_TREE_VIEW_SET_FOCUS, DONNA_TREE_VIEW_SET_CURSOR };
    DonnaTreeViewSet set;

    /* we cannot differentiate between the level arg being set to 0, and not
     * specified. So, we assume not set and default to 1.
     * In order to go up to the root, use a negative value */
    if (level == 0)
        level = 1;

    if (s_set)
        set = _get_flags (c_set, sets, s_set);
    else
        set = 0;

    if (set == (guint) -1)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Command 'tv_go_up': Invalid set argument '%s': "
                "Must be (a '+'-separated list of) 'scroll', 'focus' and/or 'cursor'",
                s_set);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_go_up (tree, level, set, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_goto_line:
 * @tree: A treeview
 * @set: Which element(s) to set
 * @rowid: A #rowid
 * @nb: (allow-none): Number of line/times to repeat the move
 * @nb_type: (allow-none): Define how to interpret @nb
 * @action: (allow-none): Action to perform on the selection
 * @to_focused: (allow-none): If 1 then rows affected will be the range from
 * @rowid to the focused row
 *
 * "Goes" to the specified row according to @set, updating selection as per
 * @action
 *
 * @set must be one or more of "scroll", "focus" and "cursor"
 *
 * @nb_type can be one of "repeat", "line", "percent" or "visible" Defaults to
 * "repeat"
 *
 * @action can be one of "select", "unselect", "invert" or "define" No defaults
 * (i.e. selection won't be affected)
 *
 * See donna_tree_view_goto_line() for more
 */
static DonnaTaskState
cmd_tv_goto_line (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *s_set = args[1];
    DonnaRowId *rid = args[2];
    gint nb = GPOINTER_TO_INT (args[3]); /* opt */
    gchar *nb_type = args[4]; /* opt */
    gchar *action = args[5]; /* opt */
    gboolean to_focused = GPOINTER_TO_INT (args[6]); /* opt */

    const gchar *c_set[] = { "scroll", "focus", "cursor" };
    DonnaTreeViewSet sets[] = { DONNA_TREE_VIEW_SET_SCROLL,
        DONNA_TREE_VIEW_SET_FOCUS, DONNA_TREE_VIEW_SET_CURSOR };
    const gchar *c_nb_type[] = { "repeat", "line", "percent", "visible" };
    DonnaTreeViewGoto nb_types[] = { DONNA_TREE_VIEW_GOTO_REPEAT,
        DONNA_TREE_VIEW_GOTO_LINE, DONNA_TREE_VIEW_GOTO_PERCENT,
        DONNA_TREE_VIEW_GOTO_VISIBLE };
    DonnaTreeViewSet set;
    const gchar *c_action[] = { "select", "unselect", "invert", "define" };
    DonnaSelAction actions[] = { DONNA_SEL_SELECT, DONNA_SEL_UNSELECT,
        DONNA_SEL_INVERT, DONNA_SEL_DEFINE };
    gint c_n;
    gint c_a;

    set = _get_flags (c_set, sets, s_set);
    if (set == (guint) -1)
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

    ensure_uint ("tv_goto_line", 4, "nb", nb);

    if (!donna_tree_view_goto_line (tree, set, rid, (guint) nb, nb_types[c_n],
                (c_a < 0) ? 0 : actions[c_a], to_focused, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

/**
 * tv_history_clear:
 * @tree: A treeview
 * @direction: (allow-none): Direction(s) to clear
 *
 * Clears @tree's history going @direction
 *
 * @direction can be one or more of "backward" and "forward" Defaults to
 * "backward+forward"
 *
 * See donna_tree_view_history_clear() for more
 */
static DonnaTaskState
cmd_tv_history_clear (DonnaTask *task, DonnaApp *app, gpointer *args)
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
        if (dir == (guint) -1)
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
        dir = DONNA_HISTORY_BOTH;

    if (!donna_tree_view_history_clear (tree, dir, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_history_get:
 * @tree: A treeview
 * @direction: (allow-none): Direction(s) to look into @tree's history
 * @nb: (allow-none): How many items to return
 *
 * Returns nodes representing items from @tree's history, e.g. to show in a
 * popup menu.
 *
 * @direction can be one or more of "backward" and "forward" Defaults to
 * "backward+forward"
 *
 * See donna_tree_view_history_get() for more
 *
 * Returns: (array): The nodes of the history items
 */
static DonnaTaskState
cmd_tv_history_get (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */
    gint nb = GPOINTER_TO_INT (args[2]); /* opt */

    const gchar *s_directions[] = { "backward", "forward" };
    DonnaHistoryDirection directions[] = { DONNA_HISTORY_BACKWARD,
        DONNA_HISTORY_FORWARD };
    guint dir;

    GValue *value;
    GPtrArray *arr;

    if (direction)
    {
        dir = _get_flags (s_directions, directions, direction);
        if (dir == (guint) -1)
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
        dir = DONNA_HISTORY_BOTH;

    ensure_uint ("tv_history_get", 3, "nb", nb);

    arr = donna_tree_view_history_get (tree, dir, (guint) nb, &err);
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

/**
 * tv_history_get_node:
 * @tree: A treeview
 * @direction: (allow-none): Direction to look into @tree's history
 * @nb: (allow-none): How many steps to go into history
 *
 * Get the node of the @nb-th item from @tree's history going @direction
 *
 * @direction can be one or more of "backward" and "forward" Defaults to
 * "backward"
 *
 * If @nb is not specified (or 0) it defaults to 1
 *
 * See donna_tree_view_history_get_node() for more
 *
 * Returns: The node for the history item
 */
static DonnaTaskState
cmd_tv_history_get_node (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */
    gint nb = GPOINTER_TO_INT (args[2]); /* opt */

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

    ensure_uint ("tv_history_get_node", 3, "nb", nb);
    if (nb == 0)
        nb = 1;

    node = donna_tree_view_history_get_node (tree, directions[dir], (guint) nb, &err);
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

/**
 * tv_history_move:
 * @tree: A treeview
 * @direction: (allow-none): Direction to look into @tree's history
 * @nb: (allow-none): How many steps to go into history
 *
 * Set current location by moving @nb steps into @tree's history going
 * @direction
 *
 * @direction can be one of "backward" or "forward" Defaults to "backward"
 *
 * If @nb is not specified (or 0) it defaults to 1
 *
 * See donna_tree_view_history_move() for more
 */
static DonnaTaskState
cmd_tv_history_move (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *direction = args[1]; /* opt */
    gint nb = GPOINTER_TO_INT (args[2]); /* opt */

    const gchar *s_directions[] = { "backward", "forward" };
    DonnaHistoryDirection directions[] = { DONNA_HISTORY_BACKWARD,
        DONNA_HISTORY_FORWARD };
    guint dir;

    if (direction)
    {
        dir = _get_flags (s_directions, directions, direction);
        if (dir == (guint) -1)
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

    ensure_uint ("tv_history_move", 3, "nb", nb);
    /* since 0 has no sense here, we'll just assume this was not specified, and
     * default to 1 */
    if (nb == 0)
        nb = 1;

    if (!donna_tree_view_history_move (tree, dir, (guint) nb, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_load_list_file:
 * @tree: A treeview
 * @file: Name of the file
 * @elements: (allow-none): Which elements to load from @file
 *
 * Loads the state of list @tree from list file @file, usually a file
 * previously saved using command tv_save_list_file()
 *
 * @elements can be one of more of "focus", "sort", "scroll" and "selection" No
 * defaults.
 *
 * See donna_tree_view_load_list_file() for more
 */
static DonnaTaskState
cmd_tv_load_list_file (DonnaTask *task, DonnaApp *app, gpointer *args)
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
                    "Command 'tv_load_list_file': Invalid elements : '%s'; "
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

/**
 * tv_load_tree_file:
 * @tree: A treeview
 * @file: Name of the file
 * @visuals: (allow-none): Which #tree-visuals to load from @file
 *
 * Loads the content of @tree from tree file @file, usually a file saved using
 * command tv_save_tree_file()
 *
 * @visuals can be one or more of "name", "icon", "box", "highlight",
 * "click_mode", or simply "all" to quickly refer to all of them.
 *
 * See donna_tree_view_load_tree_file() for more
 */
static DonnaTaskState
cmd_tv_load_tree_file (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *file = args[1];
    gchar *s_visuals = args[2]; /* opt */

    const gchar *_s_visuals[] = { "name", "icon", "box", "highlight", "click_mode",
        "all" };
    DonnaTreeVisual _visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT,
        DONNA_TREE_VISUAL_CLICK_MODE,
        DONNA_TREE_VISUAL_NAME | DONNA_TREE_VISUAL_ICON | DONNA_TREE_VISUAL_BOX
            | DONNA_TREE_VISUAL_HIGHLIGHT | DONNA_TREE_VISUAL_CLICK_MODE };
    guint visuals;

    if (s_visuals)
    {
        visuals = _get_flags (_s_visuals, _visuals, s_visuals);
        if (visuals == (guint) -1)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'tv_load_tree_file': Invalid visuals : '%s'; "
                    "Must be (a '+'-separated combination of) 'name', 'icon', "
                    "'box',' highlight', 'click_mode' and/or 'all'",
                    s_visuals);
            return DONNA_TASK_FAILED;
        }
    }
    else
        visuals = 0;

    if (!donna_tree_view_load_tree_file (tree, file, visuals, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_maxi_collapse:
 * @tree: A treeview
 * @rowid: A #rowid to maxi collapse
 *
 * Maxi collapse the row at @rowid
 *
 * See donna_tree_view_maxi_collapse() for more
 */
static DonnaTaskState
cmd_tv_maxi_collapse (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_maxi_collapse (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_maxi_expand:
 * @tree: A treeview
 * @rowid: A #rowid to maxi expand
 *
 * Maxi expand the row at @rowid
 *
 * See donna_tree_view_maxi_expand() for more
 */
static DonnaTaskState
cmd_tv_maxi_expand (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_maxi_expand (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_move_root:
 * @tree: A treeview
 * @rowid: A #rowid
 * @move: Number indication how to move the row
 *
 * Moves the root pointed to by @rowid by @move
 *
 * See donna_tree_view_move_root() for more
 */
static DonnaTaskState
cmd_tv_move_root (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gint move = GPOINTER_TO_INT (args[2]);

    if (!donna_tree_view_move_root (tree, rid, move, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_refresh:
 * @tree: A treeview
 * @mode: The refresh mode
 *
 * Refreshes @tree
 *
 * @mode must be one of "visible", "simple", "normal" or "reload"
 *
 * See donna_tree_view_refresh() for more
 */
static DonnaTaskState
cmd_tv_refresh (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *mode = args[1];

    const gchar *choices[] = { "visible", "simple", "normal", "reload" };
    DonnaTreeViewRefreshMode modes[] = { DONNA_TREE_VIEW_REFRESH_VISIBLE,
        DONNA_TREE_VIEW_REFRESH_SIMPLE, DONNA_TREE_VIEW_REFRESH_NORMAL,
        DONNA_TREE_VIEW_REFRESH_RELOAD };
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

/**
 * tv_remove_row:
 * @tree: A treeview
 * @rowid: A #rowid
 *
 * Removes the row at @rowid from @tree
 *
 * See donna_tree_view_remove_row() for more
 */
static DonnaTaskState
cmd_tv_remove_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_remove_row (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

/**
 * tv_reset_keys:
 * @tree: A treeview
 *
 * Reset keys for @tree
 *
 * See donna_tree_view_reset_keys() for more
 */
static DonnaTaskState
cmd_tv_reset_keys (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    donna_tree_view_reset_keys (tree);
    return DONNA_TASK_DONE;
}

/**
 * tv_save_list_file:
 * @tree: A treeview
 * @file: Name of the file
 * @elements: (allow-none): Which elements to save to @file
 *
 * Saves the state of list @tree into a list file, so it can be loaded back
 * later using command tv_load_list_file()
 *
 * @elements can be one of more of "focus", "sort", "scroll" and "selection" No
 * defaults.
 *
 * See donna_tree_view_save_list_file() for more
 */
static DonnaTaskState
cmd_tv_save_list_file (DonnaTask *task, DonnaApp *app, gpointer *args)
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
                    "Command 'tv_save_list_file': Invalid elements : '%s'; "
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

/**
 * tv_save_to_config:
 * @tree: A treeview
 * @elements: (allow-none): Which element to save
 *
 * Save current values of @elements to configuration
 *
 * See donna_tree_view_save_to_config() for more
 */
static DonnaTaskState
cmd_tv_save_to_config (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *elements = args[1]; /* opt */

    if (!donna_tree_view_save_to_config (tree, elements, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_save_tree_file:
 * @tree: A treeview
 * @file: Name of the file
 * @visuals: (allow-none): Which #tree-visuals to save to @file
 *
 * Saves the tree @tree into a tree file, so it can be loaded back later using
 * command tv_load_tree_file()
 *
 * @visuals can be one or more of "name", "icon", "box", "highlight",
 * "click_mode", or simply "all" to quickly refer to all of them.
 *
 * See donna_tree_view_save_tree_file() for more
 */
static DonnaTaskState
cmd_tv_save_tree_file (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *file = args[1];
    gchar *s_visuals = args[2]; /* opt */

    const gchar *_s_visuals[] = { "name", "icon", "box", "highlight", "click_mode",
        "all" };
    DonnaTreeVisual _visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT,
        DONNA_TREE_VISUAL_CLICK_MODE,
        DONNA_TREE_VISUAL_NAME | DONNA_TREE_VISUAL_ICON | DONNA_TREE_VISUAL_BOX
            | DONNA_TREE_VISUAL_HIGHLIGHT | DONNA_TREE_VISUAL_CLICK_MODE };
    guint visuals;

    if (s_visuals)
    {
        visuals = _get_flags (_s_visuals, _visuals, s_visuals);
        if (visuals == (guint) -1)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'tv_save_tree_file': Invalid visuals : '%s'; "
                    "Must be (a '+'-separated combination of) 'name', 'icon', "
                    "'box',' highlight', 'click_mode' and/or 'all'",
                    s_visuals);
            return DONNA_TASK_FAILED;
        }
    }
    else
        visuals = 0;

    if (!donna_tree_view_save_tree_file (tree, file, visuals, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_selection:
 * @tree: A treeview
 * @action: Which action to perform on the selection
 * @rowid: A #rowid
 * @to_focused: (allow-none): If 1 then rows affected will be the range from
 * @rowid to the focused row
 *
 * Affects the selection on @tree
 *
 * @action must be one of "select", "unselect", "invert" or "define"
 *
 * See donna_tree_view_selection() for more
 */
static DonnaTaskState
cmd_tv_selection (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *action = args[1];
    DonnaRowId *rid = args[2];
    gboolean to_focused = GPOINTER_TO_INT (args[3]); /* opt */

    const gchar *choices[] = { "select", "unselect", "invert", "define" };
    DonnaSelAction actions[] = { DONNA_SEL_SELECT, DONNA_SEL_UNSELECT,
        DONNA_SEL_INVERT, DONNA_SEL_DEFINE };
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

/**
 * tv_selection_nodes:
 * @tree: A treeview
 * @action: Which action to perform on the selection
 * @nodes: (array): The nodes to perform the action onto
 *
 * Affects the selection on @tree using @nodes
 *
 * @action must be one of "select", "unselect", "invert" or "define"
 *
 * See donna_tree_view_selection_nodes() for more
 */
static DonnaTaskState
cmd_tv_selection_nodes (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *action = args[1];
    GPtrArray *nodes = args[2];

    const gchar *choices[] = { "select", "unselect", "invert", "define" };
    DonnaSelAction actions[] = { DONNA_SEL_SELECT, DONNA_SEL_UNSELECT,
        DONNA_SEL_INVERT, DONNA_SEL_DEFINE };
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

/**
 * tv_set_columns:
 * @tree: A treeview
 * @columns: A comma-separated list of columns
 *
 * Sets the columns of @tree
 *
 * See donna_tree_view_set_columns() for more
 */
static DonnaTaskState
cmd_tv_set_columns (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *columns = args[1];

    if (!donna_tree_view_set_columns (tree, columns, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_set_cursor:
 * @tree: A treeview
 * @rowid: A #rowid
 * @no_scroll: (allow-none): Set to 1 to disable any scrolling
 *
 * Sets cursor on @rowid
 *
 * See donna_tree_view_set_cursor() for more
 */
static DonnaTaskState
cmd_tv_set_cursor (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gboolean no_scroll = GPOINTER_TO_INT (args[2]);

    if (!donna_tree_view_set_cursor (tree, rid, no_scroll, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_set_focus:
 * @tree: A treeview
 * @rowid: A #rowid
 *
 * Sets focus on @rowid
 *
 * See donna_tree_view_set_focus() for more
 */
static DonnaTaskState
cmd_tv_set_focus (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];

    if (!donna_tree_view_set_focus (tree, rid, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_set_key_mode:
 * @tree: A treeview
 * @key_mode: The key mode to set
 *
 * Sets @key_mode as @tree's key mode
 *
 * See donna_tree_view_set_key_mode() for more
 */
static DonnaTaskState
cmd_tv_set_key_mode (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];
    gchar *key_mode = args[1];

    donna_tree_view_set_key_mode (tree, key_mode);
    return DONNA_TASK_DONE;
}

/**
 * tv_set_location:
 * @tree: A treeview
 * @node: A node
 *
 * Sets @node as new location of @tree
 *
 * See donna_tree_view_set_location() for more
 */
static DonnaTaskState
cmd_tv_set_location (DonnaTask *task, DonnaApp *app, gpointer *args)
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

/**
 * tv_set_option:
 * @tree: A treeview
 * @option: The option name
 * @value: (allow-none): The new value to set
 * @location: (allow-none): The location to save the value to
 *
 * Sets @value as new value of @tree's option @option
 *
 * @location can be on of "memory", "current", "ask", "tree", "mode" and
 * "save-location" Defaults to "save-location"
 *
 * See donna_tree_view_set_option() for more
 */
static DonnaTaskState
cmd_tv_set_option (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *option = args[1];
    const gchar *value  = args[2]; /* opt */
    const gchar *s_l    = args[3]; /* opt */

    const gchar *c_s_l[] = { "memory", "current", "ask", "tree", "mode",
        "save-location" };
    DonnaTreeViewOptionSaveLocation save_location[] = {
        DONNA_TREE_VIEW_OPTION_SAVE_IN_MEMORY,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_CURRENT,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_ASK,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_TREE,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_MODE,
        DONNA_TREE_VIEW_OPTION_SAVE_IN_SAVE_LOCATION
    };
    gint c;

    if (s_l)
    {
        c = _get_choice (c_s_l, s_l);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot set tree option, invalid save location: '%s'; "
                    "Must be 'memory', 'current', 'ask', 'tree', 'mode' "
                    "or 'save-location'",
                    s_l);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default: USE_DEFAULT */
        c = 5;

    if (!donna_tree_view_set_option (tree, option, value, save_location[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_set_second_sort:
 * @tree: A treeview
 * @column: Name of the column
 * @order: (allow-none): The sort order
 *
 * Set @tree's second sort order on @column (using order @order)
 *
 * @order can be one of "asc", "desc" or "unknown" Defaults to "unknown"
 *
 * See donna_tree_view_set_second_sort_order() for more
 */
static DonnaTaskState
cmd_tv_set_second_sort (DonnaTask *task, DonnaApp *app, gpointer *args)
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
                    "Command 'tv_set_second_sort': Invalid sort order '%s'; "
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

/**
 * tv_set_sort:
 * @tree: A treeview
 * @column: Name of the column
 * @order: (allow-none): The sort order
 *
 * Set @tree's sort order on @column (using order @order)
 *
 * @order can be one of "asc", "desc" or "unknown" Defaults to "unknown"
 *
 * See donna_tree_view_set_sort_order() for more
 */
static DonnaTaskState
cmd_tv_set_sort (DonnaTask *task, DonnaApp *app, gpointer *args)
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
                    "Command 'tv_set_sort': Invalid sort order '%s'; "
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

/**
 * tv_set_visual:
 * @tree: A treeview
 * @rowid: A #rowid
 * @visual: Which visual to set
 * @value: The value to set @visual to
 *
 * Sets tree visual @visual for @rowid to @value
 *
 * @visual must be one of "name", "icon", "box", "highlight" or "click_mode"
 *
 * See donna_tree_view_set_visual() for more
 */
static DonnaTaskState
cmd_tv_set_visual (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
    gchar *visual = args[2];
    gchar *value = args[3];

    const gchar *choices[] = { "name", "icon", "box", "highlight", "click_mode" };
    DonnaTreeVisual visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT,
        DONNA_TREE_VISUAL_CLICK_MODE };
    gint c;

    c = _get_choice (choices, visual);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'icon', 'box', 'highlight' or 'click_mode'",
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

/**
 * tv_set_visual_filter:
 * @tree: A treeview
 * @filter: (allow-none): A filter to set as VF (or nothing to unset any current
 * VF)
 * @toggle: (allow-none): 1 to toggle, i.e. remove the VF is already set to
 * @filter
 *
 * Sets the current visual filter
 *
 * Note that if @filter is an empty string, it will work as if none was
 * specified, i.e. unset any current VF.
 *
 * See donna_tree_view_set_visual_filter() for more
 */
static DonnaTaskState
cmd_tv_set_visual_filter (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *filter = args[1]; /* opt */
    gboolean toggle = GPOINTER_TO_INT (args[2]); /* opt */

    /* special: if filter is an empty string, we treat it as NULL, so that user
     * can use an empty string (which is an invalid filter) to mean turn it off;
     * which can be useful when said filter is e.g. a command's return value */
    if (streq (filter, ""))
        filter = NULL;

    if (!donna_tree_view_set_visual_filter (tree, filter, toggle, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_start_interactive_search:
 * @tree: A treeview
 *
 * Start interactive search on @tree
 *
 * See donna_tree_view_start_interactive_search()
 */
static DonnaTaskState
cmd_tv_start_interactive_search (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    DonnaTreeView *tree = args[0];

    donna_tree_view_start_interactive_search (tree);
    return DONNA_TASK_DONE;
}

/**
 * tv_toggle_column:
 * @tree: A treeview
 * @column: A column name
 *
 * Toggles column @column in @tree
 *
 * See donna_tree_view_toggle_column() for more
 */
static DonnaTaskState
cmd_tv_toggle_column (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    const gchar *column = args[1];

    if (!donna_tree_view_toggle_column (tree, column, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

/**
 * tv_toggle_row:
 * @tree: A treeview
 * @rowid: A #rowid
 * @toggle: Which toggle to perform
 *
 * Toggles @rowid accoding to @toggle
 *
 * @toggle must be one of "standard", "full" or "maxi"
 *
 * See donna_tree_view_toggle_row() for more
 */
static DonnaTaskState
cmd_tv_toggle_row (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaRowId *rid = args[1];
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

/**
 * void:
 * @Param1: (allow-none): Argument
 * @Param2: (allow-none): Argument
 * @Param3: (allow-none): Argument
 * @Param4: (allow-none): Argument
 * @Param5: (allow-none): Argument
 * @Param6: (allow-none): Argument
 * @Param7: (allow-none): Argument
 * @Param8: (allow-none): Argument
 *
 * Does nothing.
 *
 * This is intended to be used in a trigger, to run more than one command. For
 * example, using:
 * <programlisting>
 * command:void (@foo (), @bar ())
 * </programlisting>
 *
 * Will run command foo (to get its return value, even if it doesn't return
 * anything), and then run command bar (unless foo failed/was cancelled)
 *
 * If you need to run more than 8 commands, you might wanna consider using
 * command exec() and a script. Or just use recursion.
 */
static DonnaTaskState
cmd_void (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    return DONNA_TASK_DONE;
}



#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_type) \
command.name           = g_strdup (#cmd_name); \
command.argc           = (guint) cmd_argc; \
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
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (config_save, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_set_boolean, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_set_int, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

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
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_try_get_boolean, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (config_try_get_int, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (config_try_get_string, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (exec, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (focus_move, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (focus_set, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (intref_free, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
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
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW | DONNA_ARG_IS_OPTIONAL;
    add_command (node_popup_children, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (node_trigger, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW | DONNA_ARG_IS_OPTIONAL;
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
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (terminal_add_tab, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    add_command (terminal_get_active_page, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    add_command (terminal_get_active_tab, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (terminal_get_page, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (terminal_get_tab, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_INT);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (terminal_remove_page, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (terminal_remove_tab, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (terminal_set_active_page, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TERMINAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (terminal_set_active_tab, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_abort, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_activate_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (tv_add_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_column_edit, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_column_refresh_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_column_set_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_column_set_value, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_context_get_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_context_popup, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_full_collapse, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_full_expand, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_get_location, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_get_node_at_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_get_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_get_visual, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_get_node_down, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_get_visual_filter, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_STRING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_go_down, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_get_node_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_go_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_get_node_up, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_go_up, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_goto_line, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_history_clear, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_history_get, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_history_get_node, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_history_move, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_load_list_file, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_load_tree_file, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_maxi_collapse, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_maxi_expand, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT;
    add_command (tv_move_root, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_refresh, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_remove_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_reset_keys, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_save_list_file, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_save_to_config, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_save_tree_file, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_selection, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    add_command (tv_selection_nodes, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_set_columns, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_set_cursor, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    add_command (tv_set_focus, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_set_key_mode, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (tv_set_location, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_set_option, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_set_second_sort, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_set_sort, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_set_visual, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tv_set_visual_filter, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    add_command (tv_start_interactive_search, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_toggle_column, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREE_VIEW;
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (tv_toggle_row, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
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
