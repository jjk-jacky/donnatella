
#include <gtk/gtk.h>
#include "provider-mark.h"
#include "provider-command.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "command.h"
#include "macros.h"
#include "debug.h"


struct mark
{
    gchar *location;
    gchar *name;
    DonnaMarkType type;
    gchar *value;
};

struct _DonnaProviderMarkPrivate
{
    GMutex mutex;
    GHashTable *marks;
};


/* GObject */
static void             provider_mark_contructed        (GObject            *object);
static void             provider_mark_finalize          (GObject            *object);
/* DonnaProvider */
static const gchar *    provider_mark_get_domain        (DonnaProvider      *provider);
static DonnaProviderFlags provider_mark_get_flags       (DonnaProvider      *provider);
/* DonnaProviderBase */
static DonnaTaskState   provider_mark_new_node          (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static DonnaTaskState   provider_mark_has_children      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_mark_get_children      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node,
                                                         DonnaNodeType       node_types);
static DonnaTaskState   provider_mark_trigger_node      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node);
static DonnaTaskState   provider_mark_new_child         (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *parent,
                                                         DonnaNodeType       type,
                                                         const gchar        *name);

static void
provider_mark_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain   = provider_mark_get_domain;
    interface->get_flags    = provider_mark_get_flags;
}

static void
donna_provider_mark_class_init (DonnaProviderMarkClass *klass)
{
    DonnaProviderBaseClass *pb_class;
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->constructed    = provider_mark_contructed;
    o_class->finalize       = provider_mark_finalize;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node      = provider_mark_new_node;
    pb_class->has_children  = provider_mark_has_children;
    pb_class->get_children  = provider_mark_get_children;
    pb_class->trigger_node  = provider_mark_trigger_node;
    pb_class->new_child     = provider_mark_new_child;

    g_type_class_add_private (klass, sizeof (DonnaProviderMarkPrivate));
}

static void
free_mark (struct mark *mark)
{
    g_free (mark->location);
    g_free (mark->name);
    g_free (mark->value);
    g_free (mark);
}

static void
donna_provider_mark_init (DonnaProviderMark *provider)
{
    DonnaProviderMarkPrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_MARK,
            DonnaProviderMarkPrivate);
    g_mutex_init (&priv->mutex);
    priv->marks = g_hash_table_new_full (g_str_hash, g_str_equal,
            NULL, (GDestroyNotify) free_mark);
}

static void
provider_mark_finalize (GObject *object)
{
    DonnaProviderMarkPrivate *priv = ((DonnaProviderMark *) object)->priv;

    g_mutex_clear (&priv->mutex);
    g_hash_table_unref (priv->marks);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderMark, donna_provider_mark,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_mark_provider_init)
        )


/* internals */

/* assume lock */
static struct mark *
new_mark (DonnaProviderMark *pm,
          const gchar       *location,
          const gchar       *name,
          DonnaMarkType      type,
          const gchar       *value,
          GError           **error)
{
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;

    if (strchr (location, '/'))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_NAME,
                "Provider 'mark': Invalid mark name '%s': cannot contain '/'",
                location);
        return NULL;
    }

    if (g_hash_table_lookup (priv->marks, location))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_ALREADY_EXIST,
                "Provider 'mark': Mark '%s' already exists",
                location);
        return NULL;
    }

    mark = g_new (struct mark, 1);
    mark->location = g_strdup (location);
    mark->name = g_strdup ((name) ? name : location);
    mark->type = type;
    mark->value = g_strdup (value);

    g_hash_table_insert (priv->marks, mark->location, mark);

    return mark;
}

static gboolean
refresher (DonnaTask *task, DonnaNode *node, const gchar *name)
{
    DonnaProviderMark *pm = (DonnaProviderMark *) donna_node_peek_provider (node);
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    gchar *location;
    GValue v = G_VALUE_INIT;
    gboolean ret = FALSE;

    location = donna_node_get_location (node);
    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    g_free (location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        return FALSE;
    }

    if (streq (name, "name"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, mark->name);
        ret = TRUE;
    }
    else if (streq (name, "full-name"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, mark->location);
        ret = TRUE;
    }
    else if (streq (name, "value"))
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, mark->value);
        ret = TRUE;
    }
    else if (streq (name, "mark-type"))
    {
        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, mark->type);
        ret = TRUE;
    }
    g_mutex_unlock (&priv->mutex);

    if (ret)
    {
        /* this needs to be out of the lock, since node-updated will be emitted
         * and this could otherwise lead to deadlcoks */
        donna_node_set_property_value (node, name, &v);
        g_value_unset (&v);
    }

    return ret;
}

static DonnaTaskState
setter (DonnaTask *task, DonnaNode *node, const gchar *name, const GValue *value)
{
    DonnaProviderMark *pm = (DonnaProviderMark *) donna_node_peek_provider (node);
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    gchar *location;
    DonnaTaskState ret = DONNA_TASK_FAILED;

    location = donna_node_get_location (node);
    g_mutex_lock (&priv->mutex);
    mark =g_hash_table_lookup (priv->marks, location);
    g_free (location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'mark': Cannot set '%s', mark '%s' doesn't exist",
                name, location);
        return DONNA_TASK_FAILED;
    }

    if (streq (name, "name"))
    {
        g_free (mark->name);
        mark->name = g_value_dup_string (value);
        ret = DONNA_TASK_DONE;
    }
    else if (streq (name, "value"))
    {
        g_free (mark->value);
        mark->value = g_value_dup_string (value);
        ret = DONNA_TASK_DONE;
    }
    else if (streq (name, "mark-type"))
    {
        DonnaMarkType type;

        type = (DonnaMarkType) g_value_get_int (value);
        if (type == DONNA_MARK_STANDARD || type == DONNA_MARK_DYNAMIC)
        {
            mark->type = type;
            ret = DONNA_TASK_DONE;
        }
        else
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_INVALID_VALUE,
                    "Provider 'mark': Cannot set type of mark for '%s', "
                    "invalid value (%d)",
                    location, type);
            return DONNA_TASK_FAILED;
        }
    }
    g_mutex_unlock (&priv->mutex);

    if (ret == DONNA_TASK_DONE)
        donna_node_set_property_value (node, name, value);

    return DONNA_TASK_DONE;
}

/* assume lock if data_is_mark */
static DonnaNode *
new_node_for_mark (DonnaProviderMark *pm,
                   gboolean           data_is_mark,
                   gpointer           data,
                   GError **error)
{
    DonnaProviderMarkPrivate *priv = pm->priv;
    struct mark *mark;
    DonnaNode *node;
    GValue v = G_VALUE_INIT;

    if (data_is_mark)
        mark = (struct mark *) data;
    else
    {
        g_mutex_lock (&priv->mutex);
        mark = g_hash_table_lookup (priv->marks, (gchar *) data);
        if (G_UNLIKELY (!mark))
        {
            g_mutex_unlock (&priv->mutex);
            g_set_error (error, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                    "Provider 'mark': Mark '%s' doesn't exist",
                    (gchar *) data);
            return NULL;
        }
    }

    node = donna_node_new ((DonnaProvider *) pm, mark->location,
            DONNA_NODE_ITEM, NULL, refresher, setter, mark->name,
            DONNA_NODE_NAME_WRITABLE);
    if (G_UNLIKELY (!node))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'mark': Failed to create node for mark '%s'",
                mark->location);
        return NULL;
    }

    g_value_init (&v, G_TYPE_INT);
    g_value_set_int (&v, mark->type);
    if (G_UNLIKELY (!donna_node_add_property (node, "mark-type",
                    G_TYPE_INT, &v, refresher, setter, error)))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_prefix_error (error, "Provider 'mark': Cannot create new node, "
                "failed to add property 'mark-type': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, "mark-type");
    if (G_UNLIKELY (!donna_node_add_property (node, "mark-type-extra",
                    G_TYPE_STRING, &v, refresher, NULL, error)))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_prefix_error (error, "Provider 'mark': Cannot create new node, "
                "failed to add property 'mark-type-extra': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, mark->value);
    if (G_UNLIKELY (!donna_node_add_property (node, "value",
                    G_TYPE_STRING, &v, refresher, setter, error)))
    {
        if (!data_is_mark)
            g_mutex_unlock (&priv->mutex);
        g_prefix_error (error, "Provider 'mark': Cannot create new node, "
                "failed to add property 'value': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    if (!data_is_mark)
        g_mutex_unlock (&priv->mutex);
    return node;
}

enum
{
    GET_IF_IN_CACHE = 0,
    GET_CREATE_FROM_LOCATION,
    GET_CREATE_FROM_MARK,
};

/* assume lock if GET_CREATE_FROM_MARK */
static DonnaNode *
get_node_for (DonnaProviderMark *pm,
              guint              how,
              gpointer           data,
              GError           **error)
{
    DonnaProviderBase *pb = (DonnaProviderBase *) pm;
    DonnaProviderBaseClass *klass;
    DonnaNode *node;

    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
    klass->lock_nodes (pb);
    node = klass->get_cached_node (pb, (how == GET_CREATE_FROM_MARK)
            ? ((struct mark *) data)->location : (gchar *) data);
    if (!node && how != GET_IF_IN_CACHE)
    {
        node = new_node_for_mark (pm, how == GET_CREATE_FROM_MARK, data, error);
        if (G_LIKELY (node))
            klass->add_node_to_cache (pb, node);
    }
    klass->unlock_nodes (pb);

    return node;
}

static DonnaTaskState
get_mark_node (DonnaTask            *task,
               DonnaApp             *app,
               const gchar          *location,
               DonnaNode           **node,
               DonnaProviderMark    *pm)
{
    DonnaProviderMarkPrivate *priv = pm->priv;
    GError *err = NULL;
    struct mark *mark;
    DonnaNode *n = NULL;
    DonnaMarkType type;
    DonnaTaskState state;
    DonnaTask *t;

    g_assert (node);

    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    if (G_UNLIKELY (!mark))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Provider 'mark': Mark '%s' doesn't exist",
                location);
        return DONNA_TASK_FAILED;
    }

    t = donna_app_get_node_task (app, mark->value);
    if (G_UNLIKELY (!t))
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'mark': Cannot get %s's get_node task for mark '%s' [%s]",
                (mark->type == DONNA_MARK_STANDARD) ? "dest" : "trigger",
                location, mark->value);
        return DONNA_TASK_FAILED;
    }
    type = mark->type;
    g_mutex_unlock (&priv->mutex);

    donna_task_set_can_block (g_object_ref_sink (t));
    donna_app_run_task (app, t);
    donna_task_wait_for_it (t);

    state = donna_task_get_state (t);
    if (state == DONNA_TASK_DONE)
        n = g_value_dup_object (donna_task_get_return_value (t));
    else if (state == DONNA_TASK_FAILED)
    {
        err = g_error_copy (donna_task_get_error (t));
        g_prefix_error (&err, "Provider 'mark': Failed to get node for mark '%s': ",
                location);
        donna_task_take_error (task, err);
    }
    g_object_unref (t);

    /* in STANDARD we have the node we want. In DYNAMIC we have the node to
     * trigger, which should give us the node we want */
    if (state == DONNA_TASK_DONE && type == DONNA_MARK_DYNAMIC)
    {
        t = donna_node_trigger_task (n, &err);
        if (G_UNLIKELY (!t))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'mark': Cannot get trigger task for mark '%s'",
                    location);
            return DONNA_TASK_FAILED;
        }
        donna_task_set_can_block (g_object_ref_sink (t));
        donna_app_run_task (app, t);
        donna_task_wait_for_it (t);
        g_object_unref (n);

        state = donna_task_get_state (t);
        if (state == DONNA_TASK_DONE)
        {
            const GValue *value;

            value = donna_task_get_return_value (t);
            if (!value)
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'mark': Failed to get node for mark '%s' "
                        "from its trigger: No return value",
                        location);
                state = DONNA_TASK_FAILED;
            }
            else if (!G_VALUE_HOLDS (value, DONNA_TYPE_NODE))
            {
                donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                        DONNA_PROVIDER_ERROR_OTHER,
                        "Provider 'mark': Failed to get node for mark '%s' "
                        "from its trigger: Invalid return type (%s)",
                        location, G_VALUE_TYPE_NAME (value));
                state = DONNA_TASK_FAILED;
            }
            else
                n = g_value_dup_object (donna_task_get_return_value (t));
        }
        else if (state == DONNA_TASK_FAILED)
        {
            err = g_error_copy (donna_task_get_error (t));
            g_prefix_error (&err, "Provider 'mark': Failed to get node for mark '%s' "
                    "from its trigger: ",
                    location);
            donna_task_take_error (task, err);
        }
        g_object_unref (t);
    }

    *node = n;
    return state;
}


/* DonnaProvider */

static DonnaProviderFlags
provider_mark_get_flags (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_MARK (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static const gchar *
provider_mark_get_domain (DonnaProvider *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_MARK (provider), NULL);
    return "mark";
}

/* DonnaProviderBase */

static DonnaTaskState
provider_mark_new_node (DonnaProviderBase  *_provider,
                        DonnaTask          *task,
                        const gchar        *location)
{
    GError *err = NULL;
    DonnaNode *node;
    GValue *value;

    if (streq (location, "/"))
    {
        DonnaProviderBaseClass *klass;
        DonnaNode *n;

        node = donna_node_new ((DonnaProvider *) _provider, location,
                DONNA_NODE_CONTAINER, NULL, (refresher_fn) gtk_true, NULL,
                "Marks", 0);
        if (G_UNLIKELY (!node))
        {
            donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                    DONNA_PROVIDER_ERROR_OTHER,
                    "Provider 'mark': Unable to create a new node");
            return DONNA_TASK_FAILED;
        }

        klass = DONNA_PROVIDER_BASE_GET_CLASS (_provider);
        klass->lock_nodes (_provider);
        n = klass->get_cached_node (_provider, location);
        if (n)
        {
            /* already added while we were busy */
            g_object_unref (node);
            node = n;
        }
        else
            klass->add_node_to_cache (_provider, node);
        klass->unlock_nodes (_provider);
    }
    else
    {
        node = get_node_for ((DonnaProviderMark *) _provider,
                GET_CREATE_FROM_LOCATION, (gpointer) location, &err);
        if (G_UNLIKELY (!node))
        {
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_has_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    DonnaProviderMarkPrivate *priv = ((DonnaProviderMark *) _provider)->priv;
    GValue *value;

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_BOOLEAN);
    g_mutex_lock (&priv->mutex);
    g_value_set_boolean (value, g_hash_table_size (priv->marks) > 0);
    g_mutex_unlock (&priv->mutex);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_get_children (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node,
                            DonnaNodeType       node_types)
{
    DonnaProviderMark *pm = (DonnaProviderMark *) _provider;
    DonnaProviderMarkPrivate *priv = pm->priv;
    GValue *value;
    GPtrArray *nodes;

    /* only one container, root. So we get nodes for all marks */

    if (!(node_types & DONNA_NODE_ITEM))
        /* no containers == return an empty array */
        nodes = g_ptr_array_sized_new (0);
    else
    {
        GHashTableIter iter;
        struct mark *mark;

        g_mutex_lock (&priv->mutex);
        nodes = g_ptr_array_new_full (g_hash_table_size (priv->marks),
                g_object_unref);

        g_hash_table_iter_init (&iter, priv->marks);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer) &mark))
        {
            GError *err = NULL;
            DonnaNode *n;

            n = get_node_for (pm, GET_CREATE_FROM_MARK, mark, &err);
            if (G_UNLIKELY (!n))
            {
                g_mutex_unlock (&priv->mutex);
                g_ptr_array_unref (nodes);
                donna_task_take_error (task, err);
                return DONNA_TASK_FAILED;
            }
            g_ptr_array_add (nodes, n);
        }
        g_mutex_unlock (&priv->mutex);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, G_TYPE_PTR_ARRAY);
    g_value_take_boxed (value, nodes);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_trigger_node (DonnaProviderBase  *_provider,
                            DonnaTask          *task,
                            DonnaNode          *node)
{
    GError *err = NULL;
    DonnaTreeView *tree;
    DonnaNode *n;
    DonnaTaskState state;
    gchar *location;

    g_object_get (_provider->app, "active-list", &tree, NULL);
    if (G_UNLIKELY (!tree))
    {
        gchar *location = donna_node_get_location (node);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'mark': Failed to trigger mark '%s', couldn't get active-list",
                location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    location = donna_node_get_location (node);
    state = get_mark_node (task, _provider->app, location, &n,
            (DonnaProviderMark *) _provider);
    if (state != DONNA_TASK_DONE)
    {
        g_free (location);
        return state;
    }

    if (!donna_tree_view_set_location (tree, n, &err))
    {
        g_prefix_error (&err, "Provider 'mark': Failed to trigger '%s': ",
                location);
        donna_task_take_error (task, err);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    g_free (location);
    return DONNA_TASK_DONE;
}

static DonnaTaskState
provider_mark_new_child (DonnaProviderBase  *_provider,
                         DonnaTask          *task,
                         DonnaNode          *parent,
                         DonnaNodeType       type,
                         const gchar        *name)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = ((DonnaProviderMark *) _provider)->priv;
    struct mark *mark;
    DonnaNode *node_root;
    DonnaNode *node;
    GValue *value;

    if (type == DONNA_NODE_CONTAINER)
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider 'mark': Cannot create a CONTAINER (marks are ITEMs)");
        return DONNA_TASK_FAILED;
    }

    g_mutex_lock (&priv->mutex);
    mark = new_mark ((DonnaProviderMark *) _provider, name, NULL,
            DONNA_MARK_STANDARD, NULL, &err);
    if (!mark)
    {
        g_mutex_unlock (&priv->mutex);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node = get_node_for ((DonnaProviderMark *) _provider, GET_CREATE_FROM_MARK,
            mark, &err);
    g_mutex_unlock (&priv->mutex);
    if (G_UNLIKELY (!node))
    {
        g_prefix_error (&err, "Provider 'mark': Failed to get node for new mark '%s': ",
                name);
        donna_task_take_error (task, err);
        return DONNA_TASK_FAILED;
    }

    node_root = get_node_for ((DonnaProviderMark *) _provider, GET_IF_IN_CACHE,
            "/", NULL);
    if (node_root)
    {
        donna_provider_node_new_child ((DonnaProvider *) _provider,
                node_root, node);
        g_object_unref (node_root);
    }

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

/* commands */

static DonnaTaskState
cmd_mark_get_node (DonnaTask         *task,
                   DonnaApp          *app,
                   gpointer          *args,
                   DonnaProviderMark *pm)
{
    const gchar *location = args[0];

    DonnaTaskState state;
    DonnaNode *node;
    GValue *value;

    state = get_mark_node (task, app, location, &node, pm);
    if (state != DONNA_TASK_DONE)
        return state;

    value = donna_task_grab_return_value (task);
    g_value_init (value, DONNA_TYPE_NODE);
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

static DonnaTaskState
cmd_mark_set (DonnaTask         *task,
              DonnaApp          *app,
              gpointer          *args,
              DonnaProviderMark *pm)
{
    GError *err = NULL;
    DonnaProviderMarkPrivate *priv = pm->priv;

    const gchar *location = args[0];
    const gchar *name = args[1]; /*opt */
    const gchar *type = args[2]; /* opt */
    const gchar *value = args[3]; /*opt */

    struct mark *mark;
    const gchar *s_types[] = { "standard", "dynamic" };
    DonnaMarkType m_types[] = { DONNA_MARK_STANDARD, DONNA_MARK_DYNAMIC };
    DonnaMarkType m_type;
    gint t = -1;
    DonnaNode *node;
    enum {
        UPD_NAME    = (1 << 0),
        UPD_VALUE   = (1 << 1),
    } updated = 0;

    if (type)
    {
        t = _get_choice (s_types, type);
        if (t < 0)
        {
            donna_task_set_error (task, DONNA_COMMAND_ERROR,
                    DONNA_COMMAND_ERROR_SYNTAX,
                    "Command 'mark_set': Cannot set mark '%s', invalid type '%s'; "
                    "Must be 'standard' or 'dynamic'",
                    location, type);
            return DONNA_TASK_FAILED;
        }
        m_type = m_types[t];
    }
    else
        m_type = DONNA_MARK_STANDARD;

    g_mutex_lock (&priv->mutex);
    mark = g_hash_table_lookup (priv->marks, location);
    if (mark)
    {
        if (name && !streq (mark->name, name))
        {
            g_free (mark->name);
            mark->name = g_strdup (name);
            updated |= UPD_NAME;
        }
        if (type && mark->type != m_type)
            mark->type = m_type;
        if (value && !streq (mark->value, value))
        {
            g_free (mark->value);
            mark->value = g_strdup (value);
            updated |= UPD_VALUE;
        }
        g_mutex_unlock (&priv->mutex);

        node = get_node_for (pm, GET_IF_IN_CACHE, (gpointer) location, NULL);
        if (node)
        {
            GValue v = G_VALUE_INIT;

            if (updated & UPD_NAME)
            {
                g_value_init (&v, G_TYPE_STRING);
                g_value_set_string (&v, name);
                donna_node_set_property_value (node, "name", &v);
                g_value_unset (&v);
            }

            if (updated & UPD_VALUE)
            {
                g_value_init (&v, G_TYPE_STRING);
                g_value_set_string (&v, value);
                donna_node_set_property_value (node, "value", &v);
                g_value_unset (&v);
            }

            g_object_unref (node);
        }
    }
    else
    {
        DonnaNode *node_root;

        mark = new_mark (pm, location, name, m_type, value, &err);
        if (!mark)
        {
            g_mutex_unlock (&priv->mutex);
            donna_task_take_error (task, err);
            return DONNA_TASK_FAILED;
        }

        node_root = get_node_for (pm, GET_IF_IN_CACHE, "/", NULL);
        if (node_root)
        {
            node = get_node_for (pm, GET_CREATE_FROM_MARK, mark, NULL);
            g_mutex_unlock (&priv->mutex);

            donna_provider_node_new_child ((DonnaProvider *) pm, node_root, node);
            g_object_unref (node);
            g_object_unref (node_root);
        }
    }

    return DONNA_TASK_DONE;
}


/* load/save */

/* assume lock */
static inline void
load_marks (DonnaProviderMark *pm)
{
    GError *err = NULL;
    DonnaApp *app = ((DonnaProviderBase *) pm)->app;
    struct mark m;
    gboolean in_mark = FALSE;
    gchar *file;
    gchar *data;
    gchar *s, *e;

    file = donna_app_get_conf_filename (app, "marks.conf");
    if (!g_file_get_contents (file, &data, NULL, &err))
    {
        if (!g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning ("Unable to load marks from '%s': %s", file, err->message);
        g_clear_error (&err);
        g_free (file);
        return;
    }
    g_free (file);

    s = data;
    for (;;)
    {
        e = strchr (s, '\n');
        if (e)
            *e = '\0';
        if (streqn (s, "mark=", 5))
        {
            if (in_mark)
            {
                if (!new_mark (pm, m.location, m.name, m.type, m.value, &err))
                {
                    g_warning ("Provider 'mark': Failed to load mark '%s': %s",
                            m.location, err->message);
                    g_clear_error (&err);
                }
            }
            else
                in_mark = TRUE;
            memset (&m, 0, sizeof (struct mark));
            m.location = s + 5;
        }
        else if (in_mark)
        {
            if (streqn (s, "name=", 5))
            {
                if (!strchr (s, '/'))
                    m.name = s + 5;
            }
            else if (streqn (s, "type=", 5))
            {
                if (s[5] == '0' || s[5] == '1')
                    m.type = (s[5] == '0') ? DONNA_MARK_STANDARD : DONNA_MARK_DYNAMIC;
            }
            else if (streqn (s, "value=", 6))
                m.value = s + 6;
        }
        if (e)
            s = e + 1;
        else
            break;
    }

    if (in_mark)
    {
        if (!new_mark (pm, m.location, m.name, m.type, m.value, &err))
        {
            g_warning ("Provider 'mark': Failed to load mark '%s': %s",
                    m.location, err->message);
            g_clear_error (&err);
        }
    }

    g_free (data);
}


#define add_command(cmd_name, cmd_argc, cmd_visibility, cmd_return_value) \
if (G_UNLIKELY (!donna_provider_command_add_command (pc, #cmd_name, cmd_argc, \
            arg_type, cmd_return_value, cmd_visibility, \
            (command_fn) cmd_##cmd_name, object, NULL, &err))) \
{ \
    g_warning ("Provider 'mark': Failed to add command '" #cmd_name "': %s", \
        err->message); \
    g_clear_error (&err); \
}
static void
provider_mark_contructed (GObject *object)
{
    GError *err = NULL;
    DonnaProviderCommand *pc;
    DonnaArgType arg_type[8];
    gint i;

    G_OBJECT_CLASS (donna_provider_mark_parent_class)->constructed (object);

    load_marks ((DonnaProviderMark *) object);

    pc = (DonnaProviderCommand *) donna_app_get_provider (
            ((DonnaProviderBase *) object)->app, "command");
    if (G_UNLIKELY (!pc))
    {
        g_warning ("Provider 'mark': Failed to add commands, "
                "couldn't get provider 'command'");
        return;
    }

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    add_command (mark_get_node, ++i, DONNA_TASK_VISIBILITY_INTERNAL,
            DONNA_ARG_TYPE_NODE);

    i = -1;
    arg_type[++i] = DONNA_ARG_TYPE_STRING;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    arg_type[++i] = DONNA_ARG_TYPE_STRING | DONNA_ARG_IS_OPTIONAL;
    add_command (mark_set, ++i, DONNA_TASK_VISIBILITY_INTERNAL_FAST,
            DONNA_ARG_TYPE_NOTHING);

    g_object_unref (pc);
}
#undef add_command