
#include <gtk/gtk.h>    /* see provider_base_set_property_icon() */
#include <string.h>
#include <stdlib.h>
#include "provider-base.h"
#include "provider.h"
#include "node.h"
#include "task.h"
#include "debug.h"
#include "macros.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct _DonnaProviderBasePrivate
{
    GHashTable  *nodes;
    GRecMutex    nodes_mutex;
};

static void             provider_base_set_property      (GObject        *object,
                                                         guint           prop_id,
                                                         const GValue   *value,
                                                         GParamSpec     *pspec);
static void             provider_base_get_property      (GObject        *object,
                                                         guint           prop_id,
                                                         GValue         *value,
                                                         GParamSpec     *pspec);
static void             provider_base_finalize          (GObject        *object);

/* DonnaProviderBase */
static void             provider_base_lock_nodes (
                                            DonnaProviderBase   *provider);
static void             provider_base_unlock_nodes (
                                            DonnaProviderBase   *provider);
static DonnaNode *      provider_base_get_cached_node (
                                            DonnaProviderBase   *provider,
                                            const gchar         *location);
static void             provider_base_add_node_to_cache (
                                            DonnaProviderBase   *provider,
                                            DonnaNode           *node);
static gboolean         provider_base_set_property_icon (
                                             DonnaProviderBase  *provider,
                                             DonnaNode          *node,
                                             const gchar        *property,
                                             const gchar        *icon,
                                             GError            **error);

/* DonnaProvider */
static void             provider_base_node_updated (
                                            DonnaProvider  *provider,
                                            DonnaNode      *node,
                                            const gchar    *name);
static DonnaTask *      provider_base_get_node_task (
                                            DonnaProvider       *provider,
                                            const gchar         *location,
                                            GError             **error);
static DonnaTask *      provider_base_has_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types,
                                            GError             **error);
static DonnaTask *      provider_base_get_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types,
                                            GError             **error);
static DonnaTask *      provider_base_get_node_parent_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            GError             **error);
static DonnaTask *      provider_base_trigger_node_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            GError             **error);
static DonnaTask *      provider_base_io_task (
                                            DonnaProvider       *provider,
                                            DonnaIoType          type,
                                            gboolean             is_source,
                                            GPtrArray           *sources,
                                            DonnaNode           *dest,
                                            GError             **error);
static DonnaTask *      provider_base_new_child_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *parent,
                                            DonnaNodeType        type,
                                            const gchar         *name,
                                            GError             **error);

static void
provider_base_provider_init (DonnaProviderInterface *interface)
{
    interface->node_updated           = provider_base_node_updated;

    interface->get_node_task          = provider_base_get_node_task;
    interface->has_node_children_task = provider_base_has_node_children_task;
    interface->get_node_children_task = provider_base_get_node_children_task;
    interface->get_node_parent_task   = provider_base_get_node_parent_task;
    interface->trigger_node_task      = provider_base_trigger_node_task;
    interface->io_task                = provider_base_io_task;
    interface->new_child_task         = provider_base_new_child_task;
}

static void
donna_provider_base_class_init (DonnaProviderBaseClass *klass)
{
    GObjectClass *o_class;

    klass->lock_nodes        = provider_base_lock_nodes;
    klass->unlock_nodes      = provider_base_unlock_nodes;
    klass->get_cached_node   = provider_base_get_cached_node;
    klass->add_node_to_cache = provider_base_add_node_to_cache;
    klass->set_property_icon = provider_base_set_property_icon;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = provider_base_set_property;
    o_class->get_property   = provider_base_get_property;
    o_class->finalize       = provider_base_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaProviderBasePrivate));
}

static void
donna_provider_base_init (DonnaProviderBase *provider)
{
    DonnaProviderBasePrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_BASE,
            DonnaProviderBasePrivate);
    priv->nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, g_object_unref);
    g_rec_mutex_init (&priv->nodes_mutex);
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (DonnaProviderBase, donna_provider_base,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_base_provider_init)
        )

static void
provider_base_finalize (GObject *object)
{
    DonnaProviderBasePrivate *priv;

    priv = DONNA_PROVIDER_BASE (object)->priv;

    g_hash_table_destroy (priv->nodes);
    g_rec_mutex_clear (&priv->nodes_mutex);
    g_object_unref (((DonnaProviderBase *) object)->app);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_base_parent_class)->finalize (object);
}

static void
provider_base_set_property (GObject        *object,
                            guint           prop_id,
                            const GValue   *value,
                            GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        ((DonnaProviderBase *) object)->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_base_get_property (GObject        *object,
                            guint           prop_id,
                            GValue         *value,
                            GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, ((DonnaProviderBase *) object)->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_base_lock_nodes (DonnaProviderBase *provider)
{
    g_return_if_fail (DONNA_IS_PROVIDER_BASE (provider));
    g_rec_mutex_lock (&provider->priv->nodes_mutex);
}

static void
provider_base_unlock_nodes (DonnaProviderBase *provider)
{
    g_return_if_fail (DONNA_IS_PROVIDER_BASE (provider));
    g_rec_mutex_unlock (&provider->priv->nodes_mutex);
}

/* must be called while mutex is locked */
static DonnaNode *
provider_base_get_cached_node (DonnaProviderBase *provider,
                               const gchar       *location)
{
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    node = g_hash_table_lookup (provider->priv->nodes, location);
    if (node)
        g_object_ref (node);
    return node;
}

static void
node_toggle_ref_cb (DonnaProviderBase   *_provider,
                    DonnaNode           *node,
                    gboolean             is_last)
{
    /* Here's why we use a recursive mutex for nodes: in case at the same time
     * (i.e. in 2 threads) we have someone unref-ing the node making us the
     * owner of the last ref (hence this toggle_ref is_last=TRUE triggered), and
     * someone asking for this node.
     * To ensure that we don't remove the node from our hashtable & unref it
     * while in another thread taking another ref on it and returning the node
     * to someone - which would lead to troubles - we use a rec_mutex.
     *
     * T1: unref node, trigger toggle_ref (is_last=TRUE)
     * T2: ask for node, lock rec_mutex
     * T1: toggle_ref waits for rec_mutex
     * T2: adds a ref, trigger toggle_ref (is_last=FALSE)
     * T2: toggle_ref gets rec_mutex and calls node_inc (1->2)
     * T2: unlocks rec_mutex (twice)
     * T1: gets rec_mutex, calls node_dec (2->1) and does nothing
     *
     * If T1 has locked rec_mutex, it would have unref-d the node, and by the
     * time T2 got the rec_mutex, the node would be gone, and would have to be
     * recreated.
     *
     * In simpler terms, here are the two threads:
     *
     * T1: toggle_ref when we own the last ref
     * - lock RM
     * - flg_is_last--
     * - if flg_is_last>0 unlock RM, abort
     * - remove node
     * - unlock RM
     * - unref node
     *
     * T2: asking for the node
     * - lock RM
     * - get node
     * - ref node --> toggle_ref: lock RM, flg_is_last++, unlock RM
     * - unlock RM
     */

    g_rec_mutex_lock (&_provider->priv->nodes_mutex);
    if (is_last)
    {
        gchar *location;
        int c;

        c = donna_node_dec_toggle_count (node);
        if (c > 0)
        {
            g_rec_mutex_unlock (&_provider->priv->nodes_mutex);
            return;
        }

        /* we call this to let the provider know the node is being
         * removed/finalized, in case it then needs to go to cleaning as well */
        if (DONNA_PROVIDER_BASE_GET_CLASS (_provider)->unref_node)
            DONNA_PROVIDER_BASE_GET_CLASS (_provider)->unref_node (_provider, node);
        /* sanity check */
        if (G_UNLIKELY (donna_node_get_toggle_count (node) > 0))
        {
            g_rec_mutex_unlock (&_provider->priv->nodes_mutex);
            return;
        }

        location = donna_node_get_location (node);
        /* this also removes our last ref on node */
        g_hash_table_remove (_provider->priv->nodes, location);
        g_rec_mutex_unlock (&_provider->priv->nodes_mutex);
        g_free (location);
    }
    else
    {
        donna_node_inc_toggle_count (node);
        g_rec_mutex_unlock (&_provider->priv->nodes_mutex);
    }
}

static void
provider_base_node_updated (DonnaProvider  *provider,
                            DonnaNode      *node,
                            const gchar    *name)
{
    DonnaProviderBasePrivate *priv = ((DonnaProviderBase *) provider)->priv;
    GHashTableIter iter;
    gpointer key, value;

    if (!streq (name, "location"))
        return;

    /* should be rare, but nodes can change location (e.g. rename), in which
     * case we need to find it via value (since the location changed), then we
     * remove it & re-add it with the new location as key */

    g_rec_mutex_lock (&priv->nodes_mutex);
    g_hash_table_iter_init (&iter, priv->nodes);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if ((DonnaNode *) value != node)
            continue;

        /* removing it will unref node, so ref it before */
        g_object_ref (node);
        g_hash_table_iter_remove (&iter);
        g_hash_table_insert (priv->nodes, donna_node_get_location (node), node);
        break;
    }
    g_rec_mutex_unlock (&priv->nodes_mutex);
}

/* must be called while mutex is locked */
static void
provider_base_add_node_to_cache (DonnaProviderBase *provider,
                                 DonnaNode         *node)
{
    gchar *location;

    g_return_if_fail (DONNA_IS_PROVIDER_BASE (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    location = donna_node_get_location (node);

    /* add a toggleref, so when we have the last reference on the node, we
     * can let it go (Note: this adds a (strong) reference to node) */
    g_object_add_toggle_ref (G_OBJECT (node),
                             (GToggleNotify) node_toggle_ref_cb,
                             provider);

    /* add the node to our hash table -- location is a strdup already */
    g_hash_table_insert (provider->priv->nodes, location, node);

    /* emit new-node signal */
    donna_provider_new_node ((DonnaProvider *) provider, node);

    /* mark node ready */
    donna_node_mark_ready (node);
}

struct spi
{
    DonnaNode   *node;
    const gchar *property;
    const gchar *icon;
    GError **error;
};

static DonnaTaskState
set_property_icon (DonnaTask *task, struct spi *spi)
{
    GValue v = G_VALUE_INIT;
    GdkPixbuf *pixbuf;

    pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
            spi->icon, /*FIXME*/16, 0, spi->error);
    if (!pixbuf)
        return DONNA_TASK_FAILED;

    g_value_init (&v, GDK_TYPE_PIXBUF);
    g_value_take_object (&v, pixbuf);
    donna_node_set_property_value (spi->node, spi->property, &v);
    g_value_unset (&v);

    return DONNA_TASK_DONE;
}

gboolean
_provider_base_set_property_icon (DonnaApp      *app,
                                  DonnaNode     *node,
                                  const gchar   *property,
                                  const gchar   *icon,
                                  GError       **error)
{
    DonnaTask *task;
    struct spi data = { node, property, icon, error };
    gboolean ret;

    g_return_if_fail (DONNA_IS_APP (app));
    g_return_if_fail (DONNA_IS_NODE (node));
    g_return_if_fail (property != NULL);
    g_return_if_fail (icon != NULL);

    task = donna_task_new ((task_fn) set_property_icon, &data, NULL);
    donna_task_set_visibility (task, DONNA_TASK_VISIBILITY_INTERNAL_GUI);
    donna_task_set_can_block (g_object_ref_sink (task));
    donna_app_run_task (app, task);
    donna_task_wait_for_it (task);
    ret = donna_task_get_state (task) == DONNA_TASK_DONE;
    g_object_unref (task);
    return ret;
}

static gboolean
provider_base_set_property_icon (DonnaProviderBase  *provider,
                                 DonnaNode          *node,
                                 const gchar        *property,
                                 const gchar        *icon,
                                 GError            **error)
{
    g_return_if_fail (DONNA_IS_PROVIDER_BASE (provider));
    _provider_base_set_property_icon (provider->app, node, property, icon, error);
}

struct get_node_data
{
    DonnaProviderBase   *provider_base;
    gchar               *location;
};

static void
free_get_node_data (struct get_node_data *data)
{
    g_object_unref (data->provider_base);
    g_free (data->location);
    g_slice_free (struct get_node_data, data);
}

static DonnaTaskState
get_node (DonnaTask *task, struct get_node_data *data)
{
    DonnaProviderBasePrivate *priv;
    DonnaNode *node;
    DonnaTaskState ret;

    priv = data->provider_base->priv;

    /* first make sure it wasn't created before the task started */
    g_rec_mutex_lock (&priv->nodes_mutex);
    node = provider_base_get_cached_node (data->provider_base, data->location);
    g_rec_mutex_unlock (&priv->nodes_mutex);
    if (node)
    {
        GValue *value;

        value = donna_task_grab_return_value (task);
        g_value_init (value, G_TYPE_OBJECT);
        /* a ref was added for us by provider_base_get_cached_node */
        g_value_take_object (value, node);
        donna_task_release_return_value (task);

        return DONNA_TASK_DONE;
    }

    /* create the node. It is new_node's responsability to lock, call
     * add_node_to_cache, unlock */
    ret = DONNA_PROVIDER_BASE_GET_CLASS (data->provider_base)->new_node (
            data->provider_base,
            task,
            data->location);

    free_get_node_data (data);
    return ret;
}

static DonnaTask *
provider_base_get_node_task (DonnaProvider    *provider,
                             const gchar      *location,
                             GError          **error)
{
    DonnaProviderBase *p = (DonnaProviderBase *) provider;
    DonnaTask *task;
    struct get_node_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (p), NULL);
    g_return_val_if_fail (DONNA_PROVIDER_BASE_GET_CLASS (provider)->new_node != NULL, NULL);

    data = g_slice_new0 (struct get_node_data);
    data->provider_base = g_object_ref (p);
    data->location = g_strdup (location);
    task = donna_task_new ((task_fn) get_node, data,
            (GDestroyNotify) free_get_node_data);

    DONNA_DEBUG (TASK,
            donna_task_take_desc (task, g_strdup_printf ("get_node() for '%s:%s'",
                    donna_provider_get_domain (provider),
                    location)));

    return task;
}

struct node_children_data
{
    DonnaProviderBase   *provider_base;
    DonnaNode           *node;
    DonnaNodeType        node_types;
};

static void
free_node_children_data (struct node_children_data *data)
{
    g_object_unref (data->node);
    g_slice_free (struct node_children_data, data);
}

static DonnaTaskState
has_children (DonnaTask *task, struct node_children_data *data)
{
    DonnaTaskState ret;

    ret = DONNA_PROVIDER_BASE_GET_CLASS (data->provider_base)->has_children (
            data->provider_base,
            task,
            data->node,
            data->node_types);
    free_node_children_data (data);
    return ret;
}

static DonnaTask *
provider_base_has_node_children_task (DonnaProvider *provider,
                                      DonnaNode     *node,
                                      DonnaNodeType  node_types,
                                      GError       **error)
{
    DonnaTask *task;
    struct node_children_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);
    g_return_val_if_fail (DONNA_PROVIDER_BASE_GET_CLASS (provider)->has_children != NULL, NULL);

    data = g_slice_new0 (struct node_children_data);
    data->provider_base = DONNA_PROVIDER_BASE (provider);
    data->node          = g_object_ref (node);
    data->node_types    = node_types;
    task = donna_task_new ((task_fn) has_children, data,
            (GDestroyNotify) free_node_children_data);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "has_children() for node '%s:%s'",
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

static DonnaTaskState
get_children (DonnaTask *task, struct node_children_data *data)
{
    DonnaTaskState ret;

    ret = DONNA_PROVIDER_BASE_GET_CLASS (data->provider_base)->get_children (
            data->provider_base,
            task,
            data->node,
            data->node_types);

    if (ret == DONNA_TASK_DONE)
        /* emit node-children */
        donna_provider_node_children (DONNA_PROVIDER (data->provider_base),
                data->node,
                data->node_types,
                g_value_get_boxed (donna_task_get_return_value (task)));

    free_node_children_data (data);
    return ret;
}

static DonnaTask *
provider_base_get_node_children_task (DonnaProvider  *provider,
                                      DonnaNode      *node,
                                      DonnaNodeType   node_types,
                                      GError        **error)
{
    DonnaTask *task;
    struct node_children_data *data;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);
    g_return_val_if_fail (DONNA_PROVIDER_BASE_GET_CLASS (provider)->get_children != NULL, NULL);

    data = g_slice_new0 (struct node_children_data);
    data->provider_base = DONNA_PROVIDER_BASE (provider);
    data->node          = g_object_ref (node);
    data->node_types    = node_types;
    task = donna_task_new ((task_fn) get_children, data,
            (GDestroyNotify) free_node_children_data);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "get_children() for node '%s:%s'",
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

static DonnaTaskState
get_node_parent (DonnaTask *task, DonnaNode *node)
{
    DonnaProviderBase *provider_base;
    gchar *location;
    DonnaNode *parent;
    gchar *s;
    DonnaTaskState ret;

    provider_base = (DonnaProviderBase *) donna_node_peek_provider (node);
    location = donna_node_get_location (node);

    /* is this root? */
    if (streq (location, "/"))
    {
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
                "Node '%s:%s' has no parent",
                donna_provider_get_domain ((DonnaProvider *) provider_base),
                location);
        g_free (location);
        return DONNA_TASK_FAILED;
    }

    s = strrchr (location, '/');
    if (s != location)
        *s = '\0';

    g_rec_mutex_lock (&provider_base->priv->nodes_mutex);
    parent = provider_base_get_cached_node (provider_base,
            (s == location) ? "/" : location);
    g_rec_mutex_unlock (&provider_base->priv->nodes_mutex);
    if (!parent)
        /* create the node. It is new_node's responsability to lock, call
         * add_node_to_cache, unlock */
        ret = DONNA_PROVIDER_BASE_GET_CLASS (provider_base)->new_node (
                provider_base,
                task,
                (s == location) ? "/" : location);
    else
    {
        GValue *value;

        value = donna_task_grab_return_value (task);
        g_value_init (value, G_TYPE_OBJECT);
        /* a ref was added for us by provider_base_get_cached_node */
        g_value_take_object (value, parent);
        donna_task_release_return_value (task);
        ret = DONNA_TASK_DONE;
    }

    g_free (location);
    return ret;
}

static DonnaTask *
provider_base_get_node_parent_task (DonnaProvider   *provider,
                                    DonnaNode       *node,
                                    GError         **error)
{
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);
    g_return_val_if_fail (DONNA_PROVIDER_BASE_GET_CLASS (provider)->new_node != NULL, NULL);

    task = donna_task_new ((task_fn) get_node_parent, g_object_ref (node),
            g_object_unref);

    DONNA_DEBUG (TASK,
            gchar *location = donna_node_get_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "get_node_parent() for node '%s:%s'",
                    donna_node_get_domain (node),
                    location));
            g_free (location));

    return task;
}

static DonnaTaskState
trigger_node (DonnaTask *task, DonnaNode *node)
{
    DonnaProviderBase *provider_base;
    DonnaTaskState ret;

    provider_base = (DonnaProviderBase *) donna_node_peek_provider (node);
    ret = DONNA_PROVIDER_BASE_GET_CLASS (provider_base)->trigger_node (
            provider_base, task, node);
    g_object_unref (node);
    return ret;
}

static DonnaTask *
provider_base_trigger_node_task (DonnaProvider       *provider,
                                 DonnaNode           *node,
                                 GError             **error)
{
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);
    g_return_val_if_fail (DONNA_PROVIDER_BASE_GET_CLASS (provider)->trigger_node != NULL, NULL);

    task = donna_task_new ((task_fn) trigger_node, g_object_ref (node),
            g_object_unref);

    DONNA_DEBUG (TASK,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "trigger_node() for node '%s'", fl));
            g_free (fl));

    return task;
}

struct io
{
    DonnaProviderBase   *pb;
    DonnaIoType          type;
    gboolean             is_source;
    GPtrArray           *sources;
    DonnaNode           *dest;
};

static void
free_io (struct io *io)
{
    g_ptr_array_unref (io->sources);
    if (io->dest)
        g_object_unref (io->dest);
    g_slice_free (struct io, io);
}

static DonnaTaskState
perform_io (DonnaTask *task, struct io *io)
{
    DonnaTaskState ret;

    ret = DONNA_PROVIDER_BASE_GET_CLASS (io->pb)->io (
            io->pb, task, io->type, io->is_source, io->sources, io->dest);
    free_io (io);
    return ret;
}

static DonnaTask *
provider_base_io_task (DonnaProvider       *provider,
                       DonnaIoType          type,
                       gboolean             is_source,
                       GPtrArray           *sources,
                       DonnaNode           *dest,
                       GError             **error)
{
    DonnaTask *task;
    struct io *io;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);

    if (DONNA_PROVIDER_BASE_GET_CLASS (provider)->support_io == NULL
            || DONNA_PROVIDER_BASE_GET_CLASS (provider)->io == NULL)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': No support of IO operations",
                donna_provider_get_domain (provider));
        return NULL;
    }

    if (!DONNA_PROVIDER_BASE_GET_CLASS (provider)->support_io (
                (DonnaProviderBase *) provider, type, is_source, sources, dest, error))
        return NULL;

    io = g_slice_new (struct io);
    io->pb          = (DonnaProviderBase *) provider;
    io->type        = type;
    io->is_source   = is_source;
    io->sources     = g_ptr_array_ref (sources);
    io->dest        = (dest) ? g_object_ref (dest) : NULL;

    task = donna_task_new ((task_fn) perform_io, io, (GDestroyNotify) free_io);

    DONNA_DEBUG (TASK,
            gchar *fl = donna_node_get_full_location (dest);
            donna_task_take_desc (task, g_strdup_printf (
                    "io() %s (from %s as %s) with %d sources to '%s'",
                    (type == DONNA_IO_COPY) ? "copy" :
                    (type == DONNA_IO_MOVE) ? "move" :
                    (type == DONNA_IO_DELETE) ? "delete" : "unknown",
                    donna_provider_get_domain (provider),
                    (is_source) ? "source" : "dest",
                    sources->len,
                    fl));
            g_free (fl));

    return task;
}

struct new_child
{
    DonnaNode       *parent;
    DonnaNodeType    type;
    gchar           *name;
};

static void
free_new_child (struct new_child *nc)
{
    g_object_unref (nc->parent);
    g_free (nc->name);
    g_slice_free (struct new_child, nc);
}

static DonnaTaskState
new_child (DonnaTask *task, struct new_child *nc)
{
    DonnaProviderBase *provider_base;
    DonnaTaskState ret;

    provider_base = (DonnaProviderBase *) donna_node_peek_provider (nc->parent);
    ret = DONNA_PROVIDER_BASE_GET_CLASS (provider_base)->new_child (
            provider_base, task, nc->parent, nc->type, nc->name);
    free_new_child (nc);
    return ret;
}

static DonnaTask *
provider_base_new_child_task (DonnaProvider       *provider,
                              DonnaNode           *parent,
                              DonnaNodeType        type,
                              const gchar         *name,
                              GError             **error)
{
    DonnaTask *task;
    struct new_child *nc;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (provider), NULL);

    if (DONNA_PROVIDER_BASE_GET_CLASS(provider)->new_child == NULL)
    {
        g_set_error (error, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
                "Provider '%s': No support of node creation",
                donna_provider_get_domain (provider));
        return NULL;
    }

    nc = g_slice_new (struct new_child);
    nc->parent  = g_object_ref (parent);
    nc->type    = type;
    nc->name    = g_strdup (name);

    task = donna_task_new ((task_fn) new_child, nc, (GDestroyNotify) free_new_child);

    DONNA_DEBUG (TASK,
            gchar *fl = donna_node_get_full_location (parent);
            donna_task_take_desc (task, g_strdup_printf (
                    "new_child() '%s' (%s) on '%s'",
                    name, (type == DONNA_NODE_ITEM) ? "item" : "container", fl));
            g_free (fl));

    return task;
}
