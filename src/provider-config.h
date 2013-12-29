
#ifndef __DONNA_PROVIDER_CONFIG_H__
#define __DONNA_PROVIDER_CONFIG_H__

#include "common.h"

typedef struct _DonnaArrangement    DonnaArrangement;
typedef struct _DonnaApp            DonnaApp;

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

typedef union  _DonnaConfigExtra                DonnaConfigExtra;
typedef struct _DonnaConfigExtraAny             DonnaConfigExtraAny;
typedef struct _DonnaConfigExtraList            DonnaConfigExtraList;
typedef struct _DonnaConfigExtraListInt         DonnaConfigExtraListInt;
/* flags & int have the same struct */
typedef struct _DonnaConfigExtraListInt         DonnaConfigExtraListFlags;

typedef struct _DonnaConfigItemExtraList        DonnaConfigItemExtraList;
typedef struct _DonnaConfigItemExtraListInt     DonnaConfigItemExtraListInt;
typedef struct _DonnaConfigItemExtraListInt     DonnaConfigItemExtraListFlags;

#define DONNA_CONFIG_ERROR      g_quark_from_static_string ("DonnaConfig-Error")
typedef enum
{
    DONNA_CONFIG_ERROR_NOT_FOUND,
    DONNA_CONFIG_ERROR_INVALID_TYPE,
    DONNA_CONFIG_ERROR_INVALID_OPTION_TYPE,
    DONNA_CONFIG_ERROR_INVALID_NAME,
    DONNA_CONFIG_ERROR_ALREADY_EXISTS,
    DONNA_CONFIG_ERROR_OTHER
} DonnaConfigError;

typedef enum
{
    DONNA_CONFIG_OPTION_TYPE_OPTION    = (1 << 0),
    DONNA_CONFIG_OPTION_TYPE_CATEGORY  = (1 << 1),
    DONNA_CONFIG_OPTION_TYPE_NUMBERED  = (1 << 2),
    DONNA_CONFIG_OPTION_TYPE_BOTH      = DONNA_CONFIG_OPTION_TYPE_OPTION |
        DONNA_CONFIG_OPTION_TYPE_CATEGORY,
} DonnaConfigOptionType;

typedef enum
{
    DONNA_CONFIG_EXTRA_TYPE_LIST,
    DONNA_CONFIG_EXTRA_TYPE_LIST_INT,
    DONNA_CONFIG_EXTRA_TYPE_LIST_FLAGS,
} DonnaConfigExtraType;

enum
{
    _DONNA_CONFIG_COLUMN_FROM_ARRANGEMENT = 1,
    _DONNA_CONFIG_COLUMN_FROM_TREE,
    _DONNA_CONFIG_COLUMN_FROM_MODE,
    _DONNA_CONFIG_COLUMN_FROM_DEFAULT,
};

struct _DonnaConfigExtraAny
{
    DonnaConfigExtraType type;
    gchar               *title;
    gint                 nb_items;
    gpointer             items;
};

struct _DonnaConfigItemExtraList
{
    const gchar *value;
    const gchar *label;
};

struct _DonnaConfigExtraList
{
    DonnaConfigExtraType     type;
    gchar                   *title;
    gint                     nb_items;
    DonnaConfigItemExtraList items[];
};

struct _DonnaConfigItemExtraListInt
{
    gint         value;
    const gchar *in_file;
    const gchar *label;
};

struct _DonnaConfigExtraListInt
{
    DonnaConfigExtraType         type;
    gchar                       *title;
    gint                         nb_items;
    DonnaConfigItemExtraListInt items[];
};

union _DonnaConfigExtra
{
    DonnaConfigExtraAny         any;
    DonnaConfigExtraList        list;
    DonnaConfigExtraListInt     list_int;
    DonnaConfigExtraListFlags   list_flags;
};

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
    void            (*option_deleted)           (DonnaConfig            *config,
                                                 const gchar            *name);
};

GType       donna_provider_config_get_type      (void) G_GNUC_CONST;
/* config manager */
gboolean    donna_config_add_extra              (DonnaConfig          *config,
                                                 DonnaConfigExtraType  type,
                                                 const gchar          *name,
                                                 const gchar          *title,
                                                 gint                  nb_items,
                                                 gpointer              items,
                                                 GError              **error);
gboolean    donna_config_load_config            (DonnaConfig            *config,
                                                 gchar                  *data);
gchar *     donna_config_export_config          (DonnaConfig            *config);
const DonnaConfigExtra *
            donna_config_get_extra              (DonnaConfig            *config,
                                                 const gchar            *extra,
                                                 GError                **error);
gboolean    donna_config_has_boolean            (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_int                (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_double             (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_string             (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_category           (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_has_option             (DonnaConfig            *config,
                                                 GError                **error,
                                                 GType                  *type,
                                                 const gchar           **extra_name,
                                                 const DonnaConfigExtra**extra,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_boolean            (DonnaConfig            *config,
                                                 GError                **error,
                                                 gboolean               *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_int                (DonnaConfig            *config,
                                                 GError                **error,
                                                 gint                   *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_double             (DonnaConfig            *config,
                                                 GError                **error,
                                                 gdouble                *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_string             (DonnaConfig            *config,
                                                 GError                **error,
                                                 gchar                 **value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_list_options           (DonnaConfig            *config,
                                                 GPtrArray             **options,
                                                 DonnaConfigOptionType   type,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_get_boolean_column     (DonnaConfig            *config,
                                                 const gchar            *col_name,
                                                 const gchar            *arr_name,
                                                 const gchar            *tv_name,
                                                 gboolean                is_tree,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gboolean                def_val,
                                                 guint                  *from);
gint        donna_config_get_int_column         (DonnaConfig            *config,
                                                 const gchar            *col_name,
                                                 const gchar            *arr_name,
                                                 const gchar            *tv_name,
                                                 gboolean                is_tree,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gint                    def_val,
                                                 guint                  *from);
gdouble     donna_config_get_double_column      (DonnaConfig            *config,
                                                 const gchar            *col_name,
                                                 const gchar            *arr_name,
                                                 const gchar            *tv_name,
                                                 gboolean                is_tree,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 gdouble                 def_val,
                                                 guint                  *from);
gchar *     donna_config_get_string_column      (DonnaConfig            *config,
                                                 const gchar            *col_name,
                                                 const gchar            *arr_name,
                                                 const gchar            *tv_name,
                                                 gboolean                is_tree,
                                                 const gchar            *def_cat,
                                                 const gchar            *opt_name,
                                                 const gchar            *def_val,
                                                 guint                  *from);
gboolean    donna_config_arr_load_columns       (DonnaConfig            *config,
                                                 DonnaArrangement       *arr,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_arr_load_sort          (DonnaConfig            *config,
                                                 DonnaArrangement       *arr,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_arr_load_second_sort   (DonnaConfig            *config,
                                                 DonnaArrangement       *arr,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_arr_load_columns_options (DonnaConfig          *config,
                                                 DonnaArrangement       *arr,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_arr_load_color_filters (DonnaConfig            *config,
                                                 DonnaApp               *app,
                                                 DonnaArrangement       *arr,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_new_boolean            (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **new_node,
                                                 gboolean                value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_new_int                (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **new_node,
                                                 const gchar            *extra,
                                                 gint                    value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_new_double             (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **new_node,
                                                 gdouble                 value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_new_string             (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **new_node,
                                                 const gchar            *extra,
                                                 const gchar            *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_new_string_take        (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **new_node,
                                                 const gchar            *extra,
                                                 gchar                  *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_new_category           (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **new_node,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_boolean            (DonnaConfig            *config,
                                                 GError                **error,
                                                 gboolean                value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_int                (DonnaConfig            *config,
                                                 GError                **error,
                                                 gint                    value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_double             (DonnaConfig            *config,
                                                 GError                **error,
                                                 gdouble                 value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_string             (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_take_string            (DonnaConfig            *config,
                                                 GError                **error,
                                                 gchar                  *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_set_option             (DonnaConfig            *config,
                                                 GError                **error,
                                                 DonnaNode             **node,
                                                 gboolean                create_only,
                                                 gboolean                ask_user,
                                                 const gchar            *type,
                                                 const gchar            *name,
                                                 const gchar            *value,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_rename_option          (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *new_name,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_rename_category        (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *new_name,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_remove_option          (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);
gboolean    donna_config_remove_category        (DonnaConfig            *config,
                                                 GError                **error,
                                                 const gchar            *fmt,
                                                 ...);

G_END_DECLS

#endif /* __DONNA_PROVIDER_CONFIG_H__ */
