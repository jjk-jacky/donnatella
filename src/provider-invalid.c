
#include "config.h"

#include <glib-object.h>
#include "provider-invalid.h"
#include "provider.h"

enum
{
    PROP_0,

    PROP_APP,

    NB_PROPS
};

struct _DonnaProviderInvalidPrivate
{
    DonnaApp *app;
};

static void             provider_invalid_set_property   (GObject        *object,
                                                         guint           prop_id,
                                                         const GValue   *value,
                                                         GParamSpec     *pspec);
static void             provider_invalid_get_property   (GObject        *object,
                                                         guint           prop_id,
                                                         GValue         *value,
                                                         GParamSpec     *pspec);
static void             provider_invalid_finalize       (GObject        *object);

/* DonnaProvider */
static const gchar *        provider_invalid_get_domain (
                                            DonnaProvider       *provider);
static DonnaProviderFlags   provider_invalid_get_flags (
                                            DonnaProvider       *provider);
static gboolean             provider_invalid_get_node (
                                            DonnaProvider       *provider,
                                            const gchar         *location,
                                            gboolean            *is_node,
                                            gpointer            *ret,
                                            GError             **error);
static void                 provider_invalid_unref_node (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node);
static DonnaTask *          provider_invalid_has_get_node_children_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            DonnaNodeType        node_types,
                                            GError             **error);
static DonnaTask *          provider_invalid_trigger_node_task (
                                            DonnaProvider       *provider,
                                            DonnaNode           *node,
                                            GError             **error);

static void
provider_invalid_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain             = provider_invalid_get_domain;
    interface->get_flags              = provider_invalid_get_flags;
    interface->get_node               = provider_invalid_get_node;
    interface->unref_node             = provider_invalid_unref_node;
    interface->has_node_children_task = provider_invalid_has_get_node_children_task;
    interface->get_node_children_task = provider_invalid_has_get_node_children_task;
    interface->trigger_node_task      = provider_invalid_trigger_node_task;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderInvalid, donna_provider_invalid,
        G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_invalid_provider_init)
        )

static void
donna_provider_invalid_class_init (DonnaProviderInvalidClass *klass)
{
    GObjectClass *o_class;

    o_class = (GObjectClass *) klass;
    o_class->set_property   = provider_invalid_set_property;
    o_class->get_property   = provider_invalid_get_property;
    o_class->finalize       = provider_invalid_finalize;

    g_object_class_override_property (o_class, PROP_APP, "app");

    g_type_class_add_private (klass, sizeof (DonnaProviderInvalidPrivate));
}

static void
donna_provider_invalid_init (DonnaProviderInvalid *provider)
{
    provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_INVALID,
            DonnaProviderInvalidPrivate);
}

static void
provider_invalid_finalize (GObject *object)
{
    DonnaProviderInvalidPrivate *priv;

    priv = DONNA_PROVIDER_INVALID (object)->priv;
    g_object_unref (priv->app);

    /* chain up */
    G_OBJECT_CLASS (donna_provider_invalid_parent_class)->finalize (object);
}

static void
provider_invalid_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        ((DonnaProviderInvalid *) object)->priv->app = g_value_dup_object (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
provider_invalid_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
    if (G_LIKELY (prop_id == PROP_APP))
        g_value_set_object (value, ((DonnaProviderInvalid *) object)->priv->app);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static const gchar *
provider_invalid_get_domain (DonnaProvider       *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_INVALID (provider), NULL);
    return "invalid";
}

static DonnaProviderFlags
provider_invalid_get_flags (DonnaProvider       *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_INVALID (provider), DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}

static gboolean
provider_invalid_get_node (DonnaProvider       *provider,
                           const gchar         *location,
                           gboolean            *is_node,
                           gpointer            *ret,
                           GError             **error)
{
    g_set_error (error, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
            "Provider 'invalid': Operation not supported");
    return FALSE;
}

static void
provider_invalid_unref_node (DonnaProvider       *provider,
                             DonnaNode           *node)
{
    g_warn_if_reached ();
}

static DonnaTask *
provider_invalid_has_get_node_children_task (DonnaProvider       *provider,
                                             DonnaNode           *node,
                                             DonnaNodeType        node_types,
                                             GError             **error)
{
    g_set_error (error, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
            "Provider 'invalid': Operation not supported");
    return NULL;
}

static DonnaTask *
provider_invalid_trigger_node_task (DonnaProvider       *provider,
                                    DonnaNode           *node,
                                    GError             **error)
{
    g_set_error (error, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_NOT_SUPPORTED,
            "Provider 'invalid': Operation not supported");
    return NULL;
}
