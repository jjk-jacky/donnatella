
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
};

static GParamSpec * provider_command_props[NB_PROPS] = { NULL, };

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
static DonnaTaskState   provider_command_remove_node    (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node);

static void
provider_command_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain        = provider_command_get_domain;
    interface->get_flags         = provider_command_get_flags;
    interface->trigger_node_task = provider_command_trigger_node_task;
}

static void
donna_provider_command_class_init (DonnaProviderCommandClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node      = provider_command_new_node;
    pb_class->has_children  = provider_command_has_children;
    pb_class->get_children  = provider_command_get_children;
    pb_class->remove_node   = provider_command_remove_node;

    o_class = (GObjectClass *) klass;
    o_class->get_property   = provider_command_get_property;
    o_class->set_property   = provider_command_set_property;
    o_class->finalize       = provider_command_finalize;

    provider_command_props[PROP_APP] = g_param_spec_object ("app", "app",
            "App object", DONNA_TYPE_APP,
            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties (o_class, NB_PROPS, provider_command_props);

    g_type_class_add_private (klass, sizeof (DonnaProviderCommandPrivate));
}

static void
donna_provider_command_init (DonnaProviderCommand *provider)
{
    DonnaProviderCommandPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_COMMAND,
            DonnaProviderCommandPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderCommand, donna_provider_command,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_command_provider_init)
        )

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
        ((DonnaProviderCommand *) object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_command_finalize (GObject *object)
{
    DonnaProviderCommandPrivate *priv;

    priv = DONNA_PROVIDER_COMMAND (object)->priv;

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

static gboolean
refresher (DonnaTask    *task,
           DonnaNode    *node,
           const gchar  *name)
{
    return TRUE;
}

static DonnaTaskState
provider_command_new_node (DonnaProviderBase  *_provider,
                           DonnaTask          *task,
                           const gchar        *location)
{
    GError *err = NULL;
    DonnaProviderBaseClass *klass;
    DonnaCommand *cmd;
    DonnaNode *node;
    GValue *value;
    GtkWidget *w;
    GdkPixbuf *pixbuf;
    GValue v = G_VALUE_INIT;
    gchar *s;
    gint i;

    cmd = _donna_command_init_parse ((gchar *) location, &s, &err);
    if (!cmd)
    {
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node = donna_node_new ((DonnaProvider *) _provider, location,
            DONNA_NODE_ITEM, NULL, refresher, NULL, cmd->name,
            DONNA_NODE_ICON_EXISTS);
    if (!node)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'command': Unable to create a new node");
        return DONNA_TASK_FAILED;
    }
    g_value_init (&v, G_TYPE_OBJECT);
    w = g_object_ref_sink (gtk_label_new (NULL));
    pixbuf = gtk_widget_render_icon_pixbuf (w, GTK_STOCK_EXECUTE,
            GTK_ICON_SIZE_MENU);
    g_value_take_object (&v, pixbuf);
    donna_node_set_property_value (node, "icon", &v);
    g_value_unset (&v);
    g_object_unref (w);

    /* "preload" the type of visibility neede for the task trigger_node */
    g_value_init (&v, G_TYPE_INT);
    g_value_set_int (&v, cmd->visibility);
    /* commands cannot be PUBLIC (though they could start another task PUBLIC),
     * only INTERNAL, FAST or GUI. But, the task trigger_node might need an
     * upgrade to INTERNAL if at least one arg is NODE or ROW_ID, as that might
     * trigger a get_node task. */
    if (cmd->visibility != DONNA_TASK_VISIBILITY_INTERNAL)
        for (i = 0; i < cmd->argc; ++i)
        {
            if (cmd->arg_type[i] == DONNA_ARG_TYPE_NODE
                    || cmd->arg_type[i] == DONNA_ARG_TYPE_ROW_ID)
            {
                g_value_set_int (&v, DONNA_TASK_VISIBILITY_INTERNAL);
                break;
            }
        }
    if (!donna_node_add_property (node, "trigger-visibility", G_TYPE_INT, &v,
            refresher, NULL, &err))
    {
        g_prefix_error (&err, "Provider 'command': Cannot create new node, "
                "failed to add property 'trigger-visibility': ");
        donna_task_take_error (task, err);
        g_value_unset (&v);
        g_object_unref (node);
        return DONNA_TASK_FAILED;
    }
    g_value_unset (&v);

    klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
    klass->lock_nodes (_provider);
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

static DonnaTaskState
provider_command_remove_node (DonnaProviderBase  *_provider,
                              DonnaTask          *task,
                              DonnaNode          *node)
{
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_INVALID_CALL,
            "Provider 'command': remove_node() not supported");
    return DONNA_TASK_FAILED;
}

static DonnaTask *
provider_command_trigger_node_task (DonnaProvider      *provider,
                                    DonnaNode          *node,
                                    GError            **error)
{
    struct _donna_command_run *cr;
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;

    cr = g_new (struct _donna_command_run, 1);
    cr->app = ((DonnaProviderCommand *) provider)->priv->app;
    cr->cmdline = donna_node_get_location (node);

    task = donna_task_new ((task_fn) _donna_command_run, cr,
            (GDestroyNotify) _donna_command_free_cr);
    donna_node_get (node, FALSE, "trigger-visibility", &has, &v, NULL);
    donna_task_set_visibility (task, g_value_get_int (&v));
    g_value_unset (&v);

    DONNA_DEBUG (TASK,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "trigger_node() for node '%s'", fl));
            g_free (fl));

    return task;
}
