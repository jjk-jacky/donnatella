
#ifndef __DONNA_PROVIDER_CONFIG_H__
#define __DONNA_PROVIDER_CONFIG_H__

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_CONFIG              (donna_provider_config_get_type ())
#define DONNA_PROVIDER_CONFIG(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_CONFIG, DonnaProviderConfig))
#define DONNA_PROVIDER_CONFIG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_CONFIG, DonnaProviderConfigClass))
#define DONNA_IS_PROVIDER_CONFIG(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_CONFIG))
#define DONNA_IS_PROVIDER_CONFIG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_CONFIG))
#define DONNA_PROVIDER_CONFIG_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_CONFIG, DonnaProviderConfigClass))

typedef struct _DonnaProviderConfig             DonnaProviderConfig;
typedef struct _DonnaProviderConfigClass        DonnaProviderConfigClass;
typedef struct _DonnaProviderConfigPrivate      DonnaProviderConfigPrivate;

struct _DonnaProviderConfig
{
    GObject parent;

    DonnaProviderConfigPrivate *priv;
};

struct _DonnaProviderConfigClass
{
    GObjectClass parent;
};

GType       donna_provider_config_get_type      (void) G_GNUC_CONST;
/* config manager */
gboolean    donna_config_load_config_def        (DonnaProviderConfig    *config,
                                                 gchar                  *data);
gboolean    donna_config_load_config            (DonnaProviderConfig    *config,
                                                 gchar                  *data);

G_END_DECLS

#endif /* __DONNA_PROVIDER_CONFIG_H__ */
