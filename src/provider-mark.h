
#ifndef __DONNA_PROVIDER_MARK_H__
#define __DONNA_PROVIDER_MARK_H__

#include "provider-base.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_MARK            (donna_provider_mark_get_type ())
#define DONNA_PROVIDER_MARK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_MARK, DonnaProviderMark))
#define DONNA_PROVIDER_MARK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_MARK, BonnaProviderMarkClass))
#define DONNA_IS_PROVIDER_MARK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_MARK))
#define DONNA_IS_PROVIDER_MARK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_MARK))
#define DONNA_PROVIDER_MARK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_MARK, DonnaProviderMarkClass))

typedef struct _DonnaProviderMark           DonnaProviderMark;
typedef struct _DonnaProviderMarkClass      DonnaProviderMarkClass;
typedef struct _DonnaProviderMarkPrivate    DonnaProviderMarkPrivate;

typedef enum
{
    DONNA_MARK_STANDARD,
    DONNA_MARK_DYNAMIC
} DonnaMarkType;

struct _DonnaProviderMark
{
    DonnaProviderBase parent;

    DonnaProviderMarkPrivate *priv;
};

struct _DonnaProviderMarkClass
{
    DonnaProviderBaseClass parent;
};

GType       donna_provider_mark_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_MARK_H__ */
