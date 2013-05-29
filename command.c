
#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "macros.h"
#include "debug.h"

static DonnaTaskState   cmd_set_focus       (DonnaTask *task, GPtrArray *args);
static DonnaTaskState   cmd_set_cursor      (DonnaTask *task, GPtrArray *args);
static DonnaTaskState   cmd_selection       (DonnaTask *task, GPtrArray *args);

static DonnaCommandDef commands[] = {
    {
        .command        = DONNA_COMMAND_SET_FOCUS,
        .name           = "set_focus",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_set_focus
    },
    {
        .command        = DONNA_COMMAND_SET_CURSOR,
        .name           = "set_cursor",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_set_cursor
    },
    {
        .command        = DONNA_COMMAND_SELECTION,
        .name           = "selection",
        .argc           = 4,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_ROW_ID, DONNA_ARG_TYPE_INT },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_selection
    },
};
static guint nb_commands = sizeof (commands) / sizeof (commands[0]);

/* shared private API */

#define skip_blank(s)   for ( ; isblank (*s); ++s) ;

DonnaCommandDef *
_donna_command_init_parse (gchar     *cmdline,
                           gchar    **first_arg,
                           gchar    **end,
                           GError   **error)
{
    gchar  c;
    gchar *s;
    guint  i;

    for (s = cmdline; isalnum (*s) || *s == '_'; ++s)
        ;
    c  = *s;
    *s = '\0';
    for (i = 0; i < nb_commands; ++i)
        if (streq (commands[i].name, cmdline))
            break;

    if (i >= nb_commands)
    {
        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_NOT_FOUND,
                "Command '%s' does not exists", cmdline);
        *s = c;
        return NULL;
    }
    *s = c;

    skip_blank (s);
    if (*s != '(')
    {
        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Command '%s': arguments not found, missing '('",
                commands[i].name);
        return NULL;
    }

    *first_arg = s;
    if (!_donna_command_get_next_arg (first_arg, end, error))
        return NULL;

    return &commands[i];
}

gboolean
_donna_command_get_next_arg (gchar  **arg,
                             gchar  **end,
                             GError **error)
{
    gboolean in_arg = FALSE;
    gchar *s;

    if (**arg == ',' || **arg == '(')
    {
        /* we were on the separator, or opening for the 1st arg. Skip it &
         * blanks */
        in_arg = TRUE;
        ++*arg;
        skip_blank (*arg);
    }
    else
    {
        if (**arg == '"')
            /* we were on the ending quote, move on */
            ++*arg;
        skip_blank (*arg);
        if (**arg == ',')
        {
            /* found the separator. Skit it & blanks */
            in_arg = TRUE;
            ++*arg;
            skip_blank (*arg);
        }
        else if (**arg == ')')
        {
            *arg = *end = NULL;
            return TRUE;
        }
        else
        {
            g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                    "Missing argument separator ',' or ')'");
            return FALSE;
        }
    }

    if (**arg == '"')
    {
        for (s = ++*arg; ; )
        {
            gint i;

            for ( ; *s != '"' && *s != '\0'; ++s)
                ;
            if (*s == '\0')
            {
                g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                        "Missing ending quote");
                return FALSE;
            }
            /* check for escaped quotes */
            for (i = 0; s[i - 1] == '\\'; --i)
                ;
            if ((i % 2) == 0)
                break;
            ++s;
        }
        *end = s;
        return TRUE;
    }

    if (**arg == ')')
    {
        *arg = *end = NULL;
        if (!in_arg)
            return TRUE;
        else
        {
            g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                    "Missing value before ')'");
            return FALSE;
        }
    }

    for (s = *end = *arg; *s != ',' && *s != ')' && *s != '\0'; ++s)
        if (!isblank (*s))
            *end = s;
    if (**end == '\0')
    {
        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Missing argument separator ',' or ')'");
        return FALSE;
    }
    ++*end;
    return TRUE;
}

gboolean
_donna_command_convert_arg (DonnaApp        *app,
                            DonnaArgType     type,
                            gboolean         from_string,
                            gpointer         sce,
                            gpointer        *dst,
                            GError         **error)
{
    switch (type)
    {
        case DONNA_ARG_TYPE_INT:
            if (from_string)
                * (gint *) dst = g_ascii_strtoll (sce, NULL, 10);
            else
                *dst = g_strdup_printf ("%d", GPOINTER_TO_INT (sce));
            break;

        case DONNA_ARG_TYPE_STRING:
        case DONNA_ARG_TYPE_PATH:
        /* PATH is treated as a string, because it will be. The reason we keep
         * it as a string is to allow donna-specific things that we otherwise
         * couldn't convert (w/out the tree), such as ":last" or ":prev" */
            *dst = g_strdup (sce);
            break;

        case DONNA_ARG_TYPE_TREEVIEW:
            if (from_string)
            {
                if (streq (sce, ":active"))
                    g_object_get (app, "active-list", dst, NULL);
                else
                {
                    *dst = donna_app_get_treeview (app, sce);
                    if (!*dst)
                    {
                        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_NOT_FOUND,
                                "Treeview '%s' not found", (gchar *) sce);
                        return FALSE;
                    }
                }
            }
            else
                *dst = g_strdup (donna_tree_view_get_name (sce));
            break;

        case DONNA_ARG_TYPE_NODE:
            if (from_string)
            {
                DonnaTask *task = donna_app_get_node_task (app, sce);
                if (!task)
                {
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                            "Invalid argument, can't get node for '%s'",
                            (gchar *) sce);
                    return FALSE;
                }
                donna_task_set_can_block (g_object_ref_sink (task));
                donna_app_run_task (app, task);
                if (donna_task_get_state (task) == DONNA_TASK_DONE)
                    *dst = g_value_dup_object (donna_task_get_return_value (task));
                else
                {
                    if (error)
                        *error = g_error_copy (donna_task_get_error (task));
                    g_object_unref (task);
                    return FALSE;
                }
                g_object_unref (task);
            }
            else
                *dst = donna_node_get_full_location (sce);
            break;

        case DONNA_ARG_TYPE_ROW:
            if (from_string)
            {
                DonnaTreeRow *row = g_new (DonnaTreeRow, 1);
                if (sscanf (sce, "[%p;%p]", &row->node, &row->iter) != 2)
                {
                    g_free (row);
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                            "Invalid argument syntax for TREE_ROW");
                    return FALSE;
                }
                *dst = row;
            }
            else
            {
                DonnaTreeRow *row = sce;
                *dst = g_strdup_printf ("[%p;%p]", row->node, row->iter);
            }
            break;

        case DONNA_ARG_TYPE_ROW_ID:
            if (from_string)
            {
                DonnaTreeRowId *rid = g_new (DonnaTreeRowId, 1);
                gchar *s = sce;

                if (*s == '[')
                {
                    DonnaTreeRow *row = g_new (DonnaTreeRow, 1);
                    if (sscanf (sce, "[%p;%p]", &row->node, &row->iter) != 2)
                    {
                        g_free (row);
                        g_free (rid);
                        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                                "Invalid argument syntax TREE_ROW for TREE_ROW_ID");
                        return FALSE;
                    }
                    rid->type = DONNA_ARG_TYPE_ROW;
                    rid->ptr  = row;
                }
                else if (*s == ':' || (*s >= '0' && *s <= '9'))
                {
                    rid->type = DONNA_ARG_TYPE_PATH;
                    rid->ptr  = g_strdup (sce);
                }
                else
                {
                    DonnaTask *task = donna_app_get_node_task (app, sce);
                    if (!task)
                    {
                        g_free (rid);
                        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                                "Invalid argument, can't get node for '%s'",
                                (gchar *) sce);
                        return FALSE;
                    }
                    donna_task_set_can_block (g_object_ref_sink (task));
                    donna_app_run_task (app, task);
                    if (donna_task_get_state (task) == DONNA_TASK_DONE)
                        rid->ptr = g_value_dup_object (donna_task_get_return_value (task));
                    else
                    {
                        if (error)
                            *error = g_error_copy (donna_task_get_error (task));
                        g_object_unref (task);
                        g_free (rid);
                        return FALSE;
                    }
                    g_object_unref (task);
                    rid->type = DONNA_ARG_TYPE_NODE;
                }
                *dst = rid;
            }
            else
            {
                /* ROW_ID cannot be used on return value */
                g_warning ("convert_arg() called for DONNA_ARG_TYPE_ROW_ID");
                g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                        "Invalid argument type");
                return FALSE;
            }
            break;

        case DONNA_ARG_TYPE_NOTHING:
            if (from_string)
            {
                /* NOTHING cannot be used on args */
                g_warning ("convert_arg() called for DONNA_ARG_TYPE_NOTHING");
                g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                        "Invalid argument type");
                return FALSE;
            }
            else
                *dst = NULL;
            break;
    }
    return TRUE;
}

void
_donna_command_free_args (DonnaCommandDef *command, GPtrArray *arr)
{
    guint i;

    /* we use arr->len and not command->argc because the array might not be
     * fully filled, in case of error halfway through parsing/converting.
     * We start at 1 because pdata[0] is NULL or the "parent task" */
    for (i = 1; i < arr->len; ++i)
    {
        switch (command->arg_type[i - 1])
        {
            case DONNA_ARG_TYPE_TREEVIEW:
            case DONNA_ARG_TYPE_NODE:
                g_object_unref (arr->pdata[i]);
                break;

            case DONNA_ARG_TYPE_NOTHING:
            case DONNA_ARG_TYPE_INT:
                break;

            case DONNA_ARG_TYPE_STRING:
            case DONNA_ARG_TYPE_ROW:
            case DONNA_ARG_TYPE_PATH:
                g_free (arr->pdata[i]);
                break;

            case DONNA_ARG_TYPE_ROW_ID:
                {
                    DonnaTreeRowId *rowid;

                    rowid = arr->pdata[i];
                    if (rowid->type == DONNA_ARG_TYPE_NODE)
                        g_object_unref (rowid->ptr);
                    else
                        g_free (rowid->ptr);

                    g_free (rowid);
                }
                break;
        }
    }
    g_ptr_array_unref (arr);
}

DonnaTaskState
_donna_command_run (DonnaTask *task, struct _donna_command_run *cr)
{
    GError *err = NULL;
    DonnaCommandDef *command;
    DonnaTask *cmd_task;
    gchar *start, *end;
    gchar  c;
    guint  i;
    GPtrArray *arr;
    DonnaTaskState ret;

    command = _donna_command_init_parse (cr->cmdline, &start, &end, &err);
    if (!command)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    arr = g_ptr_array_sized_new (command->argc + 1);
    g_ptr_array_add (arr, task);
    for (i = 0; i < command->argc; ++i)
    {
        gpointer ptr;

        c = *end;
        *end = '\0';
        if (!_donna_command_convert_arg (cr->app, command->arg_type[i], TRUE,
                    start, &ptr, &err))
        {
            g_prefix_error (&err, "Command '%s', argument %d: ",
                    command->name, i + 1);
            donna_task_take_error (task, err);
            *end = c;
            _donna_command_free_args (command, arr);
            return DONNA_TASK_FAILED;
        }
        *end = c;
        g_ptr_array_add (arr, ptr);

        /* get next arg. should succeed even when there are no more args */
        start = end;
        if (!_donna_command_get_next_arg (&start, &end, &err))
        {
            if (i + 2 > command->argc)
                g_prefix_error (&err, "Command '%s', too many arguments; ",
                        command->name);
            else
                g_prefix_error (&err, "Command '%s', argument %d: ",
                        command->name, i + 2);
            donna_task_take_error (task, err);
            _donna_command_free_args (command, arr);
            return DONNA_TASK_FAILED;
        }
        else if (!start && i + 1 < command->argc)
        {
            donna_task_set_error (task, COMMAND_ERROR, COMMAND_ERROR_MISSING_ARG,
                    "Command '%s': missing argument %d/%d",
                    command->name, i + 2, command->argc);
            _donna_command_free_args (command, arr);
            return DONNA_TASK_FAILED;
        }
    }

    if (i == 0)
    {
        /* special case for commands w/out args. make sure nothing was
         * specified */
        for ( ; start < end; ++start)
            if (!isblank (*start))
            {
                donna_task_set_error (task, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                        "Command '%s', too many arguments",
                        command->name);
                _donna_command_free_args (command, arr);
                return DONNA_TASK_FAILED;
            }

        /* parse to go to the end */
        if (!_donna_command_get_next_arg (&start, &end, &err))
        {
            g_prefix_error (&err, "Command '%s', too many arguments: ",
                    command->name);
            donna_task_take_error (task, err);
            _donna_command_free_args (command, arr);
            return DONNA_TASK_FAILED;
        }
    }

    if (start != NULL)
    {
        donna_task_set_error (task, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Command '%s', too many arguments",
                command->name);
        _donna_command_free_args (command, arr);
        return DONNA_TASK_FAILED;
    }

    /* run the command */
    cmd_task = donna_task_new ((task_fn) command->cmd_fn, arr, NULL);
    DONNA_DEBUG (TASK,
            donna_task_take_desc (cmd_task, g_strdup_printf (
                    "run command: %s", cr->cmdline)));
    donna_task_set_visibility (cmd_task, command->visibility);
    donna_task_set_can_block (g_object_ref_sink (cmd_task));
    donna_app_run_task (cr->app, cmd_task);
    donna_task_wait_for_it (cmd_task);
    ret = donna_task_get_state (cmd_task);
    /* because the "parent task" (task) was given to cmd_task as args[0] (in
     * arr) it is in that task that any error/return value will have been set */
    g_object_unref (cmd_task);

    /* free args */
    _donna_command_free_args (command, arr);

    _donna_command_free_cr (cr);
    return ret;
}

void
_donna_command_free_cr (struct _donna_command_run *cr)
{
    g_free (cr->cmdline);
    g_free (cr);
}


/* commands */

#define task_for_ret_err()  ((args->pdata[0]) ? args->pdata[0] : task)

static DonnaTaskState
cmd_set_focus (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

#ifdef GTK_IS_JJK
    if (!donna_tree_view_set_focus (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }
#else
    donna_task_set_error (task_for_ret_err (), COMMAND_ERROR, COMMAND_ERROR_OTHER,
            "Command 'set_focus' isn't supported with vanilla GTK+");
    return DONNA_TASK_FAILED;
#endif

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_set_cursor (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_set_cursor (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_selection (DonnaTask *task, GPtrArray *args)
{
    GError              *err = NULL;
    DonnaTreeSelAction   action;

    if (streq ("select", args->pdata[2]))
        action = DONNA_TREE_SEL_SELECT;
    else if (streq ("unselect", args->pdata[2]))
        action = DONNA_TREE_SEL_UNSELECT;
    else if (streq ("invert", args->pdata[2]))
        action = DONNA_TREE_SEL_INVERT;
    else
    {
        donna_task_set_error (task_for_ret_err (),
                COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Invalid argument 'action': '%s', expected 'select', 'unselect' or 'invert'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_selection (args->pdata[1], action, args->pdata[3],
                (gboolean) GPOINTER_TO_INT (args->pdata[4]),
                &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}