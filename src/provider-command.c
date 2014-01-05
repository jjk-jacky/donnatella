
#include "config.h"

#include <gtk/gtk.h>
#include "provider-command.h"
#include "command.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "macros.h"
#include "debug.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
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
/* DonnaProviderBase */
static DonnaTaskState   provider_command_new_node       (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static DonnaTaskState   provider_command_has_children   (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_command_get_children   (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);

/* internal from command.c */
void _donna_add_commands (GHashTable *commands);

static void
provider_command_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain        = provider_command_get_domain;
    interface->get_flags         = provider_command_get_flags;
    interface->trigger_node_task = provider_command_trigger_node_task;
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

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.has_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;
    pb_class->task_visibility.get_children  = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->new_node      = provider_command_new_node;
    pb_class->has_children  = provider_command_has_children;
    pb_class->get_children  = provider_command_get_children;

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

struct command *init_parse (DonnaProviderCommand    *pc,
                            gchar                   *cmdline,
                            gchar                  **first_arg,
                            GError                 **error);

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

    cmd = init_parse ((DonnaProviderCommand *) _provider, (gchar *) location,
            NULL, &err);
    if (!cmd)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node = donna_node_new ((DonnaProvider *) _provider, location,
            DONNA_NODE_ITEM, NULL, (refresher_fn) gtk_true, NULL, cmd->name,
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
    if (!donna_node_add_property (node, "trigger-visibility", G_TYPE_UINT, &v,
            (refresher_fn) gtk_true, NULL, &err))
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

static DonnaTaskState
provider_command_has_children (DonnaProviderBase  *_provider,
                               DonnaTask          *task,
                               DonnaNode          *node,
                               DonnaNodeType       node_types)
{
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_INVALID_CALL,
            "Provider 'command': has_children() not supported");
    return DONNA_TASK_FAILED;
}

static DonnaTaskState
provider_command_get_children (DonnaProviderBase  *_provider,
                               DonnaTask          *task,
                               DonnaNode          *node,
                               DonnaNodeType       node_types)
{
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_INVALID_CALL,
            "Provider 'command': get_children() not supported");
    return DONNA_TASK_FAILED;
}

struct run_command
{
    gboolean                 is_stack;
    DonnaProviderCommand    *pc;
    gchar                   *cmdline;
    struct command          *command;
    gchar                   *start;
    guint                    i;
    GPtrArray               *args;
    DonnaTask               *task;
};

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
        else if (command->arg_type[i] & (DONNA_ARG_TYPE_TREE_VIEW | DONNA_ARG_TYPE_NODE))
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
free_run_command (struct run_command *rc)
{
    g_free (rc->cmdline);
    free_command_args (rc->command, rc->args);
    if (!rc->is_stack)
        g_slice_free (struct run_command, rc);
}

struct command *
init_parse (DonnaProviderCommand    *pc,
            gchar                   *cmdline,
            gchar                  **first_arg,
            GError                 **error)
{
    DonnaProviderCommandPrivate *priv = pc->priv;
    struct command *command;
    gchar  c;
    gchar *s;

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
 * or before in case some unescaping was done. *end will always point to the
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

static DonnaTaskState run_command (DonnaTask *task, struct run_command *rc);

/* parse the next argument for a command. data must have been properly set &
 * initialized via init_parse() and therefore data->start points
 * to the beginning of the argument data->i
 * Will handle figuring out where the arg ends (can be quoted, can also be
 * another command to run & get its return value as arg, via @syntax), and
 * moving data->start & whatnot where needs be */
static DonnaTaskState
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
        /* move to next relevant char */
        ++end;
        skip_blank (end);
    }
    else if (err)
    {
        g_propagate_prefixed_error (error, err, "Command '%s', argument %d: ",
                rc->command->name, rc->i + 1);
        return DONNA_TASK_FAILED;
    }
    else if (*rc->start == '@')
    {
        struct run_command _rc;
        gboolean dereference;
        DonnaTask *task;
        DonnaTaskState state;
        const GValue *v;
        gchar *start;

        dereference = rc->start[1] == '*';
        start = rc->start + ((dereference) ? 2 : 1);

        /* do the init_parse to known which command this is, so we can check
         * visibility/ensure we can run it */
        _rc.command = init_parse (rc->pc, start, NULL, &err);
        if (!_rc.command)
        {
            g_propagate_prefixed_error (error, err, "Command '%s', argument %d: ",
                    rc->command->name, rc->i + 1);
            return DONNA_TASK_FAILED;
        }

        memset (&_rc, 0, sizeof (struct run_command));
        _rc.is_stack = TRUE; /* so free_run_command() doesn't try to free it */
        _rc.pc = rc->pc;
        _rc.cmdline = g_strdup (start);

        /* create a "fake" task for error/return value */
        task = g_object_ref_sink (donna_task_new ((task_fn) gtk_true, NULL, NULL));
        state = run_command (task, &_rc);
        if (state == DONNA_TASK_CANCELLED)
        {
            g_object_unref (task);
            return DONNA_TASK_CANCELLED;
        }
        else if (state != DONNA_TASK_DONE)
        {
            if (error)
            {
                const GError *e;

                e = donna_task_get_error (task);
                if (e)
                {
                    *error = g_error_copy (e);
                    g_prefix_error (error, "Command '%s', argument %d: "
                            "Command %s%s%s failed: ",
                            rc->command->name, rc->i + 1,
                            (_rc.command) ? "'" : "",
                            (_rc.command) ? _rc.command->name : "",
                            (_rc.command) ? "' " : "");
                }
                else
                    g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_OTHER,
                            "Command '%s', argument %d: "
                            "Command %s%s%sfailed (w/out error message)",
                            rc->command->name, rc->i + 1,
                            (_rc.command) ? "'" : "",
                            (_rc.command) ? _rc.command->name : "",
                            (_rc.command) ? "' " : "");
            }

            g_object_unref (task);
            return DONNA_TASK_FAILED;
        }

        v = donna_task_get_return_value (task);
        if (!v)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_IS_OPTIONAL)
            {
                g_ptr_array_add (rc->args, NULL);
                end = start + (_rc.start - _rc.cmdline + 1);
                g_object_unref (task);
                goto next;
            }
            else
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: "
                        "Argument required, and command '%s' didn't return anything",
                        rc->command->name, rc->i + 1, _rc.command->name);
                g_object_unref (task);
                return DONNA_TASK_FAILED;
            }
        }

        s = NULL;
        if (_rc.command->return_type & DONNA_ARG_IS_ARRAY)
        {
            /* is it a matching array? */
            if ((rc->command->arg_type[rc->i] & _rc.command->return_type)
                    == _rc.command->return_type)
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
                    if ((_rc.command->return_type & DONNA_ARG_TYPE_NODE)
                            && (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_NODE))
                        g_ptr_array_add (rc->args, g_object_ref (arr->pdata[0]));
                    else if ((_rc.command->return_type & DONNA_ARG_TYPE_NODE)
                            && (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_ROW_ID))
                    {
                        DonnaRowId *rid = g_new (DonnaRowId, 1);
                        rid->ptr = g_object_ref (arr->pdata[0]);
                        rid->type = DONNA_ARG_TYPE_NODE;
                        g_ptr_array_add (rc->args, rid);
                    }
                    else if ((_rc.command->return_type & DONNA_ARG_TYPE_STRING)
                            && (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_STRING))
                        g_ptr_array_add (rc->args, g_strdup (arr->pdata[0]));
                    else
                    {
                        /* since we only have one element, we'll give the string
                         * (or full location) unquoted */
                        if (_rc.command->return_type & DONNA_ARG_TYPE_NODE)
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
                        if (_rc.command->return_type & DONNA_ARG_TYPE_NODE)
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

                        if (_rc.command->return_type & DONNA_ARG_TYPE_NODE)
                            g_free (s);
                        g_string_append_c (str, ',');
                    }
                    /* remove last ',' */
                    g_string_truncate (str, str->len - 1);
                    s = g_string_free (str, FALSE);
                }
            }
        }
        else if (_rc.command->return_type & DONNA_ARG_TYPE_INT)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_INT)
                g_ptr_array_add (rc->args,
                        GINT_TO_POINTER (g_value_get_int (v)));
            else
                s = g_strdup_printf ("%d", g_value_get_int (v));
        }
        else if (_rc.command->return_type & DONNA_ARG_TYPE_STRING)
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
        else if (_rc.command->return_type & DONNA_ARG_TYPE_TREE_VIEW)
        {
            if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_TREE_VIEW)
                g_ptr_array_add (rc->args, g_value_dup_object (v));
            else
                s = g_strdup (donna_tree_view_get_name (g_value_get_object (v)));
        }
        else if (_rc.command->return_type & DONNA_ARG_TYPE_NODE)
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
                    _rc.command->name, _rc.command->return_type);
        }

        end = start + (_rc.start - _rc.cmdline + 1);
        c = start[end - start];
        g_object_unref (task);
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
        return DONNA_TASK_FAILED;
    }

    if (*end != ')')
    {
        if (rc->i + 1 == rc->command->argc)
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s', argument %d: Closing parenthesis missing: %s",
                    rc->command->name, rc->i + 1, end);
            return DONNA_TASK_FAILED;
        }
        else if (*end != ',')
        {
            g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                    "Command '%s', argument %d: Unexpected character (expected ',' or ')'): %s",
                    rc->command->name, rc->i + 1, end);
            return DONNA_TASK_FAILED;
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
            return DONNA_TASK_FAILED;
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
    else if (rc->command->arg_type[rc->i] & DONNA_ARG_TYPE_ROW_ID)
    {
        DonnaRowId *rid = g_new (DonnaRowId, 1);
        gpointer ptr;

        if (*s == '[')
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
        else if (*s == '<')
        {
            gsize len = strlen (s) - 1;

            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid node reference: '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }
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
            ptr = donna_app_get_node (app, s, &err);
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

        if (*s == '<')
        {
            gsize len = strlen (s) - 1;
            if (s[len] != '>')
            {
                g_set_error (error, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                        "Command '%s', argument %d: Invalid node full location/reference: '%s'",
                        rc->command->name, rc->i + 1, s);
                goto error;
            }

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
                    }
                    else
                        e = start + strlen (start) - 1;
                    /* go back, skip_blank() in reverse. Because we aim to trim
                     * this one (hence the skip_blank(start) above as well) */
                    for ( ; isblank (*e); --e) ;
                    *++e = '\0';
                }

                ptr = donna_app_get_node (app, start, &err);
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

                start = _end + 1;
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

            ptr = donna_app_get_node (app, s, &err);
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

                    start = _end + 1;
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
    return DONNA_TASK_DONE;

error:
    s[end - s] = c;
    return DONNA_TASK_FAILED;
}

static DonnaTaskState
task_run_command (DonnaTask *cmd_task, struct run_command *rc)
{
    return rc->command->func (rc->task, rc->pc->priv->app,
            rc->args->pdata, rc->command->data);
}

static DonnaTaskState
run_command (DonnaTask *task, struct run_command *rc)
{
    GError *err = NULL;
    DonnaApp *app = rc->pc->priv->app;
    gboolean need_task = FALSE;
    DonnaTaskState ret;

    rc->command = init_parse (rc->pc, rc->cmdline, &rc->start, NULL);
    if (G_UNLIKELY (!rc->command))
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR,
                DONNA_COMMAND_ERROR_NOT_FOUND,
                "run_command failed to identify command for: %s",
                rc->cmdline);
        free_run_command (rc);
        return DONNA_TASK_FAILED;
    }

    rc->args = g_ptr_array_sized_new (rc->command->argc);
    for (rc->i = 0; rc->i < rc->command->argc; ++rc->i)
    {
        DonnaTaskState parsed;

        parsed = parse_arg (rc, &err);
        if (parsed != DONNA_TASK_DONE)
        {
            if (parsed == DONNA_TASK_FAILED)
                donna_task_take_error (task, err);
            free_run_command (rc);
            return parsed;
        }

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
                g_clear_error (&err);
            }
            else
            {
                donna_task_set_error (task, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_MISSING_ARG,
                        "Command '%s', argument %d required",
                        rc->command->name, j + 1);
                free_run_command (rc);
                return DONNA_TASK_FAILED;
            }
        }
    }

    if (*rc->start != ')')
    {
        donna_task_set_error (task, DONNA_COMMAND_ERROR, DONNA_COMMAND_ERROR_SYNTAX,
                "Command '%s': Too many arguments: %s",
                rc->command->name, rc->start);
        free_run_command (rc);
        return DONNA_TASK_FAILED;
    }

    /* ready to run the command's function. If the command has a visibility GUI
     * and the current task isn't, we need a new task. Else, we can call it
     * directly (since we're either INTERNAL or FAST, and so is the command) */
    if (rc->command->visibility == DONNA_TASK_VISIBILITY_INTERNAL_GUI)
    {
        DonnaTaskVisibility visibility;

        g_object_get (task, "visibility", &visibility, NULL);
        need_task = visibility != DONNA_TASK_VISIBILITY_INTERNAL_GUI;
    }

    if (need_task)
    {
        DonnaTask *cmd_task;

        /* this will allow to call to the command's function with this "main"
         * task, so that's where error/return value will be set. Since this new
         * task (cmd_task) is only there to "get to thread UI" */
        rc->task = task;

        cmd_task = donna_task_new ((task_fn) task_run_command, rc, NULL);
        DONNA_DEBUG (TASK, NULL,
                donna_task_take_desc (cmd_task, g_strdup_printf (
                        "run command: %s", rc->cmdline)));
        donna_task_set_visibility (cmd_task, rc->command->visibility);
        if (!donna_app_run_task_and_wait (app, g_object_ref (cmd_task), task, &err))
        {
            g_prefix_error (&err, "Failed to run command's task: ");
            donna_task_take_error (task, err);
            g_object_unref (cmd_task);
            return DONNA_TASK_FAILED;
        }
        ret = donna_task_get_state (cmd_task);
        g_object_unref (cmd_task);
    }
    else
        ret = rc->command->func (task, app, rc->args->pdata, rc->command->data);

    free_run_command (rc);
    return ret;
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
    rc->pc = (DonnaProviderCommand *) provider;
    rc->cmdline = donna_node_get_location (node);

    task = donna_task_new ((task_fn) run_command, rc,
            (GDestroyNotify) free_run_command);
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
