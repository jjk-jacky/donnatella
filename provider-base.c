
#include <glib-object.h>
#include "provider-base.h"
#include "provider.h"

struct _DonnaProviderBasePrivate
{
    GHashTable  *nodes;
    GRWLock      nodes_lock;
};

static void             provider_base_finalize      (GObject *object);

/* DonnaProvider */
static DonnaTask *      provider_base_get_node      (DonnaProvider  *provider,
                                                     const gchar    *location,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError        **error);
static DonnaTask *      provider_base_get_content   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError       **error);
static DonnaTask *      provider_base_get_children  (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError        **error);
static DonnaTask *      provider_base_remove_node   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError        **error);

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

    o_class = (GObjectClass *) klass;
    /* TODO: implement property "domain" */
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
    g_rw_lock_init (&priv->nodes_lock);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderBase, provider_base, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_base_provider_init))

static void
provider_base_finalize (GObject *object)
{
    DonnaProviderBasePrivate *priv;

    priv = DONNA_PROVIDER_BASE (object)->priv;

    g_hash_table_destroy (priv->nodes);
    g_rw_lock_clear (&priv->nodes_lock);

    /* chain up */
    G_OBJECT_CLASS (provider_base_parent_class)->finalize (object);
}

static DonnaTask *
provider_base_get_node (DonnaProvider    *provider,
                        const gchar      *location,
                        GCallback         callback,
                        gpointer          cb_data,
                        GError          **error)
{
    DonnaProviderBase *p = (DonnaProviderBase *) provider;
    DonnaProviderBasePrivate *priv;
    DonnaNode *node;

    g_return_val_if_fail (DONNA_IS_PROVIDER_BASE (p), NULL);
    g_return_val_if_fail (location != NULL, NULL);

    priv = p->priv;
    g_rw_lock_reader_lock (&priv->nodes_lock);
    node = g_hash_table_lookup (priv->nodes, location);
    if (node)
    {
        /* add a reference for our caller */
        g_object_ref (node);
        g_rw_lock_reader_unlock (&priv->nodes_lock);
        return node;
    }


}

static DonnaTask *      provider_base_get_content   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError       **error);
static DonnaTask *      provider_base_get_children  (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError        **error);
static DonnaTask *      provider_base_remove_node   (DonnaProvider  *provider,
                                                     DonnaNode      *node,
                                                     GCallback       callback,
                                                     gpointer        cb_data,
                                                     GError        **error);
