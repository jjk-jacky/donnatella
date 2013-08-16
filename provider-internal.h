
#ifndef __DONNA_PROVIDER_INTERNAL_H__
#define __DONNA_PROVIDER_INTERNAL_H__

#include "provider-base.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_INTERNAL            (donna_provider_internal_get_type ())
#define DONNA_PROVIDER_INTERNAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_INTERNAL, DonnaProviderInternal))
#define DONNA_PROVIDER_INTERNAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_INTERNAL, BonnaProviderInternalClass))
#define DONNA_IS_PROVIDER_INTERNAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_INTERNAL))
#define DONNA_IS_PROVIDER_INTERNAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_INTERNAL))
#define DONNA_PROVIDER_INTERNAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_INTERNAL, DonnaProviderInternalClass))

typedef struct _DonnaProviderInternal           DonnaProviderInternal;
typedef struct _DonnaProviderInternalClass      DonnaProviderInternalClass;
typedef struct _DonnaProviderInternalPrivate    DonnaProviderInternalPrivate;

#define DONNA_PROVIDER_INTERNAL_ERROR           g_quark_from_static_string ("DonnaProviderInternal-Error")
typedef enum
{
    DONNA_PROVIDER_INTERNAL_ERROR_OTHER,
} DonnaProviderInternalError;

typedef DonnaTaskState (*internal_worker_fn)    (DonnaTask *task,
                                                 DonnaNode *node,
                                                 gpointer   data);

struct _DonnaProviderInternal
{
    DonnaProviderBase parent;

    DonnaProviderInternalPrivate *priv;
};

struct _DonnaProviderInternalClass
{
    DonnaProviderBaseClass parent;
};

GType       donna_provider_internal_get_type    (void) G_GNUC_CONST;

DonnaNode * donna_provider_internal_new_node    (DonnaProviderInternal  *pi,
                                                 const gchar            *name,
                                                 const GdkPixbuf        *icon,
                                                 const gchar            *desc,
                                                 internal_worker_fn      worker,
                                                 gpointer                data,
                                                 GDestroyNotify          destroy,
                                                 GError                **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_INTERNAL_H__ */
