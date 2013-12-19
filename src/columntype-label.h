
#ifndef __DONNA_COLUMNTYPE_LABEL_H__
#define __DONNA_COLUMNTYPE_LABEL_H__

#include "columntype.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMNTYPE_LABEL             (donna_column_type_label_get_type ())
#define DONNA_COLUMNTYPE_LABEL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMNTYPE_LABEL, DonnaColumnTypeLabel))
#define DONNA_COLUMNTYPE_LABEL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMNTYPE_LABEL, DonnaColumnTypeLabelClass))
#define DONNA_IS_COLUMNTYPE_LABEL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMNTYPE_LABEL))
#define DONNA_IS_COLUMNTYPE_LABEL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMNTYPE_LABEL))
#define DONNA_COLUMNTYPE_LABEL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMNTYPE_LABEL, DonnaColumnTypeLabelClass))

typedef struct _DonnaColumnTypeLabel            DonnaColumnTypeLabel;
typedef struct _DonnaColumnTypeLabelClass       DonnaColumnTypeLabelClass;
typedef struct _DonnaColumnTypeLabelPrivate     DonnaColumnTypeLabelPrivate;

struct _DonnaColumnTypeLabel
{
    GObject parent;

    DonnaColumnTypeLabelPrivate *priv;
};

struct _DonnaColumnTypeLabelClass
{
    GObjectClass parent;
};

GType               donna_column_type_label_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_LABEL_H__ */