
#ifndef __DONNA_COLUMN_TYPE_TIME_H__
#define __DONNA_COLUMN_TYPE_TIME_H__

#include "columntype.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMN_TYPE_TIME             (donna_column_type_time_get_type ())
#define DONNA_COLUMN_TYPE_TIME(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMN_TYPE_TIME, DonnaColumnTypeTime))
#define DONNA_COLUMN_TYPE_TIME_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMN_TYPE_TIME, DonnaColumnTypeTimeClass))
#define DONNA_IS_COLUMN_TYPE_TIME(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMN_TYPE_TIME))
#define DONNA_IS_COLUMN_TYPE_TIME_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMN_TYPE_TIME))
#define DONNA_COLUMN_TYPE_TIME_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMN_TYPE_TIME, DonnaColumnTypeTimeClass))

typedef struct _DonnaColumnTypeTime             DonnaColumnTypeTime;
typedef struct _DonnaColumnTypeTimeClass        DonnaColumnTypeTimeClass;
typedef struct _DonnaColumnTypeTimePrivate      DonnaColumnTypeTimePrivate;

struct _DonnaColumnTypeTime
{
    GObject parent;

    DonnaColumnTypeTimePrivate *priv;
};

struct _DonnaColumnTypeTimeClass
{
    GObjectClass parent;
};

GType                   donna_column_type_time_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COLUMN_TYPE_TIME_H__ */
