
#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "task-manager.h"
#include "macros.h"
#include "debug.h"

static DonnaTaskState   cmd_node_activate                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_node_popup_children             (DonnaTask *task,
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
static DonnaTaskState   cmd_tree_goto_line                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_maxi_collapse              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_maxi_expand                (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_refresh                    (DonnaTask *task,
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
        .name           = "node_popup_children",
        .argc           = 5,
        .arg_type       = { DONNA_ARG_TYPE_NODE, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_TREEVIEW | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL,
        .cmd_fn         = cmd_node_popup_children
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
        .name           = "tree_goto_line",
        .argc           = 5,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_ROW_ID, DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_goto_line
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
        .name           = "tree_refresh",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_refresh
    },
    {
        .name           = "tree_selection",
        .argc           = 4,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_ROW_ID, DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL },
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
        gint unesc = 0;
        for (s = ++*arg; ; )
        {
            gint i;

            for ( ; *s != '"' && *s != '\0'; ++s)
                if (unesc < 0)
                    s[unesc] = *s;
            if (unesc < 0)
                s[unesc] = *s;
            if (*s == '\0')
            {
                g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                        "Missing ending quote");
                return FALSE;
            }
            /* check for escaped quotes */
            for (i = 0; &s[i + unesc - 1] >= *arg && s[i + unesc - 1] == '\\'; --i)
                ;
            if ((i % 2) == 0)
                break;
            s[--unesc] = *s;
            ++s;
        }
        if (unesc < 0)
            s[unesc] = '\0';
        /* the *end will still be the last char. "originally" ending it, i.e. the
         * actual ending quote. Meaning that using *arg it's the unescaped
         * string ending a little before *end, which is as usual so moving to
         * the next arg works as expected/usual */
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
    if (**end != ',' && **end != ')')
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

static gboolean
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
        else if (*start == *end && i + 2 == command->argc
                && (command->arg_type[i + 1] & DONNA_ARG_IS_OPTIONAL))
            /* no last arg, but it is optional */
            return TRUE;
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
    else if (*start == *end && i + 2 <= command->argc
            && !(command->arg_type[i] & DONNA_ARG_IS_OPTIONAL))
    {
        return 1;
        g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                "Command '%s', argument %d missing",
                command->name, i + 2);
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

static gboolean
_donna_command_convert_arg (DonnaApp        *app,
                            DonnaArgType     type,
                            gboolean         from_string,
                            gboolean         can_block,
                            gpointer         sce,
                            gpointer        *dst,
                            GError         **error)
{
    if (type & DONNA_ARG_TYPE_INT)
    {
        if (from_string)
            * (gint *) dst = g_ascii_strtoll (sce, NULL, 10);
        else
            *dst = g_strdup_printf ("%d", GPOINTER_TO_INT (sce));
    }
    else if (type & (DONNA_ARG_TYPE_STRING | DONNA_ARG_TYPE_PATH))
    {
        gchar *s;
        /* PATH is treated as a string, because it will be. The reason we keep
         * it as a string is to allow donna-specific things that we otherwise
         * couldn't convert (w/out the tree), such as ":last" or ":prev" */
        for (s = sce; *s && isblank (*s); ++s)
            ;
        if (!s || *s == '\0')
            *dst = NULL;
        else
            *dst = g_strdup (sce);
    }
    else if (type & DONNA_ARG_TYPE_TREEVIEW)
    {
        if (from_string)
        {
            if (streq (sce, ":active"))
                g_object_get (app, "active-list", dst, NULL);
            else if (* (gchar *) sce == '<')
            {
                gchar *s = sce;
                gsize len = strlen (s) - 1;
                if (s[len] != '>')
                {
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                            "Invalid treeview name/reference: '%s'",
                            (gchar *) sce);
                    return FALSE;
                }
                *dst = donna_app_get_int_ref (app, sce);
                if (!*dst)
                {
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                            "Invalid internal reference '%s'",
                            (gchar *) sce);
                    return FALSE;
                }
            }
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
    }
    else if (type & DONNA_ARG_TYPE_NODE)
    {
        if (from_string)
        {
            if (* (gchar *) sce == '<')
            {
                gchar *s = sce;
                gsize len = strlen (s) - 1;
                if (s[len] != '>')
                {
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                            "Invalid node full location/reference: '%s'",
                            (gchar *) sce);
                    return FALSE;
                }
                *dst = donna_app_get_int_ref (app, sce);
                if (!*dst)
                {
                    g_set_error (error, COMMAND_ERROR, COMMAND_ERROR_OTHER,
                            "Invalid internal reference '%s'",
                            (gchar *) sce);
                    return FALSE;
                }
                return TRUE;
            }

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
    }
    else if (type & DONNA_ARG_TYPE_ROW)
    {
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
    }
    else if (type & DONNA_ARG_TYPE_ROW_ID)
    {
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
    }
    else if (type == DONNA_ARG_TYPE_NOTHING)
    {
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
    }
    return TRUE;
}

static void
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
        if (!arr->pdata[i])
            /* arg must have been optional & not specified */
            continue;
        else if (command->arg_type[i - 1] & (DONNA_ARG_TYPE_TREEVIEW | DONNA_ARG_TYPE_NODE))
            g_object_unref (arr->pdata[i]);
        else if (command->arg_type[i - 1] & (DONNA_ARG_TYPE_STRING
                    | DONNA_ARG_TYPE_ROW | DONNA_ARG_TYPE_PATH))
            g_free (arr->pdata[i]);
        else if (command->arg_type[i - 1] & DONNA_ARG_TYPE_ROW_ID)
        {
            DonnaTreeRowId *rowid;

            rowid = arr->pdata[i];
            if (rowid->type == DONNA_ARG_TYPE_NODE)
                g_object_unref (rowid->ptr);
            else
                g_free (rowid->ptr);

            g_free (rowid);
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

        /* last arg is optional and wasn't specified (so start & end are NULL) */
        if (i + 1 == command->argc && !end)
        {
            g_ptr_array_add (arr, NULL);
            break;
        }

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

/* for parsing/running commands from treeview, menu or toolbar */

struct rc_data
{
    DonnaApp        *app;
    gboolean         is_heap;
    gboolean         blocking;
    const gchar     *conv_flags;
    _conv_flag_fn    conv_fn;
    gpointer         conv_data;
    GDestroyNotify   conv_destroy;
    gchar           *fl;
    DonnaCommand    *command;
    gchar           *start;
    gchar           *end;
    guint            i;
    GPtrArray       *arr;
};

static gchar *
parse_location (const gchar *sce, struct rc_data *data)
{
    GString *str = NULL;
    gchar *s = (gchar *) sce;

    while ((s = strchr (s, '%')))
    {
        gboolean dereference = s[1] == '*';
        gboolean match;

        if (!dereference)
            match = strchr (data->conv_flags, s[1]) != NULL;
        else
            match = strchr (data->conv_flags, s[2]) != NULL;
        if (match)
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, sce, s - sce);
            if (dereference)
                ++s;
            data->conv_fn (s[1], DONNA_ARG_TYPE_NOTHING, dereference, data->app,
                    (gpointer *) &str, data->conv_data);
            s += 2;
            sce = (const gchar *) s;
        }
        else
            s += 2;
    }

    if (!str)
        return NULL;

    g_string_append (str, sce);
    return g_string_free (str, FALSE);
}

static void
free_rc_data (struct rc_data *data)
{
    if (data->conv_data && data->conv_destroy)
        data->conv_destroy (data->conv_data);
    g_free (data->fl);
    if (data->arr)
        _donna_command_free_args (data->command, data->arr);
    if (data->is_heap)
        g_free (data);
}

static void
command_run_cb (DonnaTask *task, gboolean timeout_called, DonnaApp *app)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (app, donna_task_get_error (task),
                "Action triggered failed");
}

struct err
{
    DonnaApp    *app;
    GError      *err;
    gchar       *msg;
};

static gboolean
real_show_error (struct err *e)
{
    donna_app_show_error (e->app, e->err, e->msg);
    g_clear_error (&e->err);
    g_free (e->msg);
    g_free (e);
    return FALSE;
}

/* because we might be in a thread */
static inline void
show_error (DonnaApp *app, GError *err, const gchar *msg, ...)
{
    struct err *e;
    va_list va_arg;

    e = g_new (struct err, 1);
    e->app = app;
    e->err = err;
    va_start (va_arg, msg);
    e->msg = g_strdup_vprintf (msg, va_arg);
    va_end (va_arg);
    g_main_context_invoke (NULL, (GSourceFunc) real_show_error, e);
}

static DonnaTaskState
run_command (DonnaTask *task, struct rc_data *data)
{
    GError *err = NULL;
    DonnaTask *cmd_task;

    if (!data->command)
    {
        data->command = _donna_command_init_parse (data->fl + 8,
                &data->start, &data->end, &err);
        if (!data->command)
        {
            show_error (data->app, err,
                    "Cannot trigger node, parsing command failed");
            free_rc_data (data);
            return DONNA_TASK_FAILED;
        }

        data->arr = g_ptr_array_sized_new (data->command->argc + 3);
        g_ptr_array_add (data->arr, task);
    }
    for ( ; data->i < data->command->argc; ++data->i)
    {
        gpointer ptr;
        gchar c;

        /* last arg is optional and wasn't specified (so start & end are NULL) */
        if (data->i + 1 == data->command->argc && !data->end)
        {
            g_ptr_array_add (data->arr, NULL);
            break;
        }

        c = *data->end;
        *data->end = '\0';

        if (data->start[0] == '%' && strchr (data->conv_flags, data->start[1])
                && data->conv_fn (data->start[1], data->command->arg_type[data->i],
                    FALSE, NULL, &ptr, data->conv_data))
                g_ptr_array_add (data->arr, ptr);
        else
        {
            gchar *s;

            s = parse_location (data->start, data);
            if (!_donna_command_convert_arg (data->app,
                        data->command->arg_type[data->i], TRUE,
                        task != NULL || data->blocking,
                        (s) ? s : data->start, &ptr, &err))
            {
                g_free (s);
                if (g_error_matches (err, COMMAND_ERROR, COMMAND_ERROR_MIGHT_BLOCK))
                {
                    struct rc_data *d;

                    /* restore */
                    *data->end = c;

                    /* need to put data on heap */
                    d = g_new (struct rc_data, 1);
                    memcpy (d, data, sizeof (struct rc_data));
                    d->is_heap = TRUE;

                    /* and continue parsing in a task/new thread */
                    task = donna_task_new ((task_fn) run_command, d,
                            (GDestroyNotify) free_rc_data);
                    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL);
                    donna_app_run_task (data->app, task);
                    return DONNA_TASK_DONE;
                }
                show_error (data->app, err,
                        "Cannot trigger node, parsing argument %d failed",
                        data->i + 1);
                free_rc_data (data);
                return DONNA_TASK_FAILED;
            }
            g_free (s);
            g_ptr_array_add (data->arr, ptr);
        }
        *data->end = c;

        if (!_donna_command_get_next_arg (data->command, data->i,
                    &data->start, &data->end, &err))
        {
            show_error (data->app, err,
                    "Cannot trigger node, parsing command failed");
            free_rc_data (data);
            return DONNA_TASK_FAILED;
        }
    }

    if (!_donna_command_checks_post_parsing (data->command, data->i,
                data->start, data->end, &err))
    {
        show_error (data->app, err,
                "Cannot trigger node, parsing command failed");
        free_rc_data (data);
        return DONNA_TASK_FAILED;
    }

    /* add DonnaApp* as extra arg for command */
    g_ptr_array_add (data->arr, data->app);

    cmd_task = donna_task_new ((task_fn) data->command->cmd_fn, data->arr, NULL);
    donna_task_set_visibility (cmd_task, data->command->visibility);
    donna_task_set_callback (cmd_task, (task_callback_fn) command_run_cb,
            data->app, NULL);
    if (task || data->blocking)
        /* avoid starting another thread, since we're already in one */
        donna_task_set_can_block (g_object_ref_sink (cmd_task));
    donna_app_run_task (data->app, cmd_task);
    if (task || data->blocking)
    {
        donna_task_wait_for_it (cmd_task);
        g_object_unref (cmd_task);
    }
    return DONNA_TASK_DONE;
}

gboolean
_donna_command_parse_run (DonnaApp       *app,
                          gboolean        blocking,
                          const gchar    *conv_flags,
                          _conv_flag_fn   conv_fn,
                          gpointer        conv_data,
                          GDestroyNotify  conv_destroy,
                          gchar          *fl)
{
    struct rc_data data;

    memset (&data, 0, sizeof (struct rc_data));
    data.app          = app;
    data.blocking     = blocking;
    data.conv_flags   = conv_flags;
    data.conv_fn      = conv_fn;
    data.conv_data    = conv_data;
    data.conv_destroy = conv_destroy;
    data.fl           = fl;

    if (streqn (fl, "command:", 8))
    {
        /* run_command() will take care of freeing data as/when needed */
        return run_command (NULL, &data) == DONNA_TASK_DONE;
    }
    else
    {
        gchar *ss;

        ss = parse_location (fl, &data);
        if (ss)
        {
            g_free (data.fl);
            data.fl = ss;
        }

        if (!blocking)
            donna_app_trigger_node (app, data.fl);
        else
        {
            DonnaTask *task;
            DonnaNode *node;

            task = donna_app_get_node_task (app, data.fl);
            donna_task_set_can_block (g_object_ref_sink (task));
            donna_app_run_task (app, task);
            donna_task_wait_for_it (task);

            if (donna_task_get_state (task) != DONNA_TASK_DONE)
            {
                g_object_unref (task);
                free_rc_data (&data);
                return FALSE;
            }
            node = g_value_get_object (donna_task_get_return_value (task));
            g_object_unref (task);

            task = donna_node_trigger_task (node, NULL);
            donna_task_set_can_block (g_object_ref_sink (task));
            donna_app_run_task (app, task);
            donna_task_wait_for_it (task);

            if (donna_task_get_state (task) != DONNA_TASK_DONE)
            {
                g_object_unref (task);
                free_rc_data (&data);
                return FALSE;
            }
            g_object_unref (task);
        }
        free_rc_data (&data);
    }
    return TRUE;
}

/* helpers */

static gint
get_arg_from_list (gint nb, const gchar *choices[], const gchar *arg)
{
    gchar to_lower = 'A' - 'a';
    gint *matches;
    gint i;

    if (!arg)
        return -1;

    matches = g_new (gint, nb);
    for (i = 0; i < nb; ++i)
        matches[i] = i;

    for (i = 0; arg[i] != '\0'; ++i)
    {
        gchar a;
        gint *m;

        a = arg[i];
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

#define get_choice_from_arg(choices, index)   \
    get_arg_from_list (sizeof (choices) / sizeof (choices[0]), choices, \
            args->pdata[index])

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
cmd_node_popup_children (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_children[] = { "all", "item", "container" };
    DonnaNodeType children[]  = { DONNA_NODE_ITEM | DONNA_NODE_CONTAINER,
        DONNA_NODE_ITEM, DONNA_NODE_CONTAINER };
    DonnaTaskState state;
    DonnaTask *t;
    struct popup_children_data data;
    gint c;

    c = get_choice_from_arg (c_children, 2);
    if (c < 0)
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_SYNTAX,
                "Invalid type of node children: '%s'; Must be 'item', 'container' or 'all'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_get_children_task (args->pdata[1], children[c], &err);
    if (!t)
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (args->pdata[6], t);
    donna_task_wait_for_it (t);

    state = donna_task_get_state (t);
    if (state != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'node_popup_children' failed: ");
            donna_task_take_error (task_for_ret_err (), err);
        }
        else
        {
            gchar *fl = donna_node_get_full_location (args->pdata[1]);
            donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                    COMMAND_ERROR_OTHER,
                    "Command 'node_popup_children' failed: Unable to get children of '%s'",
                    fl);
            g_free (fl);
        }
        g_object_unref (t);
        return state;
    }

    data.app    = args->pdata[6];
    data.tree   = args->pdata[5];
    data.filter = args->pdata[4];
    data.menus  = args->pdata[3];
    data.nodes  = g_value_dup_boxed (donna_task_get_return_value (t));
    g_object_unref (t);

    t = donna_task_new ((task_fn) popup_children, &data, NULL);
    donna_task_set_visibility (t, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (data.app, t);
    donna_task_wait_for_it (t);

    state = donna_task_get_state (t);
    if (state != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
            donna_task_take_error (task_for_ret_err (), g_error_copy (err));
    }
    g_object_unref (t);
    return state;
}

static DonnaTaskState
cmd_task_set_state (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *choices[] = { "run", "pause", "cancel", "stop", "wait" };
    DonnaTaskState state[] = { DONNA_TASK_RUNNING, DONNA_TASK_PAUSED,
        DONNA_TASK_CANCELLED, DONNA_TASK_STOPPED, DONNA_TASK_WAITING };
    gint c;

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

    c = get_choice_from_arg (choices, 2);
    if (c < 0)
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
                args->pdata[1], state[c], &err))
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
    const gchar *c_visual[] = { "name", "box", "highlight", "clicks" };
    DonnaTreeVisual visual[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_BOX,
        DONNA_TREE_VISUAL_HIGHLIGHT, DONNA_TREE_VISUAL_CLICKS };
    const gchar *c_source[] = { "any", "tree", "node" };
    DonnaTreeVisualSource source[] = { DONNA_TREE_VISUAL_SOURCE_ANY,
        DONNA_TREE_VISUAL_SOURCE_TREE, DONNA_TREE_VISUAL_SOURCE_NODE };
    gchar *s;
    GValue *value;
    gint c_v;
    gint c_s;

    c_v = get_choice_from_arg (c_visual, 3);
    if (c_v < 0)
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'box', 'highlight' or 'clicks'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    c_s = get_choice_from_arg (c_source, 4);
    if (c_s < 0)
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown source '%s'. "
                "Must be 'tree', 'node', or 'any'",
                args->pdata[4]);
        return DONNA_TASK_FAILED;
    }

    s = donna_tree_view_get_visual (args->pdata[1], args->pdata[2], visual[c_v],
                source[c_s], &err);
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
cmd_tree_goto_line (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_set[] = { "scroll", "focus", "cursor" };
    DonnaTreeSet _set[] = { DONNA_TREE_SET_SCROLL, DONNA_TREE_SET_FOCUS,
        DONNA_TREE_SET_CURSOR };
    const gchar *c_nb_type[] = { "repeat", "line", "percent" };
    DonnaTreeGoto nb_type[] = { DONNA_TREE_GOTO_REPEAT, DONNA_TREE_GOTO_LINE,
        DONNA_TREE_GOTO_PERCENT };
    DonnaTreeSet set;
    gchar *s;
    gint c_n;
    gint nb_sets;

    nb_sets = sizeof (c_set) / sizeof (c_set[0]);
    s = args->pdata[2];
    set = 0;
    for (;;)
    {
        gchar *ss;
        gchar *start, *end, e;
        gint c_s;

        ss = strchr (s, '+');
        if (ss)
            *ss = '\0';

        /* since we're allowing separators, we have do "trim" things */
        for (start = s; isblank (*start); ++start)
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

        c_s = get_arg_from_list (nb_sets, c_set, start);

        /* "undo trim" */
        if (ss)
            *ss = '+';
        if (end)
            *end = e;

        if (c_s < 0)
        {
            set = 0;
            break;
        }
        set |= _set[c_s];

        if (ss)
            s = ss + 1;
        else
            break;
    }
    if (set == 0)
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot go to line, unknown set type '%s'. "
                "Must be (a '+'-separated combination of) 'scroll', 'focus' and/or 'cursor'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    c_n = get_choice_from_arg (c_nb_type, 5);
    if (c_n < 0)
        c_n = 0;

    if (!donna_tree_view_goto_line (args->pdata[1], set, args->pdata[3],
                GPOINTER_TO_INT (args->pdata[4]), nb_type[c_n], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }
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
cmd_tree_refresh (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *choices[] = { "visible", "simple", "normal", "reload" };
    DonnaTreeRefreshMode mode[] = { DONNA_TREE_REFRESH_VISIBLE,
        DONNA_TREE_REFRESH_SIMPLE, DONNA_TREE_REFRESH_NORMAL,
        DONNA_TREE_REFRESH_RELOAD };
    gint c;

    c = get_choice_from_arg (choices, 2);
    if (c < 0)
    {
        donna_task_set_error (task_for_ret_err (),
                COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Invalid argument 'mode': '%s', expected 'visible', 'simple', 'normal' or 'reload'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_refresh (args->pdata[1], mode[c], &err))
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
    const gchar *choices[] = { "select", "unselect", "invert" };
    DonnaTreeSelAction action[] = { DONNA_TREE_SEL_SELECT,
        DONNA_TREE_SEL_UNSELECT, DONNA_TREE_SEL_INVERT };
    gint c;

    c = get_choice_from_arg (choices, 2);
    if (c < 0)
    {
        donna_task_set_error (task_for_ret_err (),
                COMMAND_ERROR, COMMAND_ERROR_SYNTAX,
                "Invalid argument 'action': '%s', expected 'select', 'unselect' or 'invert'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_selection (args->pdata[1], action[c], args->pdata[3],
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
    const gchar *choices[] = { "name", "icon", "box", "highlight", "clicks" };
    DonnaTreeVisual visual[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_ICON,
        DONNA_TREE_VISUAL_BOX, DONNA_TREE_VISUAL_HIGHLIGHT,
        DONNA_TREE_VISUAL_CLICKS };
    gint c;

    c = get_choice_from_arg (choices, 3);
    if (c < 0)
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'icon', 'box', 'highlight' or 'clicks'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_set_visual (args->pdata[1], args->pdata[2], visual[c],
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
    const gchar *choices[] = { "standard", "full", "maxi" };
    DonnaTreeToggle toggle[] = { DONNA_TREE_TOGGLE_STANDARD,
        DONNA_TREE_TOGGLE_FULL, DONNA_TREE_TOGGLE_MAXI };
    gint c;

    c = get_choice_from_arg (choices, 3);
    if (c < 0)
    {
        donna_task_set_error (task_for_ret_err (), COMMAND_ERROR,
                COMMAND_ERROR_SYNTAX,
                "Cannot toggle row, unknown toggle type '%s'. Must be 'standard', 'full' or 'maxi'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_toggle_row (args->pdata[1], args->pdata[2], toggle[c], &err))
    {
        donna_task_take_error (task_for_ret_err (), err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}
