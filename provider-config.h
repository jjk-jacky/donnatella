
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
gchar *     donna_config_export_config          (DonnaProviderConfig *config);
gboolean    donna_config_get_boolean            (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gboolean               *value);
gboolean    donna_config_get_int                (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gint                   *value);
gboolean    donna_config_get_uint               (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 guint                  *value);
gboolean    donna_config_get_double             (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gdouble                *value);
gboolean    donna_config_get_string             (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gchar                 **value);
gboolean    donna_config_set_boolean            (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gboolean                value);
gboolean    donna_config_set_int                (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gint                    value);
gboolean    donna_config_set_uint               (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 guint                   value);
gboolean    donna_config_set_double             (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gdouble                 value);
gboolean    donna_config_set_string             (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gchar                  *value);
gboolean    donna_config_take_string            (DonnaProviderConfig    *config,
                                                 const gchar            *name,
                                                 gchar                  *value);
gboolean    donna_config_remove_option          (DonnaProviderConfig    *config,
                                                 const gchar            *name);
gboolean    donna_config_remove_category        (DonnaProviderConfig    *config,
                                                 const gchar            *name);

G_END_DECLS

#endif /* __DONNA_PROVIDER_CONFIG_H__ */
