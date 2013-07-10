
#ifndef __DONNA_STATUS_PROVIDER_H__
#define __DONNA_STATUS_PROVIDER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _DonnaStatusProvider             DonnaStatusProvider; /* dummy typedef */
typedef struct _DonnaStatusProviderInterface    DonnaStatusProviderInterface;

#define DONNA_TYPE_STATUS_PROVIDER              (donna_status_provider_get_type ())
#define DONNA_STATUS_PROVIDER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_STATUS_PROVIDER, DonnaStatusProvider))
#define DONNA_IS_STATUS_PROVIDER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_STATUS_PROVIDER))
#define DONNA_STATUS_PROVIDER_GET_INTERFACE(obj)(G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_STATUS_PROVIDER, DonnaStatusProviderInterface))

#define DONNA_STATUS_PROVIDER_ERROR             g_quark_from_static_string ("DonnaStatusProvider-Error")
typedef enum
{
    DONNA_STATUS_PROVIDER_ERROR_INVALID_CONFIG,
    DONNA_STATUS_PROVIDER_ERROR_OTHER,
} DonnaStatusProviderError;


GType donna_status_provider_get_type            (void) G_GNUC_CONST;

struct _DonnaStatusProviderInterface
{
    GTypeInterface parent;

    /* signals */
    void            (*status_changed)       (DonnaStatusProvider    *sp,
                                             guint                   id);

    /* virtual table */
    guint           (*create_status)        (DonnaStatusProvider    *sp,
                                             gpointer                config,
                                             GError                **error);
    void            (*free_status)          (DonnaStatusProvider    *sp,
                                             guint                   id);
    const gchar *   (*get_renderers)        (DonnaStatusProvider    *sp,
                                             guint                   id);
    void            (*render)               (DonnaStatusProvider    *sp,
                                             guint                   id,
                                             guint                   index,
                                             GtkCellRenderer        *renderer);
    gboolean        (*set_tooltip)          (DonnaStatusProvider    *sp,
                                             guint                   id,
                                             guint                   index,
                                             GtkTooltip             *tooltip);
};

/* signals */
void            donna_status_provider_status_changed(DonnaStatusProvider *sp,
                                                     guint                id);
/* API */
guint           donna_status_provider_create_status (DonnaStatusProvider    *sp,
                                                     gpointer                config,
                                                     GError                **error);
void            donna_status_provider_free_status   (DonnaStatusProvider    *sp,
                                                     guint                   id);
const gchar *   donna_status_provider_get_renderers (DonnaStatusProvider    *sp,
                                                     guint                   id);
void            donna_status_provider_render        (DonnaStatusProvider    *sp,
                                                     guint                   id,
                                                     guint                   index,
                                                     GtkCellRenderer        *renderer);
gboolean        donna_status_provider_set_tooltip   (DonnaStatusProvider    *sp,
                                                     guint                   id,
                                                     guint                   index,
                                                     GtkTooltip             *tooltip);

G_END_DECLS

#endif /* __DONNA_STATUS_PROVIDER_H__ */
