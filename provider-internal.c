
#include <gtk/gtk.h>
#include "provider-internal.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "macros.h"
#include "debug.h"


struct _DonnaProviderInternalPrivate
{
    guint last;
};


/* DonnaProvider */
static const gchar *    provider_internal_get_domain    (DonnaProvider      *provider);
static DonnaProviderFlags provider_internal_get_flags   (DonnaProvider      *provider);
/* DonnaProviderBase */
static DonnaTaskState   provider_internal_new_node      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static void             provider_internal_unref_node    (DonnaProviderBase  *provider,
                                                         DonnaNode          *node);
static DonnaTaskState   provider_internal_trigger_node  (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         DonnaNode          *node);

static void
provider_internal_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain   = provider_internal_get_domain;
    interface->get_flags    = provider_internal_get_flags;
}

static void
donna_provider_internal_class_init (DonnaProviderInternalClass *klass)
{
    DonnaProviderBaseClass *pb_class;

    pb_class = (DonnaProviderBaseClass *) klass;
    pb_class->new_node      = provider_internal_new_node;
    pb_class->unref_node    = provider_internal_unref_node;
    pb_class->trigger_node  = provider_internal_trigger_node;

    g_type_class_add_private (klass, sizeof (DonnaProviderInternalPrivate));
}

static void
donna_provider_internal_init (DonnaProviderInternal *provider)
{
    provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_INTERNAL,
            DonnaProviderInternalPrivate);
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderInternal, donna_provider_internal,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_internal_provider_init)
        )


/* DonnaProvider */

static const gchar *
provider_internal_get_domain (DonnaProvider      *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_INTERNAL (provider), NULL);
    return "internal";
}

static DonnaProviderFlags
provider_internal_get_flags (DonnaProvider      *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_INTERNAL (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}


/* DonnaProviderBase */

static DonnaTaskState
provider_internal_new_node (DonnaProviderBase  *provider,
                            DonnaTask          *task,
                            const gchar        *location)
{
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
            "Provider 'internal': Location '%s' doesn't exist", location);
    return DONNA_TASK_FAILED;
}

static void
provider_internal_unref_node (DonnaProviderBase  *_provider,
                              DonnaNode          *node)
{
    GValue v_destroy = G_VALUE_INIT;
    GValue v_data = G_VALUE_INIT;
    DonnaNodeHasValue has;
    GDestroyNotify destroy;

    donna_node_get (node, FALSE,
            "_internal_data",    &has, &v_data,
            "_internal_destroy", &has, &v_destroy,
            NULL);

    destroy = g_value_get_pointer (&v_destroy);
    if (destroy)
        destroy (g_value_get_pointer (&v_data));

    g_value_unset (&v_data);
    g_value_unset (&v_destroy);
}

static DonnaTaskState
provider_internal_trigger_node (DonnaProviderBase  *provider,
                                DonnaTask          *task,
                                DonnaNode          *node)
{
    GValue v_worker = G_VALUE_INIT;
    GValue v_data = G_VALUE_INIT;
    DonnaNodeHasValue has;
    internal_worker_fn worker;
    DonnaTaskState ret;

    donna_node_get (node, FALSE,
            "_internal_worker",  &has, &v_worker,
            "_internal_data",    &has, &v_data,
            NULL);

    worker = g_value_get_pointer (&v_worker);
    g_value_unset (&v_worker);
    /* no worker means node was already triggered/it already ran */
    if (G_UNLIKELY (!worker))
    {
        gchar *location = donna_node_get_location (node);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_CALL,
                "Provider 'internal': Node '%s' has already been triggered",
                location);
        g_free (location);
        g_value_unset (&v_data);
        return DONNA_TASK_FAILED;
    }

    ret = worker (task, node, g_value_get_pointer (&v_data));

    /* mark things as done */
    g_value_set_pointer (&v_data, NULL);
    donna_node_set_property_value (node, "_internal_worker", &v_data);
    donna_node_set_property_value (node, "_internal_destroy", &v_data);
    g_value_unset (&v_data);

    return ret;
}

DonnaNode *
donna_provider_internal_new_node (DonnaProviderInternal  *pi,
                                  const gchar            *name,
                                  const GdkPixbuf        *icon,
                                  const gchar            *desc,
                                  internal_worker_fn      worker,
                                  gpointer                data,
                                  GDestroyNotify          destroy,
                                  GError                **error)
{
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *pb;
    DonnaNodeFlags flags = 0;
    DonnaNode *node;
    gchar location[64];
    GValue v = G_VALUE_INIT;

    g_return_val_if_fail (DONNA_IS_PROVIDER_INTERNAL (pi), NULL);
    g_return_val_if_fail (worker != NULL, NULL);

    if (icon)
        flags |= DONNA_NODE_ICON_EXISTS;
    if (desc)
        flags |= DONNA_NODE_DESC_EXISTS;
    snprintf (location, 64, "%u", (guint) g_atomic_int_add (&pi->priv->last, 1) + 1);

    node = donna_node_new ((DonnaProvider *) pi, location, DONNA_NODE_ITEM,
            NULL, (refresher_fn) gtk_true, NULL, name, flags);
    if (G_UNLIKELY (!node))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR, DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'internal': Unable to create a new node");
        return NULL;
    }

    if (icon)
    {
        g_value_init (&v, GDK_TYPE_PIXBUF);
        g_value_set_object (&v, (GObject *) icon);
        donna_node_set_property_value (node, "icon", &v);
        g_value_unset (&v);
    }

    if (desc)
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, desc);
        donna_node_set_property_value (node, "desc", &v);
        g_value_unset (&v);
    }

    g_value_init (&v, G_TYPE_POINTER);
    g_value_set_pointer (&v, worker);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_worker",
                    G_TYPE_POINTER, &v, (refresher_fn) gtk_true, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_worker': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    g_value_set_pointer (&v, data);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_data",
                    G_TYPE_POINTER, &v, (refresher_fn) gtk_true, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_data': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    g_value_set_pointer (&v, destroy);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_destroy",
                    G_TYPE_POINTER, &v, (refresher_fn) gtk_true, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_destroy': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    pb = (DonnaProviderBase *) pi;
    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);
    klass->lock_nodes (pb);
    klass->add_node_to_cache (pb, node);
    klass->unlock_nodes (pb);

    return node;
}
