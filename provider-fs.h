
#ifndef __DONNA_PROVIDER_FS_H__
#define __DONNA_PROVIDER_FS_H__

#include "common.h"
#include "task.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_FS              (donna_provider_fs_get_type ())
#define DONNA_PROVIDER_FS(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_FS, DonnaProviderFs))
#define DONNA_PROVIDER_FS_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_FS, BonnaProviderFsClass))
#define DONNA_IS_PROVIDER_FS(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_FS))
#define DONNA_IS_PROVIDER_FS_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_FS))
#define DONNA_PROVIDER_FS_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_FS, DonnaProviderFsClass))

typedef struct _DonnaProviderFs             DonnaProviderFs;
typedef struct _DonnaProviderFsClass        DonnaProviderFsClass;
typedef struct _DonnaProviderFsPrivate      DonnaProviderFsPrivate;

struct _DonnaProviderFs
{
    GObject parent;

    DonnaProviderFsPrivate *priv;
};

struct _DonnaProviderFsClass
{
    GObjectClass parent;
};

GType           donna_provider_fs_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_FS_H__ */
