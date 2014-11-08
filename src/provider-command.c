/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-command.c
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

#include <gtk/gtk.h>
#include <glib-unix.h>
#include "provider-command.h"
#include "command.h"
#include "provider.h"
#include "treeview.h"
#include "node.h"
#include "terminal.h"
#include "task.h"
#include "util.h"
#include "macros.h"
#include "debug.h"

/**
 * SECTION:provider-command
 * @Short_description: The commands at your disposal in donna
 *
 * As you probably know, about everything that can be done in donna is done via
 * commands, which you can use from keys, clicks, on (context) menus, etc
 *
 * Commands are used by simply specifying a node to trigger, in domain "command"
 * and with for location the actual command to execute, with its arguments (if
 * any).
 *
 * Because this is a node, you can therefore use them in donna just like you'd
 * use other nodes.
 *
 * Locations must always start with the command to execute, followed by
 * parenthesis. In between parenthesis can be found arguments, separated by
 * commas. If an argument can include a comma or parenthesis, simply quote it
 * using double quotes (`"`).
 *
 * To then use quotes, you'll need to escape them using backslash
 * (`\`), which must also be escaped to be used literally.
 *
 * Commands can have arguments, and a return value, all of which have a type.
 * Obviously some automatic convertion to string representation is performed as
 * needed. For instance, when a treeview is used, its name will actually be
 * used. For nodes, you can simply use a full location.
 *
 * It is also possible to use arrays (of nodes or strings), in which case you
 * need to quote the entire array, quoting each element if needed. For example,
 * an array of nodes could be used as such:
 * `"\"/tmp/foo\",\"/tmp/bar\""`
 * Although, because quotes aren't required here, this would also work:
 * `"/tmp/foo,/tmp/bar"`
 *
 * When an argument is an array, it is possible to simply specify one element.
 * Similarly, when an argument takes e.g. a node, and an array of nodes is given
 * (e.g. return value of a command), if the array contains exactly one
 * element/node, it will work. If more elements are present, it obviously won't.
 *
 * It is possible to use another command's return value as argument, by using
 * the at sign (`@`) followed by the command. In such a case, this command will
 * first be executed, and its return value used as argument.
 * If the return value isn't of the same type as the argument, then it will be
 * converted to a string representation. So e.g. using a command returning a
 * node as argument of anything other than a(n array of) node(s) will have the
 * node's full location be used as argument.
 *
 * Nodes for commands are items that can be triggered. It is however possible to
 * prefix a command/location with the lesser than sign (&lt;) so that the
 * corresponding node isn't an item, but a container.
 *
 * Then, when getting children of said container/node, the command will run as
 * usual and the returned nodes be its children. Obviously, this is only
 * supported for commands whose return value is a(n array of) node(s).
 * The main benefit is to be able to use such commands where containers are
 * used, e.g. one could do:
 * `command:node_popup_children ("command:&lt;mru_get_nodes (id)",a)`
 * (Noting that in this case, command `menu_popup()` could have been used
 * insead, of course. But e.g. in context menus where a container (as submenu)
 * is to be used, this might prove useful.)
 *
 * For the list of all supported commands, refer to
 * #donnatella-Commands.description
 */

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

enum which
{
    WHICH_TRIGGER,
    WHICH_HAS_CHILDREN,
    WHICH_GET_CHILDREN
};

struct run_command
{
    enum which               which;
    gboolean                 is_subcommand;
    DonnaProviderCommand    *pc;
    gchar                   *cmdline;
    struct command          *command;
    gchar                   *start;
    guint                    i;
    struct run_command      *sub_rc;
    GPtrArray               *args;
    DonnaTask               *task;
    task_run_fn              run_task;
    gpointer                 run_task_data;
    /* for WHICH_HAS_CHILDREN || WHICH_GET_CHILDREN */
    DonnaNode               *node;
    DonnaNodeType            node_types;
};

struct _DonnaProviderCommandPrivate
{
    DonnaApp *app;
    GMutex mutex;
    GHashTable *commands;
};

static void             provider_command_get_property   (GObject        *object,
                                                         guint           prop_id,
                                                         GValue         *value,
                                                         GParamSpec     *pspec);
static void             provider_command_set_property   (GObject        *object,
                                                         guint           prop_id,
                                                         const GValue   *value,
                                                         GParamSpec     *pspec);
static void             provider_command_finalize       (GObject        *object);

/* DonnaProvider */
static const gchar *    provider_command_get_domain     (DonnaProvider      *provider);
static DonnaProviderFlags provider_command_get_flags    (DonnaProvider      *provider);
static DonnaTask *      provider_command_trigger_node_task (
                                                         DonnaProvider      *provider,
                                                         DonnaNode          *node,
                                                         GError            **error);
static DonnaTask *      provider_command_has_node_children_task (
                                                         DonnaProvider      *provider,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types,
                                                         GError            **error);
static DonnaTask *      provider_command_get_node_children_task (
                                                         DonnaProvider      *provider,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types,
                                                         GError            **error);
/* DonnaProviderBase */
static DonnaTaskState   provider_command_new_node       (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);

/* internal from command.c */
void _donna_add_commands (GHashTable *commands);

static void             free_run_command                (gpointer            data);
static void             parse_command                   (struct run_command *rc);
static void             pre_run_command                 (DonnaTask          *task,
                                                         task_run_fn         run_task,
                                                         gpointer            run_task_data,
                                                         struct run_command *rc);
static DonnaTaskState   run_command                     (DonnaTask          *task,
                                                         struct run_command *rc);


static void
provider_command_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain               = provider_command_get_domain;
    interface->get_flags                = provider_command_get_flags;
    interface->trigger_node_task        = provider_command_trigger_node_task;
    interface->has_node_children_task   = provider_command_has_node_children_task;
    interface->get_node_children_task   = provider_command_get_node_children_task;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderCommand, donna_provider_command,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_command_provider_init)
        )

static void
donna_provider_command_class_init (DonnaProviderCommandClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->new_node      = provider_command_new_node;

    o_class = (GObjectClass *) klass;
    o_class->get_property   = provider_command_get_property;
    o_class->set_property   = provider_command_set_property;
    o_class->finalize       = provider_command_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaProviderCommandPrivate));
}

static void
free_command (struct command *command)
{
    g_free (command->name);
    g_free (command->arg_type);
    if (command->destroy && command->data)
        command->destroy (command->data);
    g_slice_free (struct command, command);
}

static void
donna_provider_command_init (DonnaProviderCommand *provider)
{
    DonnaProviderCommandPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_COMMAND,
            DonnaProviderCommandPrivate);
    g_mutex_init (&priv->mutex);
    priv->commands = g_hash_table_new_full (g_str_hash, g_str_equal,
            NULL, (GDestroyNotify) free_command);
    /* load commands, since we're in init there's no need to lock */
    _donna_add_commands (priv->commands);
}

static void
provider_command_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
    if (prop_id == PROP_APP)
        g_value_set_object (value, ((DonnaProviderCommand *) object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_command_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
    if (prop_id == PROP_APP)
    {
        ((DonnaProviderCommand *) object)->priv->app = g_value_dup_object (value);
        G_OBJECT_CLASS (donna_provider_command_parent_class)->set_property (
                object, prop_id, value, pspec);
    }
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_command_finalize (GObject *object)
{
    DonnaProviderCommandPrivate *priv;

    priv = DONNA_PROVIDER_COMMAND (object)->priv;
    g_object_unref (priv->app);
    g_hash_table_unref (priv->commands);
    g_mutex_clear (&priv->mutex);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_command_parent_class)->finalize (object);
}

static DonnaProviderFlags
provider_command_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_COMMAND (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_command_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_COMMAND (provider), NULL);
    return "command";
}

static DonnaTaskState
provider_command_new_node (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           const gchar        *location)
{
    GError *err = NULL;
    DonnaProviderBaseClass *klass;
    struct command *cmd;
    DonnaNode *node;
    DonnaNode *n;
    GValue *value;
    GValue v = G_VALUE_INIT;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);

    cmd = _donna_command_init_parse ((DonnaProviderCommand *) _provider,
            (gchar *) location, NULL, &err);
    if (!cmd)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    /* container mode only works for commands returning a(n array of) node(s) */
    if (*location == '<' && !(cmd->return_type & DONNA_ARG_TYPE_NODE))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider '%s': Command '%s' cannot be a container, "
                "only commands returning a(n array of) node(s) can",
                "command", cmd->name);
        return DONNA_TASK_FAILED;
    }

    node = donna_node_new ((DonnaProvider *) _provider, location,
            (*location == '<') ? DONNA_NODE_CONTAINER : DONNA_NODE_ITEM,
            NULL, /* filename */
            DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            NULL, (refresher_fn) gtk_true, NULL,
            cmd->name,
            DONNA_NODE_ICON_EXISTS);
    if (!node)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'command': Unable to create a new node");
        return DONNA_TASK_FAILED;
    }

    g_value_init (&v, G_TYPE_ICON);
    g_value_take_object (&v, g_themed_icon_new ("applications-system"));
    donna_node_set_property_value (node, "icon", &v);
    g_value_unset (&v);

    /* "preload" the type of visibility neede for the task trigger_node */
    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, cmd->visibility);
    if (!donna_node_add_property (node, "trigger-visibility",
                G_TYPE_UINT, &v,
                DONNA_TASK_VISIBILITY_INTERNAL_FAST,
                NULL, (refresher_fn) gtk_true,
                NULL,
                NULL, NULL,
                &err))
    {
        g_prefix_error (&err, "Provider 'command': Cannot create new node, "
                "failed to add property 'trigger-visibility': ");
        donna_task_take_error (task, err);
        g_value_unset (&v);
        g_object_unref (node);
        return DONNA_TASK_FAILED;
    }
    g_value_unset (&v);

    klass->lock_nodes (_provider);
    n = klass->get_cached_node (_provider, location);
    if (n)
    {
        /* already one added while we were busy */
        g_object_unref (node);
        node = n;
    }
    else
        klass->add_node_to_cache (_provider, node);
    klass->unlock_nodes (_provider);

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    /* take_object to not increment the ref count, as it was already done for
     * this task in add_node_to_cache() */
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static void
free_command_args (struct command *command, GPtrArray *args)
{
    guint i;

    /* we use arr->len because the array might not be fuly filled, in case of
     * error halfway through parsing */
    for (i = 0; i < args->len; ++i)
    {
        if (!args->pdata[i])
            /* arg must have been optional & not specified */
            continue;
        else if (command->arg_type[i] & DONNA_ARG_IS_ARRAY)
            g_ptr_array_unref (args->pdata[i]);
        else if (command->arg_type[i] & (DONNA_ARG_TYPE_TREE_VIEW
                    | DONNA_ARG_TYPE_NODE | DONNA_ARG_TYPE_TERMINAL))
            g_object_unref (args->pdata[i]);
        else if (command->arg_type[i] & (DONNA_ARG_TYPE_STRING
                    | DONNA_ARG_TYPE_ROW | DONNA_ARG_TYPE_PATH))
            g_free (args->pdata[i]);
        else if (command->arg_type[i] & DONNA_ARG_TYPE_ROW_ID)
        {
            DonnaRowId *rowid;

            rowid = args->pdata[i];
            if (rowid->type == DONNA_ARG_TYPE_NODE)
                g_object_unref (rowid->ptr);
            else
                g_free (rowid->ptr);

            g_free (rowid);
        }
    }
    g_ptr_array_unref (args);
}

static void
free_run_command (gpointer data)
{
    struct run_command *rc = data;

    if (rc->is_subcommand)
        return;
    if (rc->sub_rc)
    {
        /* so it gets free-d */
        rc->sub_rc->is_subcommand = FALSE;
        /* because it points into our own rc->cmdline */
        rc->sub_rc->cmdline = NULL;

        free_run_command (rc->sub_rc);
    }
    free_command_args (rc->command, rc->args);
    g_free (rc->cmdline);
    if (rc->node)
        g_object_unref (rc->node);
    g_slice_free (struct run_command, rc);
}

struct command *
_donna_command_init_parse (DonnaProviderCommand    *pc,
                           gchar                   *cmdline,
                           gchar                  **first_arg,
                           GError                 **error)
{
    DonnaProviderCommandPrivate *priv = pc->priv;
    struct command *command;
    gchar  c;
    gchar *s;

    /* containe prefix, to be skipped */
    if (*cmdline == '<')
        ++cmdline;

    for (s = cmdline; isalnum (*s) || *s == '_'; ++s)
        ;
    c  = *s;
    *s = '\0';

    g_mutex_lock (&priv->mutex);
    command = g_hash_table_lookup (priv->commands, cmdline);
    g_mutex_unlock (&priv->mutex);

    if (!command)
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
                command->name);
        return NULL;
    }

    ++s;
    skip_blank (s);
    if (first_arg)
        *first_arg = s;

    return command;
}

/* If *start is a quoted string, unquotes it; else return FALSE. Unquoting means
 * moving *start after the opening quote, taking care of unescaping any escaped
 * character and puttin a NUL at the end, which will eiher be the ending quote,
 * or before in case some unescaping was done. *end will always point after
 * the ending quote (which may or may not have been turned into a NUL).
 * Returns TRUE was on sucessfull unquoting, FALSE on error (no ending quote) or
 * if the string wasn't quoted. Either check first, or use error to tell. */
static gboolean
unquote_string (gchar **start, gchar **end, GError **error)
{
    if (**start != '"')
        return FALSE;

    *end = *start;
    ++*start;
    if (!donna_unquote_string (end))
    {
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Missing ending quote");
        return FALSE;
    }

    return TRUE;
}

static gboolean
subcommand_cb (gint fd, GIOCondition condition, struct run_command *rc)
{
    DonnaTaskState state;

    state = donna_task_get_state (rc->sub_rc->task);

    if (state == DONNA_TASK_CANCELLED)
    {
        g_object_unref (rc->sub_rc->task);
        donna_task_set_preran (rc->task, DONNA_TASK_CANCELLED,
                rc->run_task, rc->run_task_data);
        return G_SOURCE_REMOVE;
    }
    else if (state != DONNA_TASK_DONE)
    {
        const GError *e;
        GError *err = NULL;

        e = donna_task_get_error (rc->sub_rc->task);
        if (e)
        {
            err = g_error_copy (e);
            g_prefix_error (&err, "Command '%s', argument %d: "
                    "Command %s%s%s failed: ",
                    rc->command->name, rc->i + 1,
                    (rc->sub_rc->command) ? "'" : "",
                    (rc->sub_rc->command) ? rc->sub_rc->command->name : "",
                    (rc->sub_rc->command) ? "' " : "");
            donna_task_take_error (rc->task, err);
        }
        else
            donna_task_set_error (rc->task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_OTHER,
                    "Command '%s', argument %d: "
                    "Command %s%s%sfailed (w/out error message)",
                    rc->command->name, rc->i + 1,
                    (rc->sub_rc->command) ? "'" : "",
                    (rc->sub_rc->command) ? rc->sub_rc->command->name : "",
                    (rc->sub_rc->command) ? "' " : "");

        g_object_unref (rc->sub_rc->task);
        donna_task_set_preran (rc->task, DONNA_TASK_FAILED,
                rc->run_task, rc->run_task_data);
        return G_SOURCE_REMOVE;
    }

    parse_command (rc);
    return G_SOURCE_REMOVE;
}

enum parse
{
    PARSE_DONE,
    PARSE_PENDING,  /* source on task for subcommand was set up */
    PARSE_FAILED
};

/* parse the next argument for a command. data must have been properly set &
 * initialized via _donna_command_init_parse() and therefore data->start points
 * to the beginning of the argument data->i
 * Will handle figuring out where the arg ends (can be quoted, can also be
 * another command to run & get its return value as arg, via @syntax), and
 * moving data->start & whatnot where needs be */
static enum parse
parse_arg (struct run_command    *rc,
           GError               **error)
{
    DonnaApp *app = rc->pc->priv->app;
    GError *err = NULL;
    gchar *end;
    gchar *s;
    gchar c;

    if (unquote_string (&rc->start, &end, &err))
    {
        skip_blank (end);
    }
    else if (err)
    {
        g_propagate_prefixed_error (error, err, "Command '%s', argument %d: ",
                rc->command->name, rc->i + 1);
        return PARSE_FAILED;
    }
    else if (*rc->start == '@')
    {
        struct run_command *sub_rc;
        DonnaTask *task;
        const GValue *v;

        /* do we need to start a task for subcommand */
        if (!rc->sub_rc)
        {
            struct command *command;
            GSource *source;

            s = rc->start + ((rc->start[1] == '*') ? 2 : 1);
            /* do the _donna_command_init_parse to known which command this is,
             * making sure it exists, and get its visibility */
            command = _donna_command_init_parse (rc->pc, s, NULL, &err);
            if (!command)
            {
                g_propagate_prefixed_error (error, err, "Command '%s', argument %d: ",
                        rc->command->name, rc->i + 1);
                return PARSE_FAILED;
            }

            sub_rc = g_slice_new0 (struct run_command);
            sub_rc->is_subcommand = TRUE;
            sub_rc->pc = rc->pc;
            sub_rc->command = command;
            sub_rc->cmdline = s;

            task = donna_task_new ((task_fn) run_command, sub_rc, free_run_command);
            donna_task_set_pre_worker (task, (task_pre_fn) pre_run_command);
            donna_task_set_visibility (task, command->visibility);

            DONNA_DEBUG (TASK, NULL,
                    donna_task_take_desc (task, g_strdup_printf (
                            "trigger for subcommand '%s'", sub_rc->cmdline)));

            rc->sub_rc = sub_rc;
            source = g_unix_fd_source_new (donna_task_get_wait_fd (task), G_IO_IN);
            g_source_set_callback (source, (GSourceFunc) subcommand_cb, rc, NULL);
            g_source_attach (source, NULL);
            g_source_unref (source);

            /* take a ref on task for our source; sub_rc->task will be set when
             * the pre_worker is called */
            donna_app_run_task (app, g_object_ref (task));
            return PARSE_PENDING;
        }
        /* we are parsing return value of subcommand */

        sub_rc = rc->sub_rc;
        task = sub_rc->task;
        v = donna_task_get_return_value (task);
        if (!v)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_OPTIONAL)
            {
                g_ptr_array_add (rc->args, NULL);
                end = sub_rc->start + 1;
                s = NULL; /* to goto next */
                goto clear_and_goto;
            }
            else
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: "
                        "Argument required, and command '%s' didn't return anything",
                        rc->command->name, rc->i + 1, sub_rc->command->name);
                g_object_unref (task);
                return PARSE_FAILED;
            }
        }

        s = NULL;
        if (sub_rc->command->return_type & DONNA_ARG_IS_ARRAY)
        {
            /* is it a matching array? */
            if ((rc->command->arg_type[rc->i] & sub_rc->command->return_type)
                    == sub_rc->command->return_type)
                g_ptr_array_add (rc->args, g_value_dup_boxed (v));
            /* since we're talking about a command's return_type, we expect
             * thigs to make sense, i.e. we don't check for unsupported array
             * types */
            else
            {
                GString *str;
                GPtrArray *arr = g_value_get_boxed (v);
                guint i;

                /* try to match things up */
                if (arr->len == 1)
                {
                    if ((sub_rc->command->return_type & DONNA_ARG_TYPE_NODE)
                            && (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_NODE))
                        g_ptr_array_add (rc->args, g_object_ref (arr->pdata[0]));
                    else if ((sub_rc->command->return_type & DONNA_ARG_TYPE_NODE)
                            && (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_ROW_ID))
                    {
                        DonnaRowId *rid = g_new (DonnaRowId, 1);
                        rid->ptr = g_object_ref (arr->pdata[0]);
                        rid->type = DONNA_ARG_TYPE_NODE;
                        g_ptr_array_add (rc->args, rid);
                    }
                    else if ((sub_rc->command->return_type & DONNA_ARG_TYPE_STRING)
                            && (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_STRING))
                        g_ptr_array_add (rc->args, g_strdup (arr->pdata[0]));
                    else
                    {
                        /* since we only have one element, we'll give the string
                         * (or full location) unquoted */
                        if (sub_rc->command->return_type & DONNA_ARG_TYPE_NODE)
                            s = donna_node_get_full_location (arr->pdata[0]);
                        else
                            s = g_strdup (arr->pdata[0]);
                    }
                }
                /* turn to string */
                else
                {
                    str = g_string_new (NULL);

                    /* FIXME this should simply create an intref for the array,
                     * and put that as string in s. Only if (when supported) the
                     * return value was to be dereferenced (e.g. @*command) than
                     * we convert to a "full string" that way */

                    for (i = 0; i < arr->len; ++i)
                    {
                        gchar *ss;

                        /* only 2 types of arrays are supported: NODE & STRING */
                        if (sub_rc->command->return_type & DONNA_ARG_TYPE_NODE)
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

                        if (sub_rc->command->return_type & DONNA_ARG_TYPE_NODE)
                            g_free (s);
                        g_string_append_c (str, ',');
                    }
                    /* remove last ',' */
                    g_string_truncate (str, str->len - 1);
                    s = g_string_free (str, FALSE);
                }
            }
        }
        else if (sub_rc->command->return_type & DONNA_ARG_TYPE_INT)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_INT)
                g_ptr_array_add (rc->args,
                        GINT_TO_POINTER (g_value_get_int (v)));
            else
                s = g_strdup_printf ("%d", g_value_get_int (v));
        }
        else if (sub_rc->command->return_type & DONNA_ARG_TYPE_STRING)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_STRING)
            {
                if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_ARRAY)
                {
                    GPtrArray *arr;

                    arr = g_ptr_array_new_full (1, g_free);
                    g_ptr_array_add (arr, g_value_dup_string (v));
                    g_ptr_array_add (rc->args, arr);
                }
                else
                    g_ptr_array_add (rc->args, g_value_dup_string (v));
            }
            else
                s = g_value_dup_string (v);
        }
        else if (sub_rc->command->return_type & DONNA_ARG_TYPE_TREE_VIEW)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_TREE_VIEW)
                g_ptr_array_add (rc->args, g_value_dup_object (v));
            else
                s = g_strdup (donna_tree_view_get_name (g_value_get_object (v)));
        }
        else if (sub_rc->command->return_type & DONNA_ARG_TYPE_TERMINAL)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_TERMINAL)
                g_ptr_array_add (rc->args, g_value_dup_object (v));
            else
                s = g_strdup (donna_terminal_get_name (g_value_get_object (v)));
        }
        else if (sub_rc->command->return_type & DONNA_ARG_TYPE_NODE)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_NODE)
            {
                if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_ARRAY)
                {
                    GPtrArray *arr;

                    arr = g_ptr_array_new_full (1, g_object_unref);
                    g_ptr_array_add (arr, g_value_dup_object (v));
                    g_ptr_array_add (rc->args, arr);
                }
                else
                    g_ptr_array_add (rc->args, g_value_dup_object (v));
            }
            else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_ROW_ID)
            {
                DonnaRowId *rid = g_new (DonnaRowId, 1);
                rid->ptr = g_value_dup_object (v);
                rid->type = DONNA_ARG_TYPE_NODE;
                g_ptr_array_add (rc->args, rid);
            }
            else
                s = donna_node_get_full_location (g_value_get_object (v));
        }
        else
        {
            g_warning ("Command '%s', argument %d: "
                    "Command '%s' had an unsupported type (%d) of return value",
                    rc->command->name, rc->i + 1,
                    sub_rc->command->name, sub_rc->command->return_type);
        }

        end = sub_rc->start + 1;
        c = *end;
clear_and_goto:
        g_object_unref (task);

        rc->sub_rc = NULL;
        sub_rc->is_subcommand = FALSE;
        sub_rc->cmdline = NULL;
        free_run_command (sub_rc);

        if (!s)
            goto next;
        else
            goto convert;
    }
    else
        for (end = rc->start; *end != ',' && *end != ')' && *end != '\0'; ++end)
            ;

    if (*end == '\0')
    {
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Command '%s', argument %d: Unexpected end-of-string",
                rc->command->name, rc->i + 1);
        return PARSE_FAILED;
    }

    if (*end != ')')
    {
        if (rc->i + 1 == rc->command->argc)
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s', argument %d: Closing parenthesis missing: %s",
                    rc->command->name, rc->i + 1, end);
            return PARSE_FAILED;
        }
        else if (*end != ',')
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s', argument %d: Unexpected character (expected ',' or ')'): %s",
                    rc->command->name, rc->i + 1, end);
            return PARSE_FAILED;
        }
    }

    if (end == rc->start)
    {
        if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_OPTIONAL)
        {
            g_ptr_array_add (rc->args, NULL);
            goto next;
        }
        else
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_MISSING_ARG,
                    "Command '%s', argument %d required",
                    rc->command->name, rc->i + 1);
            return PARSE_FAILED;
        }
    }

    s = rc->start;
    c = s[end - s];
    s[end - s] = '\0';

convert:
    /* not all types are supported, only those that make sense. That is,
     * arguments for a row are ROW_ID, to be as wide as possible. They cannot be
     * ROW or PATH as that would be too limiting.
     * (ROW is a type of ROW_ID, also makes sense in the other war around (i.e.
     * as type a flag or command's return value); PATH is only a type of ROW_ID
     * but never used "directly")
     */

    if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_TREE_VIEW)
    {
        gpointer ptr;

        if (streq (s, ":active"))
            g_object_get (app, "active-list", &ptr, NULL);
        else if (*s == '<')
        {
            gsize len = strlen (s) - 1;
            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid tree view name/reference: '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
            ptr = donna_app_get_int_ref (app, s, DONNA_ARG_TYPE_TREE_VIEW);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
        }
        else
        {
            ptr = donna_app_get_tree_view (app, s);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_NOT_FOUND,
                        "Command '%s', argument %d: TreeView '%s' not found",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
        }
        g_ptr_array_add (rc->args, ptr);
    }
    else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_TERMINAL)
    {
        gpointer ptr;

        if (*s == '<')
        {
            gsize len = strlen (s) - 1;
            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid tree view name/reference: '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
            ptr = donna_app_get_int_ref (app, s, DONNA_ARG_TYPE_TERMINAL);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
        }
        else
        {
            ptr = donna_app_get_terminal (app, s);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_NOT_FOUND,
                        "Command '%s', argument %d: Terminal '%s' not found",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
        }
        g_ptr_array_add (rc->args, ptr);
    }
    else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_ROW_ID)
    {
        DonnaRowId *rid = g_new (DonnaRowId, 1);
        gpointer ptr;

        if (*s == '[' && s[strlen (s) - 1] == ']')
        {
            DonnaRow *row = g_new (DonnaRow, 1);
            if (sscanf (s, "[%p;%p]", &row->node, &row->iter) != 2)
            {
                g_free (row);
                g_free (rid);
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid argument syntax ROW for ROW_ID",
                        rc->command->name, rc->i + 1);
                goto error;
            }
            rid->type = DONNA_ARG_TYPE_ROW;
            rid->ptr  = row;
        }
        else if (*s == ':' || *s == '%' || (*s >= '0' && *s <= '9'))
        {
            rid->type = DONNA_ARG_TYPE_PATH;
            rid->ptr = g_strdup (s);
        }
        else if (*s == '<' && s[strlen (s) - 1] == '>')
        {
            ptr = donna_app_get_int_ref (app, s, DONNA_ARG_TYPE_NODE);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
            rid->type = DONNA_ARG_TYPE_NODE;
            rid->ptr  = ptr;
        }
        else
        {
            ptr = donna_app_get_node (app, s, TRUE, &err);
            if (!ptr)
            {
                g_free (rid);
                g_propagate_prefixed_error (error, err, "Command '%s', argument %d: "
                        "Can't get node for '%s': ",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
            rid->ptr = ptr;
            rid->type = DONNA_ARG_TYPE_NODE;
        }
        g_ptr_array_add (rc->args, rid);
    }
    else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_NODE)
    {
        GPtrArray *arr = NULL;
        gpointer ptr;

        if (*s == '<' && s[strlen (s) - 1] == '>')
        {
            /* if an array, try an intref for an array of nodes first */
            if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_ARRAY)
            {
                ptr = donna_app_get_int_ref (app, s,
                        DONNA_ARG_TYPE_NODE | DONNA_ARG_IS_ARRAY);
                if (ptr)
                {
                    g_ptr_array_add (rc->args, ptr);
                    goto inner_next;
                }
            }

            ptr = donna_app_get_int_ref (app, s, DONNA_ARG_TYPE_NODE);
            if (!ptr)
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                        "Command '%s', argument %d: Invalid internal reference '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
            else if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_ARRAY)
            {
                /* we want an array of nodes, not a node */

                arr = g_ptr_array_new_full (1, g_object_unref);
                g_ptr_array_add (arr, ptr);
                ptr = arr;
            }

            g_ptr_array_add (rc->args, ptr);
            goto inner_next;
        }

        if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_ARRAY)
        {
            gchar *start = s;

            arr = g_ptr_array_new_with_free_func (g_object_unref);
            for (;;)
            {
                gchar *_end;

                if (!unquote_string (&start, &_end, &err))
                {
                    gchar *e;

                    if (err)
                    {
                        g_propagate_prefixed_error (error, err,
                                "Command '%s', argument %d: ",
                                rc->command->name, rc->i + 1);
                        g_ptr_array_unref (arr);
                        goto error;
                    }

                    /* not quoted */

                    skip_blank (start);
                    _end = strchr (start, ',');
                    if (_end)
                    {
                        *_end = '\0';
                        e = _end - 1;
                        ++_end;
                    }
                    else
                        e = start + strlen (start) - 1;
                    /* go back, skip_blank() in reverse. Because we aim to trim
                     * this one (hence the skip_blank(start) above as well) */
                    for ( ; isblank (*e); --e) ;
                    *++e = '\0';
                }

                ptr = donna_app_get_node (app, start, TRUE, &err);
                if (!ptr)
                {
                    g_propagate_prefixed_error (error, err,
                            "Command '%s', argument %d: Can't get node for '%s': ",
                            rc->command->name, rc->i + 1, start);
                    g_ptr_array_unref (arr);
                    goto error;
                }
                g_ptr_array_add (arr, ptr);

                /* no ',' found in unquoted string */
                if (!_end)
                    break;

                start = _end;
                skip_blank (start);
                if (*start == '\0')
                    break;
                else if (*start != ',')
                {
                    g_set_error (error, DONNA_COMMAND_ERROR,
                            DONNA_COMMAND_ERROR_SYNTAX,
                            "Command '%s', argument %d: "
                            "Invalid list, expected ',' or EOL",
                            rc->command->name, rc->i + 1);
                    g_ptr_array_unref (arr);
                    goto error;
                }
                else
                    ++start;
            }
            g_ptr_array_add (rc->args, arr);
        }
        else
        {
            /* we shouldn't use unquote_string() here because this arg has
             * already been unquoted. "Double-quoting" can only happen in case
             * of arrays, where the list is quoted, and so are each element */

            ptr = donna_app_get_node (app, s, TRUE, &err);
            if (!ptr)
            {
                g_propagate_prefixed_error (error, err,
                        "Command '%s', argument %d: Can't get node for '%s': ",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
            g_ptr_array_add (rc->args, ptr);
        }
    }
    else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_STRING)
    {
        if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_ARRAY)
        {
            gpointer ptr;

            if (*s == '<')
            {
                gsize len = strlen (s) - 1;
                if (s[len] != '>')
                {
                    g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                            "Command '%s', argument %d: Invalid internal reference: '%s'",
                            rc->command->name, rc->i + 1, s);
                    goto error;
                }
                ptr = donna_app_get_int_ref (app, s,
                        DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_ARRAY);
                if (!ptr)
                {
                    g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                            "Command '%s', argument %d: Invalid internal reference '%s'",
                            rc->command->name, rc->i + 1, s);
                    goto error;
                }
            }
            else
            {
                GPtrArray *arr;
                gchar *start;
                gchar *_end;

                start = s;
                arr = g_ptr_array_new_with_free_func (g_free);
                for (;;)
                {
                    if (unquote_string (&start, &_end, &err))
                        g_ptr_array_add (arr, g_strdup (start));
                    else if (err)
                    {
                        g_propagate_prefixed_error (error, err,
                                "Command '%s', argument %d: ",
                                rc->command->name, rc->i + 1);
                        g_ptr_array_unref (arr);
                        goto error;
                    }
                    else
                    {
                        gchar *e;

                        /* not quoted */

                        skip_blank (start);
                        _end = strchr (start, ',');
                        if (_end)
                        {
                            *_end = '\0';
                            e = _end - 1;
                            ++_end;
                        }
                        else
                            e = start + strlen (start) - 1;
                        /* go back, skip_blank() in reverse. Because we aim to trim
                         * this one (hence the skip_blank(start) above as well) */
                        for ( ; isblank (*e); --e) ;
                        *++e = '\0';

                        g_ptr_array_add (arr, g_strdup (start));
                        if (!_end)
                            break;
                    }

                    start = _end;
                    skip_blank (start);
                    if (*start == '\0')
                        break;
                    else if (*start != ',')
                    {
                        g_set_error (error, DONNA_COMMAND_ERROR,
                                DONNA_COMMAND_ERROR_SYNTAX,
                                "Command '%s', argument %d: "
                                "Invalid list, expected ',' or EOL",
                                rc->command->name, rc->i + 1);
                        g_ptr_array_unref (arr);
                        goto error;
                    }
                    else
                        ++start;
                }
                ptr = arr;
            }
            g_ptr_array_add (rc->args, ptr);
        }
        else
            g_ptr_array_add (rc->args, g_strdup (s));
    }
    else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_INT)
        g_ptr_array_add (rc->args, GINT_TO_POINTER (g_ascii_strtoll (s, NULL, 10)));
    else
    {
        g_warning ("convert_arg(): Invalid arg_type %d for argument %d of '%s'",
                rc->command->arg_type[rc->i], rc->i + 1, rc->command->name);
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                "Command '%s', argument %d: Invalid argument type",
                rc->command->name, rc->i + 1);
        goto error;
    }
inner_next:
    s[end - s] = c;

next:
    if (*end == ')')
        rc->start = end;
    else
    {
        rc->start = end + 1;
        skip_blank (rc->start);
    }
    return PARSE_DONE;

error:
    s[end - s] = c;
    return PARSE_FAILED;
}

static void
parse_command (struct run_command *rc)
{
    GError *err = NULL;
    DonnaTaskState prestate = DONNA_TASK_DONE;

    for ( ; rc->i < rc->command->argc; ++rc->i)
    {
        enum parse parsed;

        parsed = parse_arg (rc, &err);
        if (parsed == PARSE_FAILED)
        {
            donna_task_take_error (rc->task, err);
            prestate = DONNA_TASK_FAILED;
            goto done;
        }
        else if (parsed == PARSE_PENDING)
            /* a source on a task for a subcommand was set up */
            return;
        /* PARSE_DONE */

        if (*rc->start == ')' && rc->i + 1 < rc->command->argc)
        {
            guint j;

            /* this was the last argument specified, but command has more */

            for (j = rc->i + 1; j < rc->command->argc; ++j)
                if (!(rc->command->arg_type[j] & DONNA_ARG_IS_OPTIONAL))
                    break;

            if (j >= rc->command->argc)
            {
                /* allow missing arg(s) if they're optional */
                for (rc->i = rc->i + 1; rc->i < rc->command->argc; ++rc->i)
                    g_ptr_array_add (rc->args, NULL);
            }
            else
            {
                donna_task_set_error (rc->task, DONNA_COMMAND_ERROR,
                        DONNA_COMMAND_ERROR_MISSING_ARG,
                        "Command '%s', argument %d required",
                        rc->command->name, j + 1);
                prestate = DONNA_TASK_FAILED;
                goto done;
            }
        }
    }

    if (*rc->start != ')')
    {
        donna_task_set_error (rc->task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_SYNTAX,
                "Command '%s': Too many arguments: %s",
                rc->command->name, rc->start);
        prestate = DONNA_TASK_FAILED;
        goto done;
    }

done:
    donna_task_set_preran (rc->task, prestate, rc->run_task, rc->run_task_data);
}

static void
pre_run_command (DonnaTask          *task,
                 task_run_fn         run_task,
                 gpointer            run_task_data,
                 struct run_command *rc)
{
    GError *err = NULL;

    rc->task = task;
    rc->run_task = run_task;
    rc->run_task_data = run_task_data;

    rc->command = _donna_command_init_parse (rc->pc, rc->cmdline, &rc->start, &err);
    if (G_UNLIKELY (!rc->command))
    {
        donna_task_take_error (task, err);
        free_run_command (rc);
        donna_task_set_preran (task, DONNA_TASK_FAILED, run_task, run_task_data);
        return;
    }

    rc->args = g_ptr_array_sized_new (rc->command->argc);
    parse_command (rc);
}

static DonnaTaskState
run_command (DonnaTask *task, struct run_command *rc)
{
    DonnaTaskState state;

    state = rc->command->func (task, rc->pc->priv->app,
            rc->args->pdata, rc->command->data);

    if (state == DONNA_TASK_DONE
            && (rc->which == WHICH_GET_CHILDREN || rc->which == WHICH_HAS_CHILDREN))
    {
        GPtrArray *arr;
        guint i;

        arr = (GPtrArray *) g_value_get_boxed (donna_task_get_return_value (task));

        /* here we could emit node-children, but:
         * 1. there's no obligation to do so,
         * 2. since commands could return an array containing some NULLs (e.g.
         * separators for menus, via nodes_add()) this might not be a good idea,
         * 3. such nodes will usually not be used in a way where such a signal
         * is useful anyways, i.e. either it's only for a command needing a
         * container or via context menus, or should it be as a treeview's
         * location, that treeview will be the one asking for children anyways
         * (so getting our return value)
         *
         * XXX: should everything doing a get_children() or connecting to
         * node-children be made NULL-safe (in case of separators) ?
         */
#if 0
        /* emit node-children */
        donna_provider_node_children ((DonnaProvider *) rc->pc,
                rc->node, DONNA_NODE_ITEM | DONNA_NODE_CONTAINER, arr);
#endif

        /* filter the array to only what was asked */
        for (i = 0; i < arr->len; )
        {
            DonnaNode *node = arr->pdata[i];

            /* allow node to be NULL in case this is for a menu with separators
             * (e.g. via nodes_add() */
            if (node && !(donna_node_get_node_type (node) & rc->node_types))
                g_ptr_array_remove_index_fast (arr, i);
            else
                ++i;
        }

        if (rc->which == WHICH_HAS_CHILDREN)
        {
            GValue *value;
            gboolean has_children;

            /* turn the array of nodes/children in to a TRUE/FALSE */

            has_children = arr->len > 0;
            value = donna_task_grab_return_value (task);
            g_value_unset (value);
            g_value_init (value, G_TYPE_BOOLEAN);
            g_value_set_boolean (value, has_children);
            donna_task_release_return_value (task);
        }
    }

    free_run_command (rc);
    return state;
}

static DonnaTask *
provider_command_trigger_node_task (DonnaProvider      *provider,
                                    DonnaNode          *node,
                                    GError            **error)
{
    struct run_command *rc;
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;

    rc = g_slice_new0 (struct run_command);
    rc->which = WHICH_TRIGGER;
    rc->pc = (DonnaProviderCommand *) provider;
    rc->cmdline = donna_node_get_location (node);

    task = donna_task_new ((task_fn) run_command, rc, free_run_command);
    donna_task_set_pre_worker (task, (task_pre_fn) pre_run_command);
    donna_node_get (node, FALSE, "trigger-visibility", &has, &v, NULL);
    donna_task_set_visibility (task, g_value_get_uint (&v));
    g_value_unset (&v);

    DONNA_DEBUG (TASK, NULL,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "trigger_node() for node '%s'", fl));
            g_free (fl));

    return task;
}

static DonnaTask *
provider_command_has_node_children_task (DonnaProvider      *provider,
                                         DonnaNode          *node,
                                         DonnaNodeType       node_types,
                                         GError            **error)
{
    struct run_command *rc;
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;

    rc = g_slice_new0 (struct run_command);
    rc->which = WHICH_HAS_CHILDREN;
    rc->pc = (DonnaProviderCommand *) provider;
    rc->cmdline = donna_node_get_location (node);
    rc->node = g_object_ref (node);
    rc->node_types = node_types;

    task = donna_task_new ((task_fn) run_command, rc, free_run_command);
    donna_task_set_pre_worker (task, (task_pre_fn) pre_run_command);
    donna_node_get (node, FALSE, "trigger-visibility", &has, &v, NULL);
    donna_task_set_visibility (task, g_value_get_uint (&v));
    g_value_unset (&v);

    DONNA_DEBUG (TASK, NULL,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "has_node_children() for node '%s'", fl));
            g_free (fl));

    return task;
}

static DonnaTask *
provider_command_get_node_children_task (DonnaProvider      *provider,
                                         DonnaNode          *node,
                                         DonnaNodeType       node_types,
                                         GError            **error)
{
    struct run_command *rc;
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;

    rc = g_slice_new0 (struct run_command);
    rc->which = WHICH_GET_CHILDREN;
    rc->pc = (DonnaProviderCommand *) provider;
    rc->cmdline = donna_node_get_location (node);
    rc->node = g_object_ref (node);
    rc->node_types = node_types;

    task = donna_task_new ((task_fn) run_command, rc, free_run_command);
    donna_task_set_pre_worker (task, (task_pre_fn) pre_run_command);
    donna_node_get (node, FALSE, "trigger-visibility", &has, &v, NULL);
    donna_task_set_visibility (task, g_value_get_uint (&v));
    g_value_unset (&v);

    DONNA_DEBUG (TASK, NULL,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "get_node_children() for node '%s'", fl));
            g_free (fl));

    return task;
}


gboolean
donna_provider_command_add_command (DonnaProviderCommand   *pc,
                                    const gchar            *name,
                                    guint                   argc,
                                    DonnaArgType           *_arg_type,
                                    DonnaArgType            return_type,
                                    DonnaTaskVisibility     visibility,
                                    command_fn              func,
                                    gpointer                data,
                                    GDestroyNotify          destroy,
                                    GError                **error)
{
    DonnaProviderCommandPrivate *priv;
    struct command *command;
    DonnaArgType *arg_type;

    g_return_val_if_fail (DONNA_IS_PROVIDER_COMMAND (pc), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (func != NULL, FALSE);
    priv = pc->priv;

    g_mutex_lock (&priv->mutex);
    if (G_UNLIKELY (g_hash_table_lookup (priv->commands, name)))
    {
        g_mutex_unlock (&priv->mutex);
        g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ALREADY_EXISTS,
                "Cannot add command '%s', already exists", name);
        return FALSE;
    }

    if (G_LIKELY (argc > 0))
    {
        arg_type = g_new (DonnaArgType, argc);
        memcpy (arg_type, _arg_type, sizeof (DonnaArgType) * argc);
    }
    else
        arg_type = NULL;

    command = g_slice_new (struct command);
    command->name           = g_strdup (name);
    command->argc           = argc;
    command->arg_type       = arg_type;
    command->return_type    = return_type;
    command->visibility     = visibility;
    command->func           = func;
    command->data           = data;
    command->destroy        = destroy;

    g_hash_table_insert (priv->commands, command->name, command);
    g_mutex_unlock (&priv->mutex);
    return TRUE;
}
