
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


#define DONNA_CONFIG(obj)       ((DonnaConfig *) (obj))
#define DONNA_IS_CONFIG(obj)    DONNA_IS_PROVIDER_CONFIG(obj)

typedef DonnaProviderConfig     DonnaConfig;

typedef enum
{
    DONNA_CONFIG_OPTION_TYPE_OPTION    = (1 << 0),
    DONNA_CONFIG_OPTION_TYPE_CATEGORY  = (1 << 1),
    DONNA_CONFIG_OPTION_TYPE_BOTH      = DONNA_CONFIG_OPTION_TYPE_OPTION &
        DONNA_CONFIG_OPTION_TYPE_CATEGORY,
} DonnaConfigOptionType;

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
gboolean    donna_config_has_boolean            (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_int                (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_uint               (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_double             (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_string             (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_category           (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_boolean            (DonnaConfig            *config,
                                                 gboolean               *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_int                (DonnaConfig            *config,
                                                 gint                   *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_uint               (DonnaConfig            *config,
                                                 guint                  *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_double             (DonnaConfig            *config,
                                                 gdouble                *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_string             (DonnaConfig            *config,
                                                 gchar                 **value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_list_options           (DonnaConfig            *config,
                                                 GPtrArray             **options,
                                                 DonnaConfigOptionType   type,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_boolean_column     (DonnaConfig            *config,
                                                 const gchar            *tv_name,
                                                 const gchar            *col_name,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gboolean                def_val);
gint        donna_config_get_int_column         (DonnaConfig            *config,
                                                 const gchar            *tv_name,
                                                 const gchar            *col_name,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gint                    def_val);
guint       donna_config_get_uint_column        (DonnaConfig            *config,
                                                 const gchar            *tv_name,
                                                 const gchar            *col_name,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 guint                   def_val);
gdouble     donna_config_get_double_column      (DonnaConfig            *config,
                                                 const gchar            *tv_name,
                                                 const gchar            *col_name,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gdouble                 def_val);
gchar *     donna_config_get_string_column      (DonnaConfig            *config,
                                                 const gchar            *tv_name,
                                                 const gchar            *col_name,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gchar                  *def_val);
gboolean    donna_config_set_boolean            (DonnaConfig            *config,
                                                 gboolean                value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_int                (DonnaConfig            *config,
                                                 gint                    value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_uint               (DonnaConfig            *config,
                                                 guint                   value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_double             (DonnaConfig            *config,
                                                 gdouble                 value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_string             (DonnaConfig            *config,
                                                 const gchar            *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_take_string            (DonnaConfig            *config,
                                                 gchar                  *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_remove_option          (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_remove_category        (DonnaConfig            *config,
                                                 const gchar            *fmt,
                                                 ...);

G_END_DECLS

#endif /* __DONNA_PROVIDER_CONFIG_H__ */
