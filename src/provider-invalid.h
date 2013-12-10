
#ifndef __DONNA_PROVIDER_INVALID_H__
#define __DONNA_PROVIDER_INVALID_H__

#include "common.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_INVALID             (donna_provider_invalid_get_type ())
#define DONNA_PROVIDER_INVALID(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_INVALID, DonnaProviderInvalid))
#define DONNA_PROVIDER_INVALID_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_INVALID, DonnaProviderInvalidClass))
#define DONNA_IS_PROVIDER_INVALID(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_INVALID))
#define DONNA_IS_PROVIDER_INVALID_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_INVALID))
#define DONNA_PROVIDER_INVALID_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_INVALID, DonnaProviderInvalidClass))

typedef struct _DonnaProviderInvalid            DonnaProviderInvalid;
typedef struct _DonnaProviderInvalidClass       DonnaProviderInvalidClass;
typedef struct _DonnaProviderInvalidPrivate     DonnaProviderInvalidPrivate;

struct _DonnaProviderInvalid
{
    GObject parent;

    DonnaProviderInvalidPrivate *priv;
};

struct _DonnaProviderInvalidClass
{
    GObjectClass parent;
};

GType           donna_provider_invalid_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_INVALID_H__ */
