
#include <glib-object.h>
#include "provider-base.h"
#include "provider.h"
#include "node.h"
#include "task.h"

struct _DonnaProviderBasePrivate
{
    GHashTable  *nodes;
    GRecMutex    nodes_mutex;
};

static void             provider_base_finalize          (GObject *object);

/* DonnaProviderBase */
static DonnaNode *      provider_base_get_cached_node   (DonnaProviderBase  *provider,
                                                         const gchar        *location);
static void             provider_base_add_node_to_cache (DonnaProviderBase  *provider,
                                                         DonnaNode          *node);

/* DonnaProvider */
static DonnaTask *      provider_base_get_node      (DonnaProvider    *provider,
                                                     const gchar      *location,
                                                     task_callback_fn  callback,
                                                     gpointer          callback_data,
                                                     GDestroyNotify    callback_destroy,
                                                     guint             timeout,
                                                     task_timeout_fn   timeout_callback,
                                                     gpointer          timeout_data,
                                                     GDestroyNotify    timeout_destroy,
                                                     GError          **error);
static DonnaTask *      provider_base_get_content   (DonnaProvider    *provider,
                                                     DonnaNode        *node,
                                                     task_callback_fn  callback,
                                                     gpointer          callback_data,
                                                     GDestroyNotify    callback_destroy,
                                                     guint             timeout,
                                                     task_timeout_fn   timeout_callback,
                                                     gpointer          timeout_data,
                                                     GDestroyNotify    timeout_destroy,
                                                     GError          **error);
static DonnaTask *      provider_base_get_children  (DonnaProvider    *provider,
                                                     DonnaNode        *node,
                                                     task_callback_fn  callback,
                                                     gpointer          callback_data,
                                                     GDestroyNotify    callback_destroy,
                                                     guint             timeout,
                                                     task_timeout_fn   timeout_callback,
                                                     gpointer          timeout_data,
                                                     GDestroyNotify    timeout_destroy,
                                                     GError          **error);
static DonnaTask *      provider_base_remove_node   (DonnaProvider    *provider,
                                                     DonnaNode        *node,
                                                     task_callback_fn  callback,
                                                     gpointer          callback_data,
                                                     GDestroyNotify    callback_destroy,
                                                     guint             timeout,
                                                     task_timeout_fn   timeout_callback,
                                                     gpointer          timeout_data,
                                                     GDestroyNotify    timeout_destroy,
                                                     GError          **error);

static void
provider_base_provider_init (DonnaProviderInterface *interface)
{
    interface->get_node = provider_base_get_node;
    interface->get_content = provider_base_get_content;
    interface->get_children = provider_base_get_children;
    interface->remove_node = provider_base_remove_node;
}

static void
provider_base_class_init (DonnaProviderBaseClass *klass)
{
    GObjectClass *o_class;

    klass->get_cached_node = provider_base_get_cached_node;
    klass->add_node_to_cache = provider_base_add_node_to_cache;

    o_class = (GObjectClass *) klass;
    o_class->finalize = provider_base_finalize;

    g_type_class_add_private (klass, sizeof (DonnaProviderBasePrivate));
}

static void
provider_base_init (DonnaProviderBase *provider)
{
    DonnaProviderBasePrivate *priv;

    priv = provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_BASE,
            DonnaProviderBasePrivate);
    priv->nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
            g_free, g_object_unref);
    g_rec_mutex_init (&priv->nodes_mutex);
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (DonnaProviderBase, provider_base,
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

    /* chain up */
    G_OBJECT_CLASS (provider_base_parent_class)->finalize (object);
}

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
node_toggle_ref_cb (DonnaProviderBase   *provider,
                    DonnaNode           *node,
                    gboolean             is_last)
{
    int c;

    g_rec_mutex_lock (&provider->priv->nodes_mutex);
    if (is_last)
    {
        gchar *location;

        c = donna_node_dec_toggle_count (node);
        if (c)
        {
            g_rec_mutex_unlock (&provider->priv->nodes_mutex);
            return;
        }
        donna_node_get (node, "location", &location, NULL);
        g_hash_table_remove (provider->priv->nodes, location);
        g_rec_mutex_unlock (&provider->priv->nodes_mutex);
        g_object_unref (node);
        g_free (location);
    }
    else
    {
        donna_node_inc_toggle_count (node);
        g_rec_mutex_unlock (&provider->priv->nodes_mutex);
    }
}

static void
provider_base_add_node_to_cache (DonnaProviderBase *provider,
                                 DonnaNode         *node)
{
    DonnaProviderBase *p;
    gchar *location;

    g_return_if_fail (DONNA_IS_PROVIDER_BASE (provider));
    g_return_if_fail (DONNA_IS_NODE (node));

    donna_node_get (node, "provider", &p, "location", &location, NULL);

    /* make sure the provider is the node's provider */
    g_object_unref (p);
    g_return_if_fail (p == provider);

    /* add a toggleref, so when we have the last reference on the node, we
     * can let it go (Note: this adds a (strong) reference to node) */
    g_object_add_toggle_ref (G_OBJECT (node),
                             (GToggleNotify) node_toggle_ref_cb,
                             provider);

    /* add the node to our hash table (location was strdup-ed when we got it) */
    g_hash_table_insert (provider->priv->nodes, location, node);
}

static DonnaTaskState
return_node (DonnaTask *task, DonnaNode *node)
{
    GValue *value;

    value = donna_task_take_return_value (task);
    g_value_init (value, G_TYPE_OBJECT);
    /* take_object to not increment the ref count, as it was already done for
     * this task when creating it (to ensure the node wouldn't go away e.g.
     * during thread creation) */
    g_value_take_object (value, node);
    donna_task_release_return_value (task);

    return DONNA_TASK_DONE;
}

struct get_node_data
{
    DonnaProvider   *provider;
    gchar           *location;
};

static void
free_get_node_data (struct get_node_data *data)
{
    g_object_unref (data->provider);
    g_free (data->location);
    g_slice_free (struct get_node_data, data);
}

static DonnaTaskState
get_node (DonnaTask *task, struct get_node_data *data)
{
    DonnaProviderBase *p;
    DonnaProviderBasePrivate *priv;
    DonnaNode *node;
    DonnaTaskState ret;

    p = (DonnaProviderBase *) data->provider;
    priv = p->priv;

    g_rec_mutex_lock (&priv->nodes_mutex);
    /* first make sure it wasn't created before the task started */
    node = provider_base_get_cached_node (p, data->location);
    if (node)
    {
        g_rec_mutex_unlock (&priv->nodes_mutex);
        return return_node (task, node);
    }

    /* create the node. It is new_node's responsability to call
     * add_node_to_cache */
    ret = DONNA_PROVIDER_BASE_GET_CLASS (p)->new_node (data->provider,
            task, data->location);
    g_rec_mutex_unlock (&priv->nodes_mutex);

    free_get_node_data (data);
    return ret;
}

static DonnaTask *
provider_base_get_node (DonnaProvider    *provider,
                        const gchar      *location,
                        task_callback_fn  callback,
                        gpointer          callback_data,
                        GDestroyNotify    callback_destroy,
                        guint             timeout,
                        task_timeout_fn   timeout_callback,
                        gpointer          timeout_data,
                        GDestroyNotify    timeout_destroy,
                        GError          **error)
{
    DonnaProviderBase *p = (DonnaProviderBase *) provider;
    DonnaProviderBasePrivate *priv;
    DonnaNode *node;
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (p), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    priv = p->priv;
    g_rec_mutex_lock (&priv->nodes_mutex);
    node = provider_base_get_cached_node (p, location);
    if (node)
    {
        task = donna_task_new (NULL /* internal task */,
                (task_fn) return_node, g_object_ref (node), g_object_unref,
                callback, callback_data, callback_destroy,
                timeout, timeout_callback, timeout_data, timeout_destroy);
    }
    else
    {
        struct get_node_data *data;

        data = g_slice_new0 (struct get_node_data);
        data->provider = g_object_ref (provider);
        data->location = g_strdup (location);
        task = donna_task_new (NULL /* internal task */,
                (task_fn) get_node, data, (GDestroyNotify) free_get_node_data,
                callback, callback_data, callback_destroy,
                timeout, timeout_callback, timeout_data, timeout_destroy);
    }
    g_rec_mutex_unlock (&priv->nodes_mutex);

    return task;
}

static DonnaTaskState
get_content (DonnaTask *task, DonnaNode *node)
{
    DonnaProviderBase *provider_base;
    DonnaProvider *provider;
    DonnaTaskState ret;

    donna_node_get (node, "provider", &provider, NULL);
    provider_base = (DonnaProviderBase *) provider;

    g_rec_mutex_lock (&provider_base->priv->nodes_mutex);
    ret = DONNA_PROVIDER_BASE_GET_CLASS (provider)->get_content (provider,
            task, node);
    g_rec_mutex_unlock (&provider_base->priv->nodes_mutex);
    g_object_unref (node);
    g_object_unref (provider);
    return ret;
}

static DonnaTask *
provider_base_get_content (DonnaProvider    *provider,
                           DonnaNode        *node,
                           task_callback_fn  callback,
                           gpointer          callback_data,
                           GDestroyNotify    callback_destroy,
                           guint             timeout,
                           task_timeout_fn   timeout_callback,
                           gpointer          timeout_data,
                           GDestroyNotify    timeout_destroy,
                           GError          **error)
{
    DonnaProviderBase *p = (DonnaProviderBase *) provider;
    DonnaProvider *provider_node;
    gboolean is_container;
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (p), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    donna_node_get (node,
            "provider",     &provider_node,
            "is_container", &is_container,
            NULL);
    /* make sure the provider is the node's provider */
    g_object_unref (provider_node);
    g_return_val_if_fail (provider_node == provider, NULL);
    /* make sure the node is a container */
    g_return_val_if_fail (is_container, NULL);

    task = donna_task_new (NULL /* internal task */,
            (task_fn) get_content, g_object_ref (node), g_object_unref,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy);
    return task;
}

static DonnaTaskState
get_children (DonnaTask *task, DonnaNode *node)
{
    DonnaProviderBase *provider_base;
    DonnaProvider *provider;
    DonnaTaskState ret;

    donna_node_get (node, "provider", &provider, NULL);
    provider_base = (DonnaProviderBase *) provider;

    g_rec_mutex_lock (&provider_base->priv->nodes_mutex);
    ret = DONNA_PROVIDER_BASE_GET_CLASS (provider)->get_children (provider,
            task, node);
    g_rec_mutex_unlock (&provider_base->priv->nodes_mutex);
    g_object_unref (node);
    g_object_unref (provider);
    return ret;
}

static DonnaTask *
provider_base_get_children (DonnaProvider    *provider,
                            DonnaNode        *node,
                            task_callback_fn  callback,
                            gpointer          callback_data,
                            GDestroyNotify    callback_destroy,
                            guint             timeout,
                            task_timeout_fn   timeout_callback,
                            gpointer          timeout_data,
                            GDestroyNotify    timeout_destroy,
                            GError          **error)
{
    DonnaProviderBase *p = (DonnaProviderBase *) provider;
    DonnaProvider *provider_node;
    gboolean is_container;
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (p), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    donna_node_get (node,
            "provider",     &provider_node,
            "is_container", &is_container,
            NULL);
    /* make sure the provider is the node's provider */
    g_object_unref (provider_node);
    g_return_val_if_fail (provider_node == provider, NULL);
    /* make sure the node is a container */
    g_return_val_if_fail (is_container, NULL);

    task = donna_task_new (NULL /* internal task */,
            (task_fn) get_children, g_object_ref (node), g_object_unref,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy);
    return task;
}

static DonnaTaskState
remove_node (DonnaTask *task, DonnaNode *node)
{
    DonnaProviderBase *provider_base;
    DonnaProvider *provider;
    DonnaTaskState ret;

    donna_node_get (node, "provider", &provider, NULL);
    provider_base = (DonnaProviderBase *) provider;

    g_rec_mutex_lock (&provider_base->priv->nodes_mutex);
    ret = DONNA_PROVIDER_BASE_GET_CLASS (provider)->remove_node (provider,
            task, node);
    g_rec_mutex_unlock (&provider_base->priv->nodes_mutex);
    g_object_unref (node);
    g_object_unref (provider);
    return ret;
}

static DonnaTask *
provider_base_remove_node (DonnaProvider    *provider,
                           DonnaNode        *node,
                           task_callback_fn  callback,
                           gpointer          callback_data,
                           GDestroyNotify    callback_destroy,
                           guint             timeout,
                           task_timeout_fn   timeout_callback,
                           gpointer          timeout_data,
                           GDestroyNotify    timeout_destroy,
                           GError          **error)
{
    DonnaProviderBase *p = (DonnaProviderBase *) provider;
    DonnaProvider *provider_node;
    gboolean is_container;
    DonnaTask *task;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (p), NULL);
    g_return_val_if_fail (DONNA_IS_NODE (node), NULL);

    donna_node_get (node,
            "provider",     &provider_node,
            "is_container", &is_container,
            NULL);
    /* make sure the provider is the node's provider */
    g_object_unref (provider_node);
    g_return_val_if_fail (provider_node == provider, NULL);
    /* make sure the node is a container */
    g_return_val_if_fail (is_container, NULL);

    task = donna_task_new (NULL /* internal task */,
            (task_fn) remove_node, g_object_ref (node), g_object_unref,
            callback, callback_data, callback_destroy,
            timeout, timeout_callback, timeout_data, timeout_destroy);
    return task;
}
