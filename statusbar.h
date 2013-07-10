
#ifndef __DONNA_STATUS_BAR_H__
#define __DONNA_STATUS_BAR_H__

#include <gtk/gtk.h>
#include "statusprovider.h"

G_BEGIN_DECLS

typedef struct _DonnaStatusBar          DonnaStatusBar;
typedef struct _DonnaStatusBarPrivate   DonnaStatusBarPrivate;
typedef struct _DonnaStatusBarClass     DonnaStatusBarClass;

#define DONNA_TYPE_STATUS_BAR           (donna_status_bar_get_type ())
#define DONNA_STATUS_BAR(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_STATUS_BAR, DonnaStatusBar))
#define DONNA_STATUS_BAR_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_STATUS_BAR, DonnaStatusBarClass))
#define DONNA_IS_STATUS_BAR(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_STATUS_BAR))
#define DONNA_IS_STATUS_BAR_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_STATUS_BAR))
#define DONNA_STATUS_BAR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_STATUS_BAR, DonnaStatusBarClass))

#define DONNA_STATUS_BAR_ERROR          g_quark_from_static_string ("DonnaStatusBar-Error")
typedef enum
{
    DONNA_STATUS_BAR_ERROR_AREA_ALREADY_EXISTS,
    DONNA_STATUS_BAR_ERROR_AREA_NOT_FOUND,
    DONNA_STATUS_BAR_ERROR_OTHER,
} DonnaStatusBarError;

struct _DonnaStatusBar
{
    /*< private >*/
    GtkWidget widget;
    DonnaStatusBarPrivate *priv;
};

struct _DonnaStatusBarClass
{
    GtkWidgetClass parent_class;
};


GType donna_status_bar_get_type         (void) G_GNUC_CONST;

gboolean        donna_status_bar_add_area           (DonnaStatusBar       *sb,
                                                     const gchar          *name,
                                                     DonnaStatusProvider  *sp,
                                                     guint                 id,
                                                     gint                  nat_width,
                                                     gboolean              expand,
                                                     GError              **error);
gboolean        donna_status_bar_update_area        (DonnaStatusBar       *sb,
                                                     const gchar          *name,
                                                     DonnaStatusProvider  *sp,
                                                     guint                 id,
                                                     GError              **error);
const gchar *   donna_status_bar_get_area_at_pos    (DonnaStatusBar    *sb,
                                                     gint               x,
                                                     gint               y);

G_END_DECLS

#endif /* __DONNA_STATUS_BAR_H__ */
