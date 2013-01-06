
#ifndef __FSPROVIDER_H__
#define __FSPROVIDER_H__

G_BEGIN_DECLS

#define TYPE_FSPROVIDER             (fsprovider_get_type ())
#define FSPROVIDER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FSPROVIDER, FsProvider))
#define FSPROVIDER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FSPROVIDER, FsProviderClass))
#define IS_FSPROVIDER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FSPROVIDER))
#define IS_FSPROVIDER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FSPROVIDER))
#define FSPROVIDER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FSPROVIDER, FsProviderClass))

typedef struct _FsProvider          FsProvider;
typedef struct _FsProviderClass     FsProviderClass;
typedef struct _FsProviderPrivate   FsProviderPrivate;

struct _FsProvider
{
    GObject parent;

    FsProviderPrivate *priv;
};

struct _FsProviderClass
{
    GObjectClass parent;
};

GType           fsprovider_get_type     (void) G_GNUC_CONST;

FsProvider *    fsprovider_new          (void);

G_END_DECLS

#endif /* __FSPROVIDER_H__ */
