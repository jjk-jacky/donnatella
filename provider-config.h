
#ifndef __DONNA_PROVIDER_CONFIG_H__
#define __DONNA_PROVIDER_CONFIG_H__

#include "sharedstring.h"

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


#define DONNA_CONFIG(obj)       ((DonnaConfig *) (obj))
#define DONNA_IS_CONFIG(obj)    DONNA_IS_PROVIDER_CONFIG(obj)

typedef DonnaProviderConfig     DonnaConfig;


struct _DonnaProviderConfig
{
    GObject parent;

    DonnaProviderConfigPrivate *priv;
};

struct _DonnaProviderConfigClass
{
    GObjectClass parent;

    /* signals -- config manager */
    void            (*option_set)               (DonnaConfig            *config,
                                                 const gchar            *name);
    void            (*option_removed)           (DonnaConfig            *config,
                                                 const gchar            *name);
};

GType       donna_provider_config_get_type      (void) G_GNUC_CONST;
/* config manager */
gboolean    donna_config_load_config_def        (DonnaConfig            *config,
                                                 gchar                  *data);
gboolean    donna_config_load_config            (DonnaConfig            *config,
                                                 gchar                  *data);
gchar *     donna_config_export_config          (DonnaConfig            *config);
gboolean    donna_config_get_boolean            (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gboolean               *value);
gboolean    donna_config_get_int                (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gint                   *value);
gboolean    donna_config_get_uint               (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 guint                  *value);
gboolean    donna_config_get_double             (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gdouble                *value);
gboolean    donna_config_get_shared_string      (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 DonnaSharedString     **value);
gboolean    donna_config_set_boolean            (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gboolean                value);
gboolean    donna_config_set_int                (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gint                    value);
gboolean    donna_config_set_uint               (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 guint                   value);
gboolean    donna_config_set_double             (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gdouble                 value);
gboolean    donna_config_set_shared_string      (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 DonnaSharedString      *value);
gboolean    donna_config_take_shared_string     (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 DonnaSharedString      *value);
gboolean    donna_config_set_string_take        (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 gchar                  *value);
gboolean    donna_config_set_string_dup         (DonnaConfig            *config,
                                                 const gchar            *name,
                                                 const gchar            *value);
gboolean    donna_config_remove_option          (DonnaConfig            *config,
                                                 const gchar            *name);
gboolean    donna_config_remove_category        (DonnaConfig            *config,
                                                 const gchar            *name);

G_END_DECLS

#endif /* __DONNA_PROVIDER_CONFIG_H__ */
