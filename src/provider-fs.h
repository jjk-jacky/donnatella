
#ifndef __DONNA_PROVIDER_FS_H__
#define __DONNA_PROVIDER_FS_H__

#include "provider-base.h"

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
    DonnaProviderBase parent;

    DonnaProviderFsPrivate *priv;
};

struct _DonnaProviderFsClass
{
    DonnaProviderBaseClass parent;
};

typedef gchar *     (*fs_parse_cmdline)             (const gchar        *cmdline,
                                                     GPtrArray          *sources,
                                                     DonnaNode          *dest,
                                                     GError            **error);
typedef void        (*fs_file_created)              (DonnaProviderFs    *pfs,
                                                     const gchar        *location);
typedef void        (*fs_file_deleted)              (DonnaProviderFs    *pfs,
                                                     const gchar        *location);

typedef DonnaTask * (*fs_engine_io_task)            (DonnaProviderFs    *pfs,
                                                     DonnaApp           *app,
                                                     DonnaIoType         type,
                                                     GPtrArray          *sources,
                                                     DonnaNode          *dest,
                                                     const gchar        *new_name,
                                                     fs_parse_cmdline    parser,
                                                     fs_file_created     created,
                                                     fs_file_deleted     deleted,
                                                     GError            **error);

GType               donna_provider_fs_get_type      (void) G_GNUC_CONST;

gboolean            donna_provider_fs_add_io_engine (DonnaProviderFs    *pfs,
                                                     const gchar        *name,
                                                     fs_engine_io_task   engine,
                                                     GError            **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_FS_H__ */
