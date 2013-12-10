
#ifndef __DONNA_COLUMNTYPE_PROGRESS_H__
#define __DONNA_COLUMNTYPE_PROGRESS_H__

#include "columntype.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMNTYPE_PROGRESS              (donna_column_type_progress_get_type ())
#define DONNA_COLUMNTYPE_PROGRESS(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMNTYPE_PROGRESS, DonnaColumnTypeProgress))
#define DONNA_COLUMNTYPE_PROGRESS_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMNTYPE_PROGRESS, DonnaColumnTypeProgressClass))
#define DONNA_IS_COLUMNTYPE_PROGRESS(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMNTYPE_PROGRESS))
#define DONNA_IS_COLUMNTYPE_PROGRESS_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMNTYPE_PROGRESS))
#define DONNA_COLUMNTYPE_PROGRESS_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMNTYPE_PROGRESS, DonnaColumnTypeProgressClass))

typedef struct _DonnaColumnTypeProgress             DonnaColumnTypeProgress;
typedef struct _DonnaColumnTypeProgressClass        DonnaColumnTypeProgressClass;
typedef struct _DonnaColumnTypeProgressPrivate      DonnaColumnTypeProgressPrivate;

struct _DonnaColumnTypeProgress
{
    GObject parent;

    DonnaColumnTypeProgressPrivate *priv;
};

struct _DonnaColumnTypeProgressClass
{
    GObjectClass parent;
};

GType               donna_column_type_progress_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_PROGRESS_H__ */
