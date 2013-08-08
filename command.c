
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


static void
_g_string_append_quoted (GString *str, gchar *s)
{
    g_string_append_c (str, '"');
    for ( ; *s != '\0'; ++s)
    {
        if (*s == '"' || *s == '\\')
            g_string_append_c (str, '\\');

        g_string_append_c (str, *s);
    }
    g_string_append_c (str, '"');
}

/* helper function to parse FL for actions (clicks/keys from treeview, menu...) */
gchar *
_donna_command_parse_fl (DonnaApp       *app,
                         gchar          *_fl,
                         const gchar    *conv_flags,
                         _conv_flag_fn   conv_fn,
                         gpointer        conv_data,
                         GPtrArray     **intrefs)
{
    GString *str = NULL;
    gchar *fl = _fl;
    gchar *s = fl;

    while ((s = strchr (s, '%')))
    {
        gboolean dereference = s[1] == '*';
        gboolean match;

        if (!dereference)
            match = s[1] != '\0' && strchr (conv_flags, s[1]) != NULL;
        else
            match = s[2] != '\0' && strchr (conv_flags, s[2]) != NULL;

        if (match)
        {
            DonnaArgType type;
            gpointer ptr;
            GDestroyNotify destroy = NULL;

            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, fl, s - fl);
            if (dereference)
                ++s;

            if (G_UNLIKELY (!conv_fn (s[1], &type, &ptr, &destroy, conv_data)))
            {
                fl = ++s;
                ++s;
                continue;
            }

            /* we don't need to test for all possible types, only those can make
             * sense. That is, it could be a ROW, but not a ROW_ID (or PATH)
             * since those only make sense the other way around (or as type of
             * ROW_ID) */

            if (type & DONNA_ARG_TYPE_TREEVIEW)
                g_string_append (str, donna_tree_view_get_name ((DonnaTreeView *) ptr));
            else if (type & DONNA_ARG_TYPE_ROW)
            {
                DonnaTreeRow *row = (DonnaTreeRow *) ptr;
                g_string_append_printf (str, "[%p;%p]", row->node, row->iter);
            }
            /* this will do nodes, array of nodes, array of strings */
            else if (type & (DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY))
            {
                if (dereference)
                {
                    if (type & DONNA_ARG_IS_ARRAY)
                    {
                        GString *str_arr;
                        GPtrArray *arr = (GPtrArray *) ptr;
                        guint i;

                        str_arr = g_string_new (NULL);
                        if (type & DONNA_ARG_TYPE_NODE)
                            for (i = 0; i < arr->len; ++i)
                            {
                                gchar *fl;
                                fl = donna_node_get_full_location (
                                        (DonnaNode *) arr->pdata[i]);
                                _g_string_append_quoted (str_arr, fl);
                                g_string_append_c (str_arr, ',');
                                g_free (fl);
                            }
                        else
                            for (i = 0; i < arr->len; ++i)
                            {
                                _g_string_append_quoted (str_arr,
                                        (gchar *) arr->pdata[i]);
                                g_string_append_c (str_arr, ',');
                            }

                        /* remove last comma */
                        g_string_truncate (str_arr, str_arr->len - 1);
                        /* str_arr is a list of quoted strings/FL, but we also
                         * need to quote the list itself */
                        _g_string_append_quoted (str, str_arr->str);
                        g_string_free (str_arr, TRUE);
                    }
                    else
                    {
                        gchar *fl;
                        fl = donna_node_get_full_location ((DonnaNode *) ptr);
                        _g_string_append_quoted (str, fl);
                        g_free (fl);
                    }
                }
                else
                {
                    gchar *s = donna_app_new_int_ref (app, type, ptr);
                    g_string_append (str, s);
                    if (!*intrefs)
                        *intrefs = g_ptr_array_new_with_free_func (g_free);
                    g_ptr_array_add (*intrefs, s);
                }
            }
            else if (type & DONNA_ARG_TYPE_STRING)
                _g_string_append_quoted (str, (gchar *) ptr);
            else if (type & DONNA_ARG_TYPE_INT)
                g_string_append_printf (str, "%d", * (gint *) ptr);

            if (destroy)
                destroy (ptr);

            s += 2;
            fl = s;
        }
        else if (s[1] != '\0')
        {
            if (!str)
                str = g_string_new (NULL);
            g_string_append_len (str, fl, s - fl);
            fl = ++s;
            ++s;
        }
        else
            break;
    }

    if (!str)
        return _fl;

    g_string_append (str, fl);
    g_free (_fl);
    return g_string_free (str, FALSE);
}

struct fir
{
    gboolean is_stack;
    DonnaApp *app;
    GPtrArray *intrefs;
};

static void
free_fir (struct fir *fir)
{
    guint i;

    for (i = 0; i < fir->intrefs->len; ++i)
        donna_app_free_int_ref (fir->app, fir->intrefs->pdata[i]);
    g_ptr_array_unref (fir->intrefs);
    if (!fir->is_stack)
        g_free (fir);
}

static gboolean
trigger_cb (DonnaTask *task, gboolean timeout_called, struct fir *fir)
{
    free_fir (fir);
}

static gboolean
get_node_cb (DonnaTask *task, gboolean timeout_called, DonnaApp *app)
{
    GError *err = NULL;
    struct fir *fir;
    DonnaNode *node;
    DonnaTask *t;

    if (donna_task_get_state (task) != DONNA_TASK_DONE)
    {
        donna_app_show_error (app, donna_task_get_error (task),
                "Failed to trigger action, couldn't get node");
        fir = g_object_get_data ((GObject *) task, "donna-fir");
        if (fir)
            free_fir (fir);
        return FALSE;
    }

    node = g_value_get_object (donna_task_get_return_value (task));
    t = donna_node_trigger_task (node, &err);
    if (G_UNLIKELY (!t))
    {
        donna_app_show_error (app, err,
                "Failed to trigger action, couldn't get task");
        g_clear_error (&err);
        fir = g_object_get_data ((GObject *) task, "donna-fir");
        if (fir)
            free_fir (fir);
        return FALSE;
    }

    /* see _donna_command_trigger_fl() for why this is blocking */
    if (timeout_called)
        donna_task_set_can_block (g_object_ref_sink (t));
    else
    {
        fir = g_object_get_data ((GObject *) task, "donna-fir");
        if (fir)
            donna_task_set_callback (t, (task_callback_fn) trigger_cb, fir, NULL);
    }

    donna_app_run_task (app, t);

    if (timeout_called)
    {
        gboolean ret;

        ret = donna_task_get_state (t) == DONNA_TASK_DONE;
        g_object_unref (t);
        return ret;
    }

    return TRUE;
}

/* helper function to trigger FL for actions (clicks/keys from treeview, menu...) */
gboolean
_donna_command_trigger_fl (DonnaApp     *app,
                           const gchar  *fl,
                           GPtrArray    *intrefs,
                           gboolean      blocking)
{
    DonnaTask *task;

    task = donna_app_get_node_task (app, fl);
    if (G_UNLIKELY (!task))
    {
        donna_app_show_error (app, NULL,
                "Failed to trigger action, couldn't get task get_node()");
        return FALSE;
    }
    if (blocking)
        donna_task_set_can_block (g_object_ref_sink (task));
    else
    {
        if (intrefs)
        {
            struct fir *fir;
            fir = g_new0 (struct fir, 1);
            fir->app = app;
            fir->intrefs = intrefs;
            g_object_set_data ((GObject *) task, "donna-fir", fir);
        }
        donna_task_set_callback (task, (task_callback_fn) get_node_cb, app, NULL);
    }

    donna_app_run_task (app, task);

    if (blocking)
    {
        gboolean ret;

        /* we're abusing timeout_called here, since we don't use a timeout it
         * should always be FALSE. If TRUE, that'll mean be blocking */
        ret = get_node_cb (task, TRUE, app);
        g_object_unref (task);
        if (intrefs)
        {
            struct fir fir = { TRUE, app, intrefs };
            free_fir (&fir);
        }
        return ret;
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
