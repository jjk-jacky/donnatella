
#ifndef __DONNA_PROVIDER_BASE_H__
#define __DONNA_PROVIDER_BASE_H__

#include "common.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_BASE            (donna_provider_base_get_type ())
#define DONNA_PROVIDER_BASE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_BASE, DonnaProviderBase))
#define DONNA_PROVIDER_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_BASE, BonnaProviderBaseClass))
#define DONNA_IS_PROVIDER_BASE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_BASE))
#define DONNA_IS_PROVIDER_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_BASE))
#define DONNA_PROVIDER_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_BASE, DonnaProviderBaseClass))

typedef struct _DonnaProviderBase           DonnaProviderBase;
typedef struct _DonnaProviderBaseClass      DonnaProviderBaseClass;
typedef struct _DonnaProviderBasePrivate    DonnaProviderBasePrivate;

struct _DonnaProviderBase
{
    GObject parent;

    DonnaProviderBasePrivate *priv;
};

struct _DonnaProviderBaseClass
{
    GObjectClass parent;

    /* virtual table */
    DonnaNode *         (*new_node)         (DonnaProvider   *provider,
                                             const gchar     *location,
                                             GError         **error);
};

GType           donna_provider_base_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_BASE_H__ */
