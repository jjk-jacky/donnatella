
#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "task-manager.h"
#include "provider.h"
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
    GError *er = NULL;
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
    gchar *name = args[0];
    GValue *v;
    gboolean val;

    if (!donna_config_get_boolean (donna_app_peek_config (app), &val, "%s", name))
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gchar *name = args[0];
    GValue *v;
    gint val;

    if (!donna_config_get_int (donna_app_peek_config (app), &val, "%s", name))
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_INT);
    g_value_set_int (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gchar *name = args[0];
    GValue *v;
    gchar *val;

    if (!donna_config_get_string (donna_app_peek_config (app), &val, "%s", name))
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_STRING);
    g_value_take_string (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_boolean (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gchar *name = args[0];
    gint value = GPOINTER_TO_INT (args[1]);

    if (!donna_config_set_boolean (donna_app_peek_config (app), value, "%s", name))
        return DONNA_TASK_FAILED;
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_int (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gchar *name = args[0];
    gint value = GPOINTER_TO_INT (args[1]);

    if (!donna_config_set_int (donna_app_peek_config (app), value, "%s", name))
        return DONNA_TASK_FAILED;
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_string (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    gchar *name = args[0];
    gchar *value = args[1];

    if (!donna_config_set_string (donna_app_peek_config (app), value, "%s", name))
        return DONNA_TASK_FAILED;
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
cmd_node_popup_children (DonnaTask *task, DonnaApp *app, gpointer *args);

static DonnaTaskState
cmd_node_activate (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaNode *node = args[0];
    gboolean is_alt = GPOINTER_TO_INT (args[1]); /* opt */

    DonnaTreeView *tree;

    if (donna_node_get_node_type (node) == DONNA_NODE_CONTAINER)
    {
        if (is_alt)
        {
            gpointer _args[5] = { node, "all", NULL, NULL, NULL };
            return cmd_node_popup_children (task, app, _args);
        }
    }
    else /* DONNA_NODE_ITEM */
    {
        if (!is_alt)
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
    }

    /* (CONTAINER && !is_alt) || (ITEM && is_alt) */

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

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (app, t);
    donna_task_wait_for_it (t);

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
    g_value_set_object (value, g_value_dup_object (v));
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

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (app, t);
    donna_task_wait_for_it (t);

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
    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (data.app, t);
    donna_task_wait_for_it (t);

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
cmd_nodes_io (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    gchar *io_type = args[1];
    DonnaNode *dest = args[2]; /* opt */

    const gchar *c_io_type[] = { "copy", "move", "delete" };
    DonnaIoType io_types[] = { DONNA_IO_COPY, DONNA_IO_MOVE, DONNA_IO_DELETE };
    gint c_io;

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

    if (!donna_app_nodes_io (app, nodes, io_types[c_io], dest, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_nodes_remove_from (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    GPtrArray *nodes = args[0];
    DonnaNode *source = args[1];

    DonnaProvider *provider;
    DonnaTask *t;

    provider = donna_node_peek_provider (source);
    t = donna_provider_remove_from_task (provider, nodes, source, &err);
    if (!t)
    {
        g_prefix_error (&err, "Command 'nodes_remove_from': Failed to get task: ");
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (app, t);
    donna_task_wait_for_it (t);

    if (donna_task_get_state (t) != DONNA_TASK_DONE)
    {
        err = (GError *) donna_task_get_error (t);
        if (err)
        {
            err = g_error_copy (err);
            g_prefix_error (&err, "Command 'nodes_remove_from' failed: ");
            donna_task_take_error (task, err);
        }
        else
        {
            gchar *fl = donna_node_get_full_location (source);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command 'nodes_remove_from' failed: Unable to remove nodes from '%s'",
                    fl);
            g_free (fl);
        }
        g_object_unref (t);
        return DONNA_TASK_FAILED;
    }
    g_object_unref (t);

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

    if (!donna_task_manager_set_state (donna_app_get_task_manager (app), node,
                states[c], &err))
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

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (app, t);
    donna_task_wait_for_it (t);

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
cmd_tree_edit_column (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    DonnaTreeRowId *rid = args[1];
    gchar *col_name = args[2];

    if (!donna_tree_view_edit_column (tree, rid, col_name, &err))
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

    const gchar *c_visual[] = { "name", "box", "highlight", "clicks" };
    DonnaTreeVisual visuals[] = { DONNA_TREE_VISUAL_NAME, DONNA_TREE_VISUAL_BOX,
        DONNA_TREE_VISUAL_HIGHLIGHT, DONNA_TREE_VISUAL_CLICKS };
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
cmd_tree_go_down (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    if (!donna_tree_view_go_down (tree, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_go_up (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];

    if (!donna_tree_view_go_up (tree, &err))
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
    const gchar *c_action[] = { "select", "unselect", "invert" };
    DonnaTreeSelAction actions[] = { DONNA_TREE_SEL_SELECT, DONNA_TREE_SEL_UNSELECT,
        DONNA_TREE_SEL_INVERT };
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
cmd_tree_selection (DonnaTask *task, DonnaApp *app, gpointer *args)
{
    GError *err = NULL;
    DonnaTreeView *tree = args[0];
    gchar *action = args[1];
    DonnaTreeRowId *rid = args[2];
    gboolean to_focused = GPOINTER_TO_INT (args[3]); /* opt */

    const gchar *choices[] = { "select", "unselect", "invert" };
    DonnaTreeSelAction actions[] = { DONNA_TREE_SEL_SELECT,
        DONNA_TREE_SEL_UNSELECT, DONNA_TREE_SEL_INVERT };
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
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (node_activate, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

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
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_OPTIONAL;
    add_command (nodes_io, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (nodes_remove_from, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (task_set_state, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_NODE;
    add_command (task_toggle, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
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
    add_command (tree_edit_column, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
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
    add_command (tree_go_down, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
            DONNA_ARG_TYPE_NOTHING);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
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
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_TREEVIEW;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_history_move, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
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
    arg_type[++i] = DONNA_ARG_TYPE_ROW_ID;
    arg_type[++i] = DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL;
    add_command (tree_selection, ++i, DONNA_TASK_VISIBILITY_INTERNAL_GUI,
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
