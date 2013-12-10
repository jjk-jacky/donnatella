
#ifndef __DONNA_COLOR_FILTER_H__
#define __DONNA_COLOR_FILTER_H__

#include <glib.h>
#include <glib-object.h>
#include "app.h"
#include "node.h"
#include "filter.h"

G_BEGIN_DECLS

typedef struct _DonnaColorFilter            DonnaColorFilter;
typedef struct _DonnaColorFilterPrivate     DonnaColorFilterPrivate;
typedef struct _DonnaColorFilterClass       DonnaColorFilterClass;

#define DONNA_TYPE_COLOR_FILTER             (donna_color_filter_get_type ())
#define DONNA_COLOR_FILTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLOR_FILTER, DonnaColorFilter))
#define DONNA_COLOR_FILTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLOR_FILTER, DonnaColorFilterClass))
#define DONNA_IS_COLOR_FILTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLOR_FILTER))
#define DONNA_IS_COLOR_FILTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_COLOR_FILTER))
#define DONNA_COLOR_FILTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLOR_FILTER, DonnaColorFilterClass))

GType   donna_color_filter_get_type         (void) G_GNUC_CONST;

struct _DonnaColorFilter
{
    /*< private >*/
    GObject parent;
    DonnaColorFilterPrivate *priv;
};

struct _DonnaColorFilterClass
{
    GObjectClass parent_class;
};

gboolean            donna_color_filter_add_prop     (DonnaColorFilter   *cf,
                                                     const gchar        *name_set,
                                                     const gchar        *name,
                                                     const GValue       *value);
gboolean            donna_color_filter_apply_if_match (DonnaColorFilter *cf,
                                                     GObject            *renderer,
                                                     const gchar        *col_name,
                                                     DonnaNode          *node,
                                                     get_ct_data_fn      get_ct_data,
                                                     gpointer            data,
                                                     gboolean           *keep_going,
                                                     GError            **error);

G_END_DECLS

#endif /* __DONNA_COLOR_FILTER_H__ */
