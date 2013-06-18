
#ifndef __DONNA_FILTER_H__
#define __DONNA_FILTER_H__

#include <glib.h>
#include <glib-object.h>
#include "node.h"

G_BEGIN_DECLS

#define DONNA_FILTER_ERROR          g_quark_from_static_string ("DonnaFilter-Error")
typedef enum
{
    DONNA_FILTER_ERROR_INVALID_COLUMNTYPE,
    DONNA_FILTER_ERROR_INVALID_SYNTAX,
} DonnaFilterError;

typedef struct _DonnaFilter         DonnaFilter;
typedef struct _DonnaFilterPrivate  DonnaFilterPrivate;
typedef struct _DonnaFilterClass    DonnaFilterClass;

#define DONNA_TYPE_FILTER           (donna_filter_get_type ())
#define DONNA_FILTER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_FILTER, DonnaFilter))
#define DONNA_FILTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_FILTER, DonnaFilterClass))
#define DONNA_IS_FILTER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_FILTER))
#define DONNA_IS_FILTER_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_FILTER))
#define DONNA_FILTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_FILTER, DonnaFilterClass))

GType   donna_filter_get_type       (void) G_GNUC_CONST;

typedef gpointer (*get_ct_data_fn)  (const gchar *col_name, gpointer data);

struct _DonnaFilter
{
    /*< private >*/
    GObject parent;
    DonnaFilterPrivate *priv;
};

struct _DonnaFilterClass
{
    GObjectClass parent_class;
};

gchar *             donna_filter_get_filter         (DonnaFilter    *filter);
gboolean            donna_filter_is_match           (DonnaFilter    *filter,
                                                     DonnaNode      *node,
                                                     get_ct_data_fn  get_ct_data,
                                                     gpointer        data,
                                                     GError        **error);

G_END_DECLS

#endif /* __DONNA_FILTER_H__ */
