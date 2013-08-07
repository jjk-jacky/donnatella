
#include <stdio.h>
#include <ctype.h>
#include "command.h"
#include "treeview.h"
#include "task-manager.h"
#include "macros.h"
#include "debug.h"

static DonnaTaskState   cmd_ask_text                        (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_config_get_boolean              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_config_get_int                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_config_get_string               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_config_set_boolean              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_config_set_int                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_config_set_string               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_menu_popup                      (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_node_activate                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_node_new_child                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_node_popup_children             (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_nodes_io                        (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_add_nodes              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_drop                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_get_nodes              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_get_type               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_load                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_nodes_io               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_save                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_set                    (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_register_set_type               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_repeat                          (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_task_set_state                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_task_toggle                     (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_abort                      (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_activate_row               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_add_root                   (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_edit_column                (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_full_collapse              (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_full_expand                (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_get_location               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_get_node_at_row            (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_get_nodes                  (DonnaTask *task,
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
static DonnaTaskState   cmd_tree_remove_row                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_reset_keys                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_selection                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_cursor                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_focus                  (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_key_mode               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_location               (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_set_visual                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_tree_toggle_row                 (DonnaTask *task,
                                                             GPtrArray *args);
static DonnaTaskState   cmd_void                            (DonnaTask *task,
                                                             GPtrArray *args);

static DonnaCommand commands[] = {
    {
        .name           = "ask_text",
        .argc           = 4,
        .arg_type       = { DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL | DONNA_ARG_IS_ARRAY },
        .return_type    = DONNA_ARG_TYPE_STRING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_ask_text
    },
    {
        .name           = "config_get_boolean",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_INT,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_config_get_boolean
    },
    {
        .name           = "config_get_int",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_INT,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_config_get_int
    },
    {
        .name           = "config_get_string",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_STRING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_config_get_string
    },
    {
        .name           = "config_set_boolean",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_INT },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_config_set_boolean
    },
    {
        .name           = "config_set_int",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_INT },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_config_set_int
    },
    {
        .name           = "config_set_string",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_config_set_string
    },
    {
        .name           = "menu_popup",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_menu_popup
    },
    {
        .name           = "node_activate",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_NODE, DONNA_ARG_TYPE_INT },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_node_activate
    },
    {
        .name           = "node_new_child",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_NODE, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NODE,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL,
        .cmd_fn         = cmd_node_new_child
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
        .name           = "nodes_io",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY,
            DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL,
        .cmd_fn         = cmd_nodes_io
    },
    {
        .name           = "register_add_nodes",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_register_add_nodes
    },
    {
        .name           = "register_drop",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_register_drop
    },
    {
        .name           = "register_get_nodes",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL,
        .cmd_fn         = cmd_register_get_nodes
    },
    {
        .name           = "register_get_type",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_STRING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_register_get_type
    },
    {
        .name           = "register_load",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_register_load
    },
    {
        .name           = "register_nodes_io",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL,
        .cmd_fn         = cmd_register_nodes_io
    },
    {
        .name           = "register_save",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_register_save
    },
    {
        .name           = "register_set",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING, DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL,
        .cmd_fn         = cmd_register_set
    },
    {
        .name           = "register_set_type",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_register_set_type
    },
    {
        .name           = "repeat",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_INT, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_repeat
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
        .name           = "tree_abort",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_abort
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
        .name           = "tree_add_root",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_NODE },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_add_root
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
        .name           = "tree_get_location",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW },
        .return_type    = DONNA_ARG_TYPE_NODE,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_get_location
    },
    {
        .name           = "tree_get_node_at_row",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NODE,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_get_node_at_row
    },
    {
        .name           = "tree_get_nodes",
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID,
            DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_get_nodes
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
        .argc           = 7,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING,
            DONNA_ARG_TYPE_ROW_ID, DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL },
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
        .name           = "tree_remove_row",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_remove_row
    },
    {
        .name           = "tree_reset_keys",
        .argc           = 1,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_reset_keys
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
        .argc           = 3,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_ROW_ID,
            DONNA_ARG_TYPE_INT | DONNA_ARG_IS_OPTIONAL },
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
        .name           = "tree_set_key_mode",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_STRING },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_set_key_mode
    },
    {
        .name           = "tree_set_location",
        .argc           = 2,
        .arg_type       = { DONNA_ARG_TYPE_TREEVIEW, DONNA_ARG_TYPE_NODE },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_GUI,
        .cmd_fn         = cmd_tree_set_location
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
    {
        .name           = "void",
        .argc           = 8,
        .arg_type       = { DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL,
            DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL },
        .return_type    = DONNA_ARG_TYPE_NOTHING,
        .visibility     = DONNA_TASK_VISIBILITY_INTERNAL_FAST,
        .cmd_fn         = cmd_void
    },
};
static guint nb_commands = sizeof (commands) / sizeof (commands[0]);

#define skip_blank(s)   for ( ; isblank (*s); ++s) ;

static void
free_command_args (DonnaCommand *command, GPtrArray *arr)
{
    guint i;

    /* we use arr->len because the array might not be fuly filled, in case of
     * error halfway through parsing/converting; We *also* use command->argc
     * because the arr sent to the command contains an extra pointer, to
     * DonnaApp in case the command needs it (e.g. to access config, etc).
     * And we start at 1 because pdata[0] is NULL or the "parent task" */
    for (i = 1; i < arr->len && i <= command->argc; ++i)
    {
        if (!arr->pdata[i])
            /* arg must have been optional & not specified */
            continue;
        else if (command->arg_type[i - 1] & DONNA_ARG_IS_ARRAY)
            g_ptr_array_unref (arr->pdata[i]);
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

struct rc_data
{
    DonnaApp        *app;
    gboolean         is_heap;
    const gchar     *conv_flags;
    _conv_flag_fn    conv_fn;
    gpointer         conv_data;
    GDestroyNotify   conv_destroy;
    gchar           *fl;
    DonnaCommand    *command;
    gchar           *start;
    guint            i;
    GPtrArray       *arr;
};

static DonnaTaskState run_command (DonnaTask *task, struct rc_data *data);

static void
free_rc_data (struct rc_data *data)
{
    if (data->conv_data && data->conv_destroy)
        data->conv_destroy (data->conv_data);
    g_free (data->fl);
    if (data->arr)
        free_command_args (data->command, data->arr);
    if (data->is_heap)
        g_free (data);
}

static inline gboolean
can_arg_get_direct_conv (const gchar *flags, gchar *arg)
{
    if (arg[0] != '%' || !strchr (flags, arg[1]))
        return FALSE;
    for (arg += 2; isblank (*arg); ++arg)
        ;
    if (*arg == '\0' || *arg == ',' || *arg == ')')
        return TRUE;
    return FALSE;
}

/* replaces the %n flags into a string (e.g. full location, or just one
 * argument), for "actions" (clicks/keys from treeview, menu, toolbar...) */
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
            match = s[1] != '\0' && strchr (data->conv_flags, s[1]) != NULL;
        else
            match = s[2] != '\0' && strchr (data->conv_flags, s[2]) != NULL;
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
        else if (s[1] != '\0')
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, sce, s - sce);
            sce = (const gchar *) ++s;
            ++s;
        }
        else
            break;
    }

    if (!str)
        return NULL;

    g_string_append (str, sce);
    return g_string_free (str, FALSE);
}

/* If *start is a quoted string, unquotes it; else return FALSE. Unquoting means
 * moving *start after the opening quote, taking care of unescaping any escaped
 * character and puttin a NUL at the end, which will eiher be the ending quote,
 * or before in case some unescaping was some. *end will always point to the
 * position of the ending quote (which may or may not have been turned into a
 * NUL).
 * Returns TRUE was on sucessfull unquoting, FALSE on error (no ending quote) or
 * if the string wasn't quoted. Either check first, or use error to tell. */
static gboolean
unquote_string (gchar **start, gchar **_end, GError **error)
{
    gchar *end;
    guint i = 0;

    if (**start != '"')
        return FALSE;

    for (end = ++*start; ; ++end)
    {
        if (end[i] == '\\')
        {
            *end = end[++i];
            continue;
        }
        *end = end[i];
        if (*end == '"')
            break;
        else if (*end == '\0')
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Missing ending quote");
            return FALSE;
        }
    }
    /* turn the ending quote in to NUL, or put it somewhat before if we
     * did some unescaping */
    *end = '\0';
    /* move end to the original ending quote */
    end += i;

    if (_end)
        *_end = end;

    return TRUE;
}

static DonnaNode *
get_node (DonnaApp *app, const gchar *fl, GError **error)
{
    DonnaTask *task;
    DonnaNode *node = NULL;

    task = donna_app_get_node_task (app, fl);
    if (!task)
    {
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                "Can't get node for '%s'", fl);
        return NULL;
    }
    donna_task_set_can_block (g_object_ref_sink (task));
    donna_app_run_task (app, task);
    if (donna_task_get_state (task) == DONNA_TASK_DONE)
        node = g_value_dup_object (donna_task_get_return_value (task));
    else
    {
        if (error)
            *error = g_error_copy (donna_task_get_error (task));
    }
    g_object_unref (task);
    return node;
}

/* parse the next argument for a command. data must have been properly set &
 * initialized via _donna_command_init_parse() and therefore data->start points
 * to the beginning of the argument data->i (+1).
 * Will handle figuring out where the arg ends (can be quoted, can also be
 * another command to run & get its return value as arg, via @syntax),
 * processing/converting any %n flags, and moving data->start & whatnot where
 * needs be */
static DonnaTaskState
parse_arg (struct rc_data   *data,
           GError          **error)
{
    GError *err = NULL;
    gchar *end;
    gchar *s;
    gchar c;

    if (unquote_string (&data->start, &end, &err))
    {
        /* move to next relevant char */
        ++end;
        skip_blank (end);
    }
    else if (err)
    {
        g_propagate_prefixed_error (error, err, "Command '%s', argument %d: ",
                data->command->name, data->i + 1);
        return DONNA_TASK_FAILED;
    }
    else if (*data->start == '@')
    {
        struct rc_data d;
        gboolean dereference;
        DonnaTask *task;
        DonnaTaskState state;
        const GValue *v;
        gchar *start;

        dereference = data->start[1] == '*';
        start = data->start + ((dereference) ? 2 : 1);

        /* do the init_parse to known which command this is, so we can check
         * visibility/ensure we can run it */
        d.command = _donna_command_init_parse (start, &s, error);
        if (!d.command)
            return DONNA_TASK_FAILED;

        memset (&d, 0, sizeof (struct rc_data));
        d.app          = data->app;
        d.conv_flags   = data->conv_flags;
        d.conv_fn      = data->conv_fn;
        d.conv_data    = data->conv_data;
        /* we don't set conv_destroy because we (obviously) don't want it to
         * free conv_data */
        d.fl           = g_strdup (start);

        /* create a "fake" task so 1) run_command will run the command
         * blockingly, and 2) it will be used for error/return value */
        task = g_object_ref_sink (donna_task_new ((task_fn) gtk_true, NULL, NULL));
        state = run_command (task, &d);
        if (state == DONNA_TASK_CANCELLED)
        {
            g_object_unref (task);
            return DONNA_TASK_CANCELLED;
        }
        else if (state != DONNA_TASK_DONE)
        {
            const GError *err;

            err = donna_task_get_error (task);
            if (err)
            {
                *error = g_error_copy (err);
                g_prefix_error (error, "Command '%s', argument %d: Command %s%s%s failed: ",
                        data->command->name, data->i + 1,
                        (d.command) ? "'" : "",
                        (d.command) ? d.command->name : "",
                        (d.command) ? "' " : "");
            }
            else
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Command %s%s%sfailed (w/out error message)",
                        data->command->name, data->i + 1,
                        (d.command) ? "'" : "",
                        (d.command) ? d.command->name : "",
                        (d.command) ? "' " : "");

            g_object_unref (task);
            return DONNA_TASK_FAILED;
        }

        v = donna_task_get_return_value (task);
        if (!v)
        {
            if (data->command->arg_type[data->i] & DONNA_ARG_IS_OPTIONAL)
            {
                g_ptr_array_add (data->arr, NULL);
                end = start + (d.start - d.fl + 1);
                g_object_unref (task);
                goto next;
            }
            else
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Argument required, and command '%s' didn't return anything",
                        data->command->name, data->i + 1, d.command->name);
                g_object_unref (task);
                return DONNA_TASK_FAILED;
            }
        }

        s = NULL;
        if (d.command->return_type & DONNA_ARG_IS_ARRAY)
        {
            /* is it a matching array? */
            if ((data->command->arg_type[data->i] & d.command->return_type)
                    == d.command->return_type)
                g_ptr_array_add (data->arr, g_value_dup_boxed (v));
            /* since we're talking about a command's return_type, we expect
             * thigs to make sense, i.e. we don't check for unsupported array
             * types */
            else
            {
                GString *str = g_string_new (NULL);
                GPtrArray *arr = g_value_get_boxed (v);
                guint i;

                /* turn to string */

                for (i = 0; i < arr->len; ++i)
                {
                    gchar *ss;

                    /* only 2 types of arrays are supported: NODE & STRING */
                    if (d.command->return_type & DONNA_ARG_TYPE_NODE)
                        s = donna_node_get_full_location (arr->pdata[i]);
                    else
                        s = arr->pdata[i];

                    g_string_append_c (str, '"');
                    for (ss = s; *ss != '\0'; ++ss)
                    {
                        if (*ss == '"' || *ss == '\\')
                            g_string_append_c (str, '\\');
                        g_string_append_c (str, *ss);
                    }
                    g_string_append_c (str, '"');

                    if (d.command->return_type & DONNA_ARG_TYPE_NODE)
                        g_free (s);
                    g_string_append_c (str, ',');
                }
                /* remove last ',' */
                g_string_truncate (str, str->len - 1);
                s = g_string_free (str, FALSE);
            }
        }
        else
        {
            switch (d.command->return_type)
            {
                case DONNA_ARG_TYPE_INT:
                    if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_INT)
                        g_ptr_array_add (data->arr,
                                GINT_TO_POINTER (g_value_get_int (v)));
                    else
                        s = g_strdup_printf ("%d", g_value_get_int (v));
                    break;

                case DONNA_ARG_TYPE_STRING:
                    if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_STRING)
                    {
                        if (data->command->arg_type[data->i] & DONNA_ARG_IS_ARRAY)
                        {
                            GPtrArray *arr;

                            arr = g_ptr_array_new_full (1, g_free);
                            g_ptr_array_add (arr, g_value_dup_string (v));
                            g_ptr_array_add (data->arr, arr);
                        }
                        else
                            g_ptr_array_add (data->arr, g_value_dup_string (v));
                    }
                    else
                        s = g_value_dup_string (v);
                    break;

                case DONNA_ARG_TYPE_TREEVIEW:
                    if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_TREEVIEW)
                        g_ptr_array_add (data->arr, g_value_dup_object (v));
                    else
                        s = g_strdup (donna_tree_view_get_name (g_value_get_object (v)));
                    break;

                case DONNA_ARG_TYPE_NODE:
                    if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_NODE)
                    {
                        if (data->command->arg_type[data->i] & DONNA_ARG_IS_ARRAY)
                        {
                            GPtrArray *arr;

                            arr = g_ptr_array_new_full (1, g_object_unref);
                            g_ptr_array_add (arr, g_value_dup_object (v));
                            g_ptr_array_add (data->arr, arr);
                        }
                        else
                            g_ptr_array_add (data->arr, g_value_dup_object (v));
                    }
                    else
                        s = donna_node_get_full_location (g_value_get_object (v));
                    break;

                case DONNA_ARG_TYPE_NOTHING:
                case DONNA_ARG_TYPE_ROW:
                case DONNA_ARG_TYPE_ROW_ID:
                case DONNA_ARG_TYPE_PATH:
                    g_warning ("Command '%s', argument %d: "
                            "Command '%s' had an unsupported type (%d) of return value",
                            data->command->name,
                            data->i + 1,
                            d.command->name,
                            d.command->return_type);
                    break;
            }
        }

        end = start + (d.start - d.fl + 1);
        c = start[end - start];
        g_object_unref (task);
        if (!s)
            goto next;
        else
            goto convert;
    }
    else
        for (end = data->start; *end != ',' && *end != ')' && *end != '\0'; ++end)
            ;

    if (*end == '\0')
    {
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Command '%s', argument %d: Unexpected end-of-string",
                data->command->name, data->i + 1);
        return DONNA_TASK_FAILED;
    }

    if (*end != ')')
    {
        if (data->i + 1 == data->command->argc)
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s', argument %d: Closing parenthesis missing: %s",
                    data->command->name, data->i + 1, end);
            return DONNA_TASK_FAILED;
        }
        else if (*end != ',')
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s', argument %d: Unexpected character (expected ',' or ')'): %s",
                    data->command->name, data->i + 1, end);
            return DONNA_TASK_FAILED;
        }
    }

    if (end == data->start)
    {
        if (data->command->arg_type[data->i] & DONNA_ARG_IS_OPTIONAL)
        {
            g_ptr_array_add (data->arr, NULL);
            goto next;
        }
        else
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_MISSING_ARG,
                    "Command '%s', argument %d required",
                    data->command->name, data->i + 1);
            return DONNA_TASK_FAILED;
        }
    }

    c = data->start[end - data->start];
    data->start[end - data->start] = '\0';
    if (data->conv_fn && can_arg_get_direct_conv (data->conv_flags, data->start))
    {
        gpointer ptr;

        if (data->conv_fn (data->start[1], data->command->arg_type[data->i],
                    FALSE, NULL, &ptr, data->conv_data))
        {
            g_ptr_array_add (data->arr, ptr);
            data->start[end - data->start] = c;
            goto next;
        }
    }

    s = parse_location (data->start, data);
    if (!s)
        s = data->start;

convert:
    if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_INT)
        g_ptr_array_add (data->arr, GINT_TO_POINTER (g_ascii_strtoll (s, NULL, 10)));
    else if (data->command->arg_type[data->i]
                & (DONNA_ARG_TYPE_STRING | DONNA_ARG_TYPE_PATH))
    {
        if ((data->command->arg_type[data->i]
                    & (DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_ARRAY))
                == (DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_ARRAY))
        {
            gpointer ptr;

            if (*s == '<')
            {
                gsize len = strlen (s) - 1;
                if (s[len] != '>')
                {
                    g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                            "Command '%s', argument %d: Invalid internal reference: '%s'",
                            data->command->name, data->i + 1, s);
                    goto error;
                }
                ptr = donna_app_get_int_ref (data->app, data->start,
                        DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_ARRAY);
                if (!ptr)
                {
                    g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                            "Command '%s', argument %d: Invalid internal reference '%s'",
                            data->command->name, data->i + 1, data->start);
                    goto error;
                }
            }
            else
            {
                GError *err = NULL;
                GPtrArray *arr;
                gchar *start;
                gchar *end;

                start = s;
                arr = g_ptr_array_new_with_free_func (g_free);
                for (;;)
                {
                    if (unquote_string (&start, &end, &err))
                        g_ptr_array_add (arr, g_strdup (start));
                    else if (err)
                    {
                        g_propagate_prefixed_error (error, err,
                                "Command '%s', argument %d: ",
                                data->command->name, data->i + 1);
                        g_ptr_array_unref (arr);
                        goto error;
                    }
                    else
                    {
                        gchar *e;

                        /* not quoted */

                        skip_blank (start);
                        end = strchr (start, ',');
                        if (end)
                        {
                            *end = '\0';
                            e = end - 1;
                        }
                        else
                            e = start + strlen (start) - 1;
                        /* go back, skip_blank() in reverse. Because we aim to trim
                         * this one (hence the skip_blank(start) above as well) */
                        for ( ; isblank (*e); --e) ;
                        *++e = '\0';

                        g_ptr_array_add (arr, g_strdup (start));
                        if (!end)
                            break;
                    }

                    start = end + 1;
                    skip_blank (start);
                    if (*start == '\0')
                        break;
                }
                ptr = arr;
            }
            g_ptr_array_add (data->arr, ptr);
        }
        else
        {
            /* PATH is treated as a string, because it will be. The reason we
             * keep it as a string is to allow donna-specific things that we
             * otherwise couldn't convert (w/out the tree), such as ":last" or
             * ":prev" */
            if (s == data->start)
                g_ptr_array_add (data->arr, g_strdup (s));
            else
            {
                g_ptr_array_add (data->arr, s);
                s = NULL;
            }
        }
    }
    else if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_TREEVIEW)
    {
        gpointer ptr;

        if (streq (s, ":active"))
            g_object_get (data->app, "active-list", &ptr, NULL);
        else if (*s == '<')
        {
            gsize len = strlen (s) - 1;
            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid treeview name/reference: '%s'",
                        data->command->name, data->i + 1, s);
                goto error;
            }
            ptr = donna_app_get_int_ref (data->app, data->start, DONNA_ARG_TYPE_TREEVIEW);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        data->command->name, data->i + 1, data->start);
                goto error;
            }
        }
        else
        {
            ptr = donna_app_get_treeview (data->app, s);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_NOT_FOUND,
                        "Command '%s', argument %d: Treeview '%s' not found",
                        data->command->name, data->i + 1, s);
                goto error;
            }
        }
        g_ptr_array_add (data->arr, ptr);
    }
    else if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_NODE)
    {
        GError *err = NULL;
        GPtrArray *arr = NULL;
        gpointer ptr;

        if (*s == '<')
        {
            gsize len = strlen (s) - 1;
            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid node full location/reference: '%s'",
                        data->command->name, data->i + 1, s);
                goto error;
            }

            /* if an array, try an intref for an array of nodes first */
            if (data->command->arg_type[data->i] & DONNA_ARG_IS_ARRAY)
            {
                ptr = donna_app_get_int_ref (data->app, s,
                        DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);
                if (ptr)
                {
                    g_ptr_array_add (data->arr, ptr);
                    goto inner_next;
                }
            }

            ptr = donna_app_get_int_ref (data->app, s, DONNA_ARG_TYPE_NODE);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        data->command->name, data->i + 1, s);
                goto error;
            }
            else if (data->command->arg_type[data->i] & DONNA_ARG_IS_ARRAY)
            {
                /* we want an array of nodes, not a node */

                arr = g_ptr_array_new_full (1, g_object_unref);
                g_ptr_array_add (arr, ptr);
                ptr = arr;
            }

            g_ptr_array_add (data->arr, ptr);
            goto inner_next;
        }

        if (data->command->arg_type[data->i] & DONNA_ARG_IS_ARRAY)
        {
            gchar *start = s;

            arr = g_ptr_array_new_with_free_func (g_object_unref);
            for (;;)
            {
                gchar *end;

                if (!unquote_string (&start, &end, &err))
                {
                    gchar *e;

                    if (err)
                    {
                        g_propagate_prefixed_error (error, err,
                                "Command '%s', argument %d: ",
                                data->command->name, data->i + 1);
                        g_ptr_array_unref (arr);
                        goto error;
                    }

                    /* not quoted */

                    skip_blank (start);
                    end = strchr (start, ',');
                    if (end)
                    {
                        *end = '\0';
                        e = end - 1;
                    }
                    else
                        e = start + strlen (start) - 1;
                    /* go back, skip_blank() in reverse. Because we aim to trim
                     * this one (hence the skip_blank(start) above as well) */
                    for ( ; isblank (*e); --e) ;
                    *++e = '\0';
                }

                ptr = get_node (data->app, start, &err);
                if (!ptr)
                {
                    g_propagate_prefixed_error (error, err,
                            "Command '%s', argument %d: ",
                            data->command->name, data->i + 1);
                    g_ptr_array_unref (arr);
                    goto error;
                }
                g_ptr_array_add (arr, ptr);

                /* no ',' found in unquoted string */
                if (!end)
                    break;

                start = end + 1;
                skip_blank (start);
                if (*start == '\0')
                    break;
            }
            g_ptr_array_add (data->arr, arr);
        }
        else
        {
            /* we shouldn't use unquote_string() here because this arg has
             * already been unquoted. "Double-quoting" can only happen in case
             * of arrays, where the list is quoted, and so are each element */

            ptr = get_node (data->app, s, &err);
            if (!ptr)
            {
                g_propagate_prefixed_error (error, err,
                        "Command '%s', argument %d: ",
                        data->command->name, data->i + 1);
                goto error;
            }
            g_ptr_array_add (data->arr, ptr);
        }
    }
    else if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_ROW)
    {
        DonnaTreeRow *row = g_new (DonnaTreeRow, 1);
        if (sscanf (s, "[%p;%p]", &row->node, &row->iter) != 2)
        {
            g_free (row);
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                    "Command '%s', argument %d: Invalid argument syntax for TREE_ROW",
                    data->command->name, data->i + 1);
            goto error;
        }
        g_ptr_array_add (data->arr, row);
    }
    else if (data->command->arg_type[data->i] & DONNA_ARG_TYPE_ROW_ID)
    {
        DonnaTreeRowId *rid = g_new (DonnaTreeRowId, 1);
        gpointer ptr;

        if (*s == '[')
        {
            DonnaTreeRow *row = g_new (DonnaTreeRow, 1);
            if (sscanf (s, "[%p;%p]", &row->node, &row->iter) != 2)
            {
                g_free (row);
                g_free (rid);
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid argument syntax TREE_ROW for TREE_ROW_ID",
                        data->command->name, data->i + 1);
                goto error;
            }
            rid->type = DONNA_ARG_TYPE_ROW;
            rid->ptr  = row;
        }
        else if (*s == ':' || *s == '%' || (*s >= '0' && *s <= '9'))
        {
            rid->type = DONNA_ARG_TYPE_PATH;
            if (s == data->start)
                rid->ptr = g_strdup (s);
            else
            {
                rid->ptr = s;
                s = NULL;
            }
        }
        else if (*s == '<')
        {
            gsize len = strlen (s) - 1;

            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid node reference: '%s'",
                        data->command->name, data->i + 1, s);
                goto error;
            }
            ptr = donna_app_get_int_ref (data->app, s, DONNA_ARG_TYPE_NODE);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        data->command->name, data->i + 1, s);
                goto error;
            }
            rid->type = DONNA_ARG_TYPE_NODE;
            rid->ptr  = ptr;
        }
        else
        {
            GError *err = NULL;

            ptr = get_node (data->app, s, &err);
            if (!ptr)
            {
                g_free (rid);
                g_propagate_prefixed_error (error, err, "Command '%s', argument %d: ",
                        data->command->name, data->i + 1);
                goto error;
            }
            rid->ptr = ptr;
            rid->type = DONNA_ARG_TYPE_NODE;
        }
        g_ptr_array_add (data->arr, rid);
    }
    else if (G_UNLIKELY (data->command->arg_type[data->i] == DONNA_ARG_TYPE_NOTHING))
    {
        /* NOTHING cannot be used on args */
        g_warning ("convert_arg() called for DONNA_ARG_TYPE_NOTHING");
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                "Command '%s', argument %d: Invalid argument type",
                data->command->name, data->i + 1);
        goto error;
    }
inner_next:
    data->start[end - data->start] = c;
    if (s != data->start && s)
        g_free (s);

next:
    if (*end == ')')
        data->start = end;
    else
    {
        data->start = end + 1;
        skip_blank (data->start);
    }
    return DONNA_TASK_DONE;

error:
    data->start[end - data->start] = c;
    if (s != data->start)
        g_free (s);
    return DONNA_TASK_FAILED;
}

/* for parsing/running commands from treeview, menu or toolbar */

struct cmd_run_data
{
    gboolean is_heap;
    /* to show error message on FAILED */
    DonnaApp *app;
    /*  needed to free args below */
    DonnaCommand *command;
    /* is set, must be free-d */
    GPtrArray *args;
};

static void
free_cmd_run_data (struct cmd_run_data *data)
{
    if (data->args)
        free_command_args (data->command, data->args);
    if (data->is_heap)
        g_slice_free (struct cmd_run_data, data);
}

static void
command_run_no_free_cb (DonnaTask *task, gboolean timeout_called, DonnaApp *app)
{
    if (donna_task_get_state (task) == DONNA_TASK_FAILED)
        donna_app_show_error (app, donna_task_get_error (task),
                "Action triggered failed");
}

static void
command_run_cb (DonnaTask *task, gboolean timeout_called, struct cmd_run_data *data)
{
    command_run_no_free_cb (task, timeout_called, data->app);
    free_cmd_run_data (data);
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

#define set_error(task, app, err, ...)    do {  \
    if (task)                                   \
    {                                           \
        if (err)                                \
            donna_task_take_error (task, err);  \
        else                                    \
            donna_task_set_error (task,         \
                    DONNA_COMMAND_ERROR,              \
                    DONNA_COMMAND_ERROR_OTHER,        \
                    __VA_ARGS__);               \
    }                                           \
    else                                        \
        show_error (app, err, __VA_ARGS__);     \
} while (0)

static DonnaTaskState
run_command (DonnaTask *task, struct rc_data *data)
{
    GError *err = NULL;
    DonnaTask *cmd_task;
    DonnaTaskState state;
    struct cmd_run_data cmd_run_data = { 0, };
    struct cmd_run_data *cr_data;

    if (!data->command)
    {
        data->command = _donna_command_init_parse (
                (streqn (data->fl, "command:", 8)) ? data->fl + 8 : data->fl,
                &data->start, &err);
        if (!data->command)
        {
            set_error (task, data->app, err,
                    "Cannot trigger node, parsing command failed");
            free_rc_data (data);
            return DONNA_TASK_FAILED;
        }

        data->arr = g_ptr_array_sized_new (data->command->argc + 3);
        g_ptr_array_add (data->arr, task);
    }

    for ( ; data->i < data->command->argc; ++data->i)
    {
        DonnaTaskState parsed;

        parsed = parse_arg (data, &err);
        if (parsed != DONNA_TASK_DONE)
        {
            if (parsed == DONNA_TASK_FAILED)
                set_error (task, data->app, err,
                        "Cannot trigger node, parsing argument %d failed",
                        data->i + 1);
            free_rc_data (data);
            return parsed;
        }

        if (*data->start == ')' && data->i + 1 < data->command->argc)
        {
            guint j;

            /* this was the last argument specified, but command has more */

            for (j = data->i + 1; j < data->command->argc; ++j)
                if (!(data->command->arg_type[j] & DONNA_ARG_IS_OPTIONAL))
                    break;

            if (j >= data->command->argc)
            {
                /* allow missing arg(s) if they're optional */
                for (data->i = data->i + 1;
                        data->i < data->command->argc;
                        ++data->i)
                    g_ptr_array_add (data->arr, NULL);
                g_clear_error (&err);
            }
            else
            {
                set_error (task, data->app, err,
                        "Cannot trigger node: Command '%s', argument %d required",
                        data->command->name, j + 1);
                free_rc_data (data);
                return DONNA_TASK_FAILED;
            }
        }
    }

    if (*data->start != ')')
    {
        set_error (task, data->app, err,
                "Cannot trigger node: Command '%s': Too many arguments: %s",
                data->command->name, data->start);
        free_rc_data (data);
        return DONNA_TASK_FAILED;
    }

    /* add DonnaApp* as extra arg for command */
    g_ptr_array_add (data->arr, data->app);

    cmd_task = donna_task_new ((task_fn) data->command->cmd_fn, data->arr, NULL);
    DONNA_DEBUG (TASK,
            donna_task_take_desc (cmd_task, g_strdup_printf (
                    "run command: %s", data->fl)));
    donna_task_set_visibility (cmd_task, data->command->visibility);

    if (!task)
    {
        /* !task == we're in thread UI, this is the trigger of an action. If the
         * command has a visibility GUI or FAST then we know it'll run
         * blockingly, and we don't need to alloc on heap. */
        if (data->command->visibility == DONNA_TASK_VISIBILITY_INTERNAL_GUI
                || data->command->visibility == DONNA_TASK_VISIBILITY_INTERNAL_FAST)
            cr_data = &cmd_run_data;
        else
        {
            cr_data = g_slice_new0 (struct cmd_run_data);
            cr_data->is_heap = TRUE;
            cr_data->command = data->command;
            cr_data->args = data->arr;
            data->arr = NULL;
        }
        cr_data->app = data->app;

        donna_task_set_callback (cmd_task, (task_callback_fn) command_run_cb,
                cr_data, (GDestroyNotify) free_cmd_run_data);
    }

    if (task)
        /* avoid starting another thread, since we're already in one */
        donna_task_set_can_block (g_object_ref_sink (cmd_task));
    donna_app_run_task (data->app, cmd_task);
    if (task)
    {
        donna_task_wait_for_it (cmd_task);
        state = donna_task_get_state (cmd_task);
        g_object_unref (cmd_task);
    }
    else
        state = DONNA_TASK_DONE;
    free_rc_data (data);
    return state;
}

/* shared private API */

DonnaCommand *
_donna_command_init_parse (gchar     *cmdline,
                           gchar    **first_arg,
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
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_NOT_FOUND,
                "Command '%s' does not exists", cmdline);
        *s = c;
        return NULL;
    }
    *s = c;

    skip_blank (s);
    if (*s != '(')
    {
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Command '%s': arguments not found, missing '('",
                commands[i].name);
        return NULL;
    }

    ++s;
    skip_blank (s);
    if (first_arg)
        *first_arg = s;

    return &commands[i];
}

/* used from provider-command */
DonnaTaskState
_donna_command_run (DonnaTask *task, struct _donna_command_run *cr)
{
    GError *err = NULL;
    struct rc_data data;
    DonnaTask *cmd_task;
    DonnaTaskState ret;

    memset (&data, 0, sizeof (struct rc_data));
    data.app = cr->app;

    data.command = _donna_command_init_parse (cr->cmdline, &data.start, &err);
    if (!data.command)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    data.arr = g_ptr_array_sized_new (data.command->argc + 2);
    g_ptr_array_add (data.arr, task);
    for (data.i = 0; data.i < data.command->argc; ++data.i)
    {
        DonnaTaskState parsed;

        parsed = parse_arg (&data, &err);
        if (parsed != DONNA_TASK_DONE)
        {
            if (parsed == DONNA_TASK_FAILED)
                donna_task_take_error (task, err);
            free_command_args (data.command, data.arr);
            return parsed;
        }

        if (*data.start == ')' && data.i + 1 < data.command->argc)
        {
            guint j;

            /* this was the last argument specified, but command has more */

            for (j = data.i + 1; j < data.command->argc; ++j)
                if (!(data.command->arg_type[j] & DONNA_ARG_IS_OPTIONAL))
                    break;

            if (j >= data.command->argc)
            {
                /* allow missing arg(s) if they're optional */
                for (data.i = data.i + 1; data.i < data.command->argc; ++data.i)
                    g_ptr_array_add (data.arr, NULL);
                g_clear_error (&err);
            }
            else
            {
                donna_task_set_error (task, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_MISSING_ARG,
                        "Command '%s', argument %d required",
                        data.command->name, j + 1);
                free_command_args (data.command, data.arr);
                return DONNA_TASK_FAILED;
            }
        }
    }

    if (*data.start != ')')
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Command '%s': Too many arguments: %s",
                data.command->name, data.start);
        free_command_args (data.command, data.arr);
        return DONNA_TASK_FAILED;
    }

    /* add DonnaApp* as extra arg for command */
    g_ptr_array_add (data.arr, cr->app);

    /* run the command */
    cmd_task = donna_task_new ((task_fn) data.command->cmd_fn, data.arr, NULL);
    DONNA_DEBUG (TASK,
            donna_task_take_desc (cmd_task, g_strdup_printf (
                    "run command: %s", cr->cmdline)));
    donna_task_set_visibility (cmd_task, data.command->visibility);
    donna_task_set_can_block (g_object_ref_sink (cmd_task));
    donna_app_run_task (cr->app, cmd_task);
    donna_task_wait_for_it (cmd_task);
    ret = donna_task_get_state (cmd_task);
    /* because the "parent task" (task) was given to cmd_task as args[0] (in
     * arr) it is in that task that any error/return value will have been set */
    g_object_unref (cmd_task);

    /* free args */
    free_command_args (data.command, data.arr);

    _donna_command_free_cr (cr);
    return ret;
}

void
_donna_command_free_cr (struct _donna_command_run *cr)
{
    g_free (cr->cmdline);
    g_free (cr);
}

/* used from actions (clicks/keys on treeview/menu/toolbar/etc) to deal with
 * converting %n flags */
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

    matches = g_new (gint, nb + 1);
    for (i = 0; i < nb; ++i)
        matches[i] = i;
    matches[nb] = -1;

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
cmd_ask_text (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    GPtrArray *other;
    GValue *v;
    gchar *s;

    other = args->pdata[4];
    if (other)
        /* we need to make it NULL-terminated for ask_text() */
        g_ptr_array_add (other, NULL);

    s = donna_app_ask_text (args->pdata[5], args->pdata[1], args->pdata[2],
                args->pdata[3], (other) ? (const gchar **) other->pdata : NULL,
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
cmd_config_get_boolean (DonnaTask *task, GPtrArray *args)
{
    GValue *v;
    gboolean val;

    if (!donna_config_get_boolean (donna_app_peek_config (args->pdata[2]), &val,
                "%s", args->pdata[1]))
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_BOOLEAN);
    g_value_set_boolean (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_int (DonnaTask *task, GPtrArray *args)
{
    GValue *v;
    gint val;

    if (!donna_config_get_int (donna_app_peek_config (args->pdata[2]), &val,
                "%s", args->pdata[1]))
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_INT);
    g_value_set_int (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_get_string (DonnaTask *task, GPtrArray *args)
{
    GValue *v;
    gchar *val;

    if (!donna_config_get_string (donna_app_peek_config (args->pdata[2]), &val,
                "%s", args->pdata[1]))
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, G_TYPE_STRING);
    g_value_take_string (v, val);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_boolean (DonnaTask *task, GPtrArray *args)
{
    if (!donna_config_set_boolean (donna_app_peek_config (args->pdata[3]),
                GPOINTER_TO_INT (args->pdata[2]), "%s", args->pdata[1]))
        return DONNA_TASK_FAILED;
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_int (DonnaTask *task, GPtrArray *args)
{
    if (!donna_config_set_int (donna_app_peek_config (args->pdata[3]),
                GPOINTER_TO_INT (args->pdata[2]), "%s", args->pdata[1]))
        return DONNA_TASK_FAILED;
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_config_set_string (DonnaTask *task, GPtrArray *args)
{
    if (!donna_config_set_string (donna_app_peek_config (args->pdata[3]),
                args->pdata[2], "%s", args->pdata[1]))
        return DONNA_TASK_FAILED;
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_menu_popup (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    /* we give our ref to show_menu(), but since the command is gonna be done
     * almost instantly and args free-d, we need to add one */
    if (!donna_app_show_menu (args->pdata[3], g_ptr_array_ref (args->pdata[1]),
                args->pdata[2], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
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
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
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
                donna_task_take_error (task, err);
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
        donna_task_take_error (task, err);
        g_object_unref (tree);
        return DONNA_TASK_FAILED;
    }
    g_object_unref (tree);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_node_new_child (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const GValue *v;
    GValue *value;
    DonnaTaskState ret;
    DonnaTask *t;
    const gchar *choices[] = { "item", "container" };
    DonnaNodeType type[] = { DONNA_NODE_ITEM, DONNA_NODE_CONTAINER };
    gint c;

    c = get_choice_from_arg (choices, 2);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Cannot create new child, unknown type '%s'; "
                "Must be 'item' or 'container'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_new_child_task (args->pdata[1], type[c], args->pdata[3], &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (args->pdata[4], t);
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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid type of node children: '%s'; Must be 'item', 'container' or 'all'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_get_children_task (args->pdata[1], children[c], &err);
    if (!t)
    {
        donna_task_take_error (task, err);
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
            donna_task_take_error (task, err);
        }
        else
        {
            gchar *fl = donna_node_get_full_location (args->pdata[1]);
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
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
            donna_task_take_error (task, g_error_copy (err));
    }
    g_object_unref (t);
    return state;
}

static DonnaTaskState
cmd_nodes_io (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_io_type[] = { "copy", "move", "delete" };
    DonnaIoType io_type[] = { DONNA_IO_COPY, DONNA_IO_MOVE, DONNA_IO_DELETE };
    gint c_io;

    c_io = get_choice_from_arg (c_io_type, 2);
    if (c_io < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid type of IO operation: '%s'; "
                "Must be 'copy', 'move' or 'delete'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_nodes_io (args->pdata[4], args->pdata[1],
                io_type[c_io], args->pdata[3], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_add_nodes (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_app_register_add_nodes (args->pdata[3],
                (args->pdata[1]) ? args->pdata[1] : "",
                args->pdata[2], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_drop (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_app_register_drop (args->pdata[2],
                (args->pdata[1]) ? args->pdata[1] : "", &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_get_nodes (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_drop[] = { "not", "always", "on-cut" };
    DonnaDropRegister drop[] = { DONNA_DROP_REGISTER_NOT, DONNA_DROP_REGISTER_ALWAYS,
        DONNA_DROP_REGISTER_ON_CUT };
    gint c;
    GPtrArray *arr;
    GValue *value;

    c = get_choice_from_arg (c_drop, 2);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid drop option: '%s'; Must be 'not', 'always' or 'on-cut'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_register_get_nodes (args->pdata[3],
                (args->pdata[1]) ? args->pdata[1] : "",
                drop[c], NULL, &arr, &err))
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
cmd_register_get_type (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaRegisterType type;
    GValue *value;
    const gchar *s_type[3];
    s_type[DONNA_REGISTER_UNKNOWN]  = "unknown";
    s_type[DONNA_REGISTER_CUT]      = "cut";
    s_type[DONNA_REGISTER_COPY]     = "copy";

    if (!donna_app_register_get_nodes (args->pdata[2],
                (args->pdata[1]) ? args->pdata[1] : "",
                DONNA_DROP_REGISTER_NOT, &type, NULL, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_STRING);
    g_value_set_static_string (value, s_type[type]);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_load (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_file_type[] = { "nodes", "files", "uris" };
    DonnaRegisterFile file_type[] = { DONNA_REGISTER_FILE_NODES,
        DONNA_REGISTER_FILE_FILE, DONNA_REGISTER_FILE_URIS };
    gint c;

    if (args->pdata[3])
    {
        c = get_choice_from_arg (c_file_type, 3);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Invalid register file type: '%s'; Must be 'nodes', 'files' or 'uris'",
                    args->pdata[3]);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c = 0;

    if (!donna_app_register_load (args->pdata[4],
                (args->pdata[1]) ? args->pdata[1] : "",
                args->pdata[2], file_type[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_nodes_io (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_io_type[] = { "auto", "copy", "move", "delete" };
    DonnaIoType io_type[] = { DONNA_IO_UNKNOWN, DONNA_IO_COPY, DONNA_IO_MOVE,
        DONNA_IO_DELETE };
    DonnaDropRegister drop;
    DonnaRegisterType reg_type;
    gint c_io;
    GPtrArray *nodes;

    if (args->pdata[2])
    {
        c_io = get_choice_from_arg (c_io_type, 2);
        if (c_io < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Invalid type of IO operation: '%s'; "
                    "Must be 'auto', 'copy', 'move' or 'delete'",
                    args->pdata[2]);
            return DONNA_TASK_FAILED;
        }
    }
    else
        /* default to 'auto' */
        c_io = 0;

    switch (c_io)
    {
        case 0:
            drop = DONNA_DROP_REGISTER_ON_CUT;
            break;
        case 1:
            drop = DONNA_DROP_REGISTER_NOT;
            break;
        case 2:
        case 3:
            drop = DONNA_DROP_REGISTER_ALWAYS;
            break;
    }

    if (!donna_app_register_get_nodes (args->pdata[4],
                (args->pdata[1]) ? args->pdata[1] : "",
                drop, &reg_type, &nodes, &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    if (c_io == 0)
        c_io = (reg_type == DONNA_REGISTER_CUT) ? 2 : 1;

    if (!donna_app_nodes_io (args->pdata[4], nodes, io_type[c_io],
                args->pdata[3], &err))
    {
        donna_task_take_error (task, err);
        g_ptr_array_unref (nodes);
        return DONNA_TASK_FAILED;
    }
    g_ptr_array_unref (nodes);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_save (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_file_type[] = { "nodes", "files", "uris" };
    DonnaRegisterFile file_type[] = { DONNA_REGISTER_FILE_NODES,
        DONNA_REGISTER_FILE_FILE, DONNA_REGISTER_FILE_URIS };
    gint c;

    if (args->pdata[3])
    {
        c = get_choice_from_arg (c_file_type, 3);
        if (c < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Invalid register file type: '%s'; Must be 'nodes', 'files' or 'uris'",
                    args->pdata[3]);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c = 0;

    if (!donna_app_register_save (args->pdata[4],
                (args->pdata[1]) ? args->pdata[1] : "",
                args->pdata[2], file_type[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_set (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_type[] = { "cut", "copy" };
    DonnaRegisterType type[] = { DONNA_REGISTER_CUT, DONNA_REGISTER_COPY };
    gint c;

    c = get_choice_from_arg (c_type, 2);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid register type: '%s'; Must be 'cut' or 'copy'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_register_set (args->pdata[4],
                (args->pdata[1]) ? args->pdata[1] : "",
                type[c], args->pdata[3], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_register_set_type (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    const gchar *c_type[] = { "cut", "copy" };
    DonnaRegisterType type[] = { DONNA_REGISTER_CUT, DONNA_REGISTER_COPY };
    gint c;

    c = get_choice_from_arg (c_type, 2);
    if (c < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid register type: '%s'; Must be 'cut' or 'copy'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_app_register_set_type (args->pdata[3],
                (args->pdata[1]) ? args->pdata[1] : "",
                type[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_repeat (DonnaTask *task, GPtrArray *args)
{
    gint nb;

    nb = GPOINTER_TO_INT (args->pdata[1]);
    for (nb = MAX (1, nb); nb > 0; --nb)
    {
        struct rc_data data;

        memset (&data, 0, sizeof (struct rc_data));
        data.app          = args->pdata[3];
        data.fl           = g_strdup (args->pdata[2]);

        /* run_command() will take care of freeing data as/when needed */
        if (run_command (task, &data) != DONNA_TASK_DONE)
            return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command 'task_set_state' cannot be used on '%s', only works on node in domain 'task'",
                fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    c = get_choice_from_arg (choices, 2);
    if (c < 0)
    {
        gchar *d = donna_node_get_name (args->pdata[1]);
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
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
        donna_task_take_error (task, err);
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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Command 'task_toggle' cannot be used on '%s', only works on node in domain 'task'",
                fl);
        g_free (fl);
        return DONNA_TASK_FAILED;
    }

    t = donna_node_trigger_task (args->pdata[1], &err);
    if (!t)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (args->pdata[2], t);
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
cmd_tree_abort (DonnaTask *task, GPtrArray *args)
{
    donna_tree_view_abort (args->pdata[1]);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_activate_row (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_activate_row (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_add_root (DonnaTask *task, GPtrArray *args)
{
    GError *err= NULL;

    if (!donna_tree_view_add_root (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task, err);
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
        donna_task_take_error (task, err);
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
        donna_task_take_error (task, err);
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
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_location (DonnaTask *task, GPtrArray *args)
{
    DonnaNode *node;
    GValue *v;

    node = donna_tree_view_get_location (args->pdata[1]);
    if (!node)
        return DONNA_TASK_FAILED;

    v = donna_task_grab_return_value (task);
    g_value_init (v, DONNA_TYPE_NODE);
    g_value_take_object (v, node);
    donna_task_release_return_value (task);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_get_node_at_row (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    DonnaNode *node;
    GValue *v;

    node = donna_tree_view_get_node_at_row (args->pdata[1], args->pdata[2], &err);
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
cmd_tree_get_nodes (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;
    GPtrArray *arr;
    GValue *v;

    arr = donna_tree_view_get_nodes (args->pdata[1], args->pdata[2],
            (gboolean) GPOINTER_TO_INT (args->pdata[3]), &err);
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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'box', 'highlight' or 'clicks'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    c_s = get_choice_from_arg (c_source, 4);
    if (c_s < 0)
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
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
    const gchar *c_action[] = { "select", "unselect", "invert" };
    DonnaTreeSelAction action[] = { DONNA_TREE_SEL_SELECT, DONNA_TREE_SEL_UNSELECT,
        DONNA_TREE_SEL_INVERT };
    gchar *s;
    gint c_n;
    gint nb_sets;
    gint c_a;

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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot go to line, unknown set type '%s'. "
                "Must be (a '+'-separated combination of) 'scroll', 'focus' and/or 'cursor'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (args->pdata[5])
    {
        c_n = get_choice_from_arg (c_nb_type, 5);
        if (c_n < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot goto line, invalid type: '%s'; "
                    "Must be 'repeat', 'line' or 'percent'",
                    args->pdata[5]);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c_n = 0;

    if (args->pdata[6])
    {
        c_a = get_choice_from_arg (c_action, 6);
        if (c_a < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Cannot goto line, invalid selection action: '%s'; "
                    "Must be 'select', 'unselect' or 'invert'",
                    args->pdata[6]);
            return DONNA_TASK_FAILED;
        }
    }
    else
        c_a = -1;

    if (!donna_tree_view_goto_line (args->pdata[1], set, args->pdata[3],
                GPOINTER_TO_INT (args->pdata[4]), nb_type[c_n],
                (c_a < 0) ? 0 : action[c_a], GPOINTER_TO_INT (args->pdata[7]),
                &err))
    {
        donna_task_take_error (task, err);
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
        donna_task_take_error (task, err);
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
        donna_task_take_error (task, err);
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
        donna_task_set_error (task,
                DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid argument 'mode': '%s', expected 'visible', 'simple', 'normal' or 'reload'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_refresh (args->pdata[1], mode[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_remove_row (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_remove_row (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_reset_keys (DonnaTask *task, GPtrArray *args)
{
    donna_tree_view_reset_keys (args->pdata[1]);
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
        donna_task_set_error (task,
                DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Invalid argument 'action': '%s', expected 'select', 'unselect' or 'invert'",
                args->pdata[2]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_selection (args->pdata[1], action[c], args->pdata[3],
                (gboolean) GPOINTER_TO_INT (args->pdata[4]),
                &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_cursor (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_set_cursor (args->pdata[1], args->pdata[2],
                (gboolean) GPOINTER_TO_INT (args->pdata[3]), &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_focus (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_set_focus (args->pdata[1], args->pdata[2], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_key_mode (DonnaTask *task, GPtrArray *args)
{
    donna_tree_view_set_key_mode (args->pdata[1], args->pdata[2]);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_tree_set_location (DonnaTask *task, GPtrArray *args)
{
    GError *err = NULL;

    if (!donna_tree_view_set_location (args->pdata[1], args->pdata[2], &err))
    {
        if (err)
            donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }
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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_OTHER,
                "Cannot set tree visual, unknown type '%s'. "
                "Must be 'name', 'icon', 'box', 'highlight' or 'clicks'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_set_visual (args->pdata[1], args->pdata[2], visual[c],
                args->pdata[4], &err))
    {
        donna_task_take_error (task, err);
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
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Cannot toggle row, unknown toggle type '%s'. Must be 'standard', 'full' or 'maxi'",
                args->pdata[3]);
        return DONNA_TASK_FAILED;
    }

    if (!donna_tree_view_toggle_row (args->pdata[1], args->pdata[2], toggle[c], &err))
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_void (DonnaTask *task, GPtrArray *args)
{
    return DONNA_TASK_DONE;
}
