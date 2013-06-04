
#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "task-manager.h"
#include "macros.h"
#include "debug.h"

static DonnaTaskState   cmd_node_activate                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_task_set_state                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_task_toggle                     (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_activate_row               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_edit_column                (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_full_collapse              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_full_expand                (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_get_visual                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_maxi_collapse              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_maxi_expand                (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_selection                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_cursor                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_focus                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_visual                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_toggle_row                 (DonnaTask *task,
                                                             GPtrArray *args);

static DonnaCommand commands[] = {
    {
        .name           = "node_activate",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_NODE, DONNA_ARG_TYPE_INT },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_node_activate
    },
    {
        .name           = "task_set_state",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_NODE, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_task_set_state
    },
    {
        .name           = "task_toggle",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_NODE },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_task_toggle
    },
    {
        .name           = "tree_activate_row",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_activate_row
    },
    {
        .name           = "tree_edit_column",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID,
            DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_edit_column
    },
    {
        .name           = "tree_full_collapse",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_full_collapse
    },
    {
        .name           = "tree_full_expand",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_full_expand
    },
    {
        .name           = "tree_get_visual",
        .argc           = 4,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID,
            DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_STRING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_get_visual
    },
    {
        .name           = "tree_maxi_collapse",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_maxi_collapse
    },
    {
        .name           = "tree_maxi_expand",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_maxi_expand
    },
    {
        .name           = "tree_selection",
        .argc           = 4,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_ROW_ID, DONNA_ARG_TYPE_INT },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_selection
    },
    {
        .name           = "tree_set_cursor",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_set_cursor
    },
    {
        .name           = "tree_set_focus",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_set_focus
    },
    {
        .name           = "tree_set_visual",
        .argc           = 4,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID,
            DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_set_visual
    },
    {
        .name           = "tree_toggle_row",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID,
            DONNA_ARG_TYPE_STRING},
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_toggle_row
    },
};
static guint nb_commands = sizeof (commands) / sizeof (commands[0]);

#define skip_blank(s)   for ( ; isblank (*s); ++s) ;

static gboolean
get_next_arg (gchar  **arg,
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

/* shared private API */

DonnaCommand *
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
    if (!get_next_arg (first_arg, end, error))
        return NULL;

    return &commands[i];
}

gboolean
_donna_command_get_next_arg (DonnaCommand    *command,
                             guint            i,
                             gchar          **start,
                             gchar          **end,
                             GError         **error)
{
    /* get next arg. should succeed even when there are no more args */
    *start = *end;
    if (!get_next_arg (start, end, error))
    {
        if (i + 2 > command->argc)
            g_prefix_error (error, "Command '%s', too many arguments; ",
                    command->name);
        else
            g_prefix_error (error, "Command '%s', argument %d: ",
                    command->name, i + 2);
        return FALSE;
    }
    else if (!*start && i + 1 < command->argc)
    {
        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_MISSING_ARG,
                "Command '%s': missing argument %d/%d",
                command->name, i + 2, command->argc);
        return FALSE;
    }
    else
        return TRUE;
}

gboolean
_donna_command_checks_post_parsing (DonnaCommand    *command,
                                    guint            i,
                                    gchar           *start,
                                    gchar           *end,
                                    GError         **error)
{
    if (i == 0)
    {
        /* special case for commands w/out args. make sure nothing was
         * specified */
        for ( ; start < end; ++start)
            if (!isblank (*start))
            {
                g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                        "Command '%s', too many arguments",
                        command->name);
                return FALSE;
            }

        /* parse to go to the end */
        if (!get_next_arg (&start, &end, error))
        {
            g_prefix_error (error, "Command '%s', too many arguments: ",
                    command->name);
            return FALSE;
        }
    }

    if (start != NULL)
    {
        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Command '%s', too many arguments",
                command->name);
        return FALSE;
    }

    return TRUE;
}

gboolean
_donna_command_convert_arg (DonnaApp        *app,
                            DonnaArgType     type,
                            gboolean         from_string,
                            gboolean         can_block,
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
                if (!can_block)
                {
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_MIGHT_BLOCK,
                            "Converting argument would required to use a (possibly blocking) task");
                    return FALSE;
                }

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
                    if (!can_block)
                    {
                        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_MIGHT_BLOCK,
                                "Converting argument would required to use a (possibly blocking) task");
                        return FALSE;
                    }

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
_donna_command_free_args (DonnaCommand *command, GPtrArray *arr)
{
    guint i;

    /* we use arr->len because the array might not be fuly filled, in case of
     * error halfway through parsing/converting; We *also* use command->argc
     * because the arr sent to the command contains an extra pointer, to
     * DonnaApp in case the command needs it (e.g. to access config, etc).
     * And we start at 1 because pdata[0] is NULL or the "parent task" */
    for (i = 1; i < arr->len && command->argc ; ++i)
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
    DonnaCommand *command;
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

    arr = g_ptr_array_sized_new (command->argc + 2);
    g_ptr_array_add (arr, task);
    for (i = 0; i < command->argc; ++i)
    {
        gpointer ptr;

        c = *end;
        *end = '\0';
        if (!_donna_command_convert_arg (cr->app, command->arg_type[i], TRUE,
                    TRUE, start, &ptr, &err))
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

        if (!_donna_command_get_next_arg (command, i, &start, &end, &err))
        {
            donna_task_take_error (task, err);
            _donna_command_free_args (command, arr);
            return DONNA_TASK_FAILED;
        }
    }

    if (!_donna_command_checks_post_parsing (command, i, start, end, &err))
    {
        donna_task_take_error (task, err);
        _donna_command_free_args (command, arr);
        return DONNA_TASK_FAILED;
    }

    /* add DonnaApp* as extra arg for command */
    g_ptr_array_add (arr, cr->app);

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

static void
show_err_on_task_failed (DonnaTask  *task,
                         gboolean    timeout_called,
                         DonnaApp   *app)
{
    if (donna_task_get_state (task) != DONNA_TASK_FAILED)
        return;

    donna_app_show_error (app, donna_task_get_error (task),
            "Command 'action_node': Failed to trigger node");
}

static DonnaTaskState
cmd_node_activate (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    gboolean is_alt = GPOINTER_TO_INT (args->pdata[2]);
    DonnaTreeView *tree;

    if (donna_node_get_node_type (args->pdata[1]) == DONNA_NODE_CONTAINER)
    {
        if (is_alt)
        {
            donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                    COMMAND_ERROR_OTHER,
                    "action_node (CONTAINER, 1) == popup; not yet implemented");
            return DONNA_TASK_FAILED;
        }
    }
    else /* DONNA_NODE_ITEM */
    {
        if (!is_alt)
        {
            DonnaTask *trigger_task;

            trigger_task = donna_node_trigger_task (args->pdata[1], &err);
            if (!trigger_task)
            {
                donna_task_take_error (task_for_ret_err (), err);
                return DONNA_TASK_FAILED;
            }

            donna_task_set_callback (trigger_task,
                    (task_callback_fn) show_err_on_task_failed,
                    args->pdata[3], NULL);
            donna_app_run_task (args->pdata[3], trigger_task);
            return DONNA_TASK_DONE;
        }
    }

    /* (CONTAINER && !is_alt) || (ITEM && is_alt) */

    g_object_get (args->pdata[3], "active-list", &tree, NULL);
    if (!donna_tree_view_set_location (tree, args->pdata[1], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        g_object_unref (tree);
        return DONNA_TASK_FAILED;
    }
    g_object_unref (tree);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_task_set_state (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaTaskState state;

    if (!streq (donna_node_get_domain (args->pdata[1]), "task"))
    {
        gchar *fl = donna_node_get_full_location (args->pdata[1]);
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Command 'task_set_state' cannot be used on '%s', only works on node in domain 'task'",
                fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    if (streq (args->pdata[2], "run"))
        state = DONNA_TASK_RUNNING;
    else if (streq (args->pdata[2], "pause"))
        state = DONNA_TASK_PAUSED;
    else if (streq (args->pdata[2], "cancel"))
        state = DONNA_TASK_CANCELLED;
    else if (streq (args->pdata[2], "stop"))
        state = DONNA_TASK_STOPPED;
    else if (streq (args->pdata[2], "wait"))
        state = DONNA_TASK_WAITING;
    else
    {
        gchar *d = donna_node_get_name (args->pdata[1]);
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_SYNTAX,
                "Invalid state for task '%s': '%s' "
                "Must be 'run', 'pause', 'cancel', 'stop' or 'wait'",
                d, args->pdata[2]);
        g_free (d);
        return DONNA_TASK_FAILED;
    }

    if (!donna_task_manager_set_state (
                donna_app_get_task_manager (args->pdata[3]),
                args->pdata[1], state, &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_task_toggle (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaTask *t;

    if (!streq (donna_node_get_domain (args->pdata[1]), "task"))
    {
        gchar *fl = donna_node_get_full_location (args->pdata[1]);
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Command 'task_toggle' cannot be used on '%s', only works on node in domain 'task'",
                fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_trigger_task (args->pdata[1], &err);
    if (!t)
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (args->pdata[2], t);
    donna_task_wait_for_it (t);

    if (donna_task_get_state (t) != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
            donna_task_take_error (task_for_ret_err (), g_error_copy (err));
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }

    g_object_unref (t);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_activate_row (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_activate_row (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_edit_column (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_edit_column (args->pdata[1], args->pdata[2],
                args->pdata[3], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_full_collapse (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_full_collapse (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_full_expand (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_full_expand (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_visual (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaTreeVisual visual;
    DonnaTreeVisualSource source;
    gchar *s;
    GValue *value;

    if (streq (args->pdata[3], "name"))
        visual = DONNA_TREE_VISUAL_NAME;
    else if (streq (args->pdata[3], "box"))
        visual = DONNA_TREE_VISUAL_BOX;
    else if (streq (args->pdata[3], "highlight"))
        visual = DONNA_TREE_VISUAL_HIGHLIGHT;
    else if (streq (args->pdata[3], "clicks"))
        visual = DONNA_TREE_VISUAL_CLICKS;
    else
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'box', 'highlight' or 'clicks'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (streq (args->pdata[4], "any"))
        source = DONNA_TREE_VISUAL_SOURCE_ANY;
    else if (streq (args->pdata[4], "tree"))
        source = DONNA_TREE_VISUAL_SOURCE_TREE;
    else if (streq (args->pdata[4], "node"))
        source = DONNA_TREE_VISUAL_SOURCE_NODE;
    else
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown source '%s'. "
                "Must be 'tree', 'node', or 'any'",
                args->pdata[4]);
        return DONNA_TASK_FAILED;
    }

    s = donna_tree_view_get_visual (args->pdata[1], args->pdata[2], visual,
                source, &err);
    if (!s)
    {
        if (err)
            donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task_for_ret_err ());
    g_value_init (value, G_TYPE_STRING);
    g_value_take_string (value, s);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_maxi_collapse (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_maxi_collapse (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_maxi_expand (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_maxi_expand (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_selection (DonnaTask *task, GPtrArray *args)
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

static DonnaTaskState
cmd_tree_set_cursor (DonnaTask *task, GPtrArray *args)
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
cmd_tree_set_focus (DonnaTask *task, GPtrArray *args)
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
cmd_tree_set_visual (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaTreeVisual visual;

    if (streq (args->pdata[3], "name"))
        visual = DONNA_TREE_VISUAL_NAME;
    else if (streq (args->pdata[3], "icon"))
        visual = DONNA_TREE_VISUAL_ICON;
    else if (streq (args->pdata[3], "box"))
        visual = DONNA_TREE_VISUAL_BOX;
    else if (streq (args->pdata[3], "highlight"))
        visual = DONNA_TREE_VISUAL_HIGHLIGHT;
    else if (streq (args->pdata[3], "clicks"))
        visual = DONNA_TREE_VISUAL_CLICKS;
    else
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'icon', 'box', 'highlight' or 'clicks'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_set_visual (args->pdata[1], args->pdata[2], visual,
                args->pdata[4], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_toggle_row (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaTreeToggle toggle;

    if (streq (args->pdata[3], "std"))
        toggle = DONNA_TREE_TOGGLE_STANDARD;
    else if (streq (args->pdata[3], "full"))
        toggle = DONNA_TREE_TOGGLE_FULL;
    else if (streq (args->pdata[3], "maxi"))
        toggle = DONNA_TREE_TOGGLE_MAXI;
    else
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_SYNTAX,
                "Cannot toggle row, unknown toggle type '%s'. Must be 'std', 'full' or 'maxi'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_toggle_row (args->pdata[1], args->pdata[2], toggle, &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}
