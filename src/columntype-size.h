
#ifndef __DONNA_COLUMN_TYPE_SIZE_H__
#define __DONNA_COLUMN_TYPE_SIZE_H__

#include "columntype.h"
#include "donna.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMN_TYPE_SIZE             (donna_column_type_size_get_type ())
#define DONNA_COLUMN_TYPE_SIZE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMN_TYPE_SIZE, DonnaColumnTypeSize))
#define DONNA_COLUMN_TYPE_SIZE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMN_TYPE_SIZE, DonnaColumnTypeSizeClass))
#define DONNA_IS_COLUMN_TYPE_SIZE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMN_TYPE_SIZE))
#define DONNA_IS_COLUMN_TYPE_SIZE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMN_TYPE_SIZE))
#define DONNA_COLUMN_TYPE_SIZE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMN_TYPE_SIZE, DonnaColumnTypeSizeClass))

typedef struct _DonnaColumnTypeSize             DonnaColumnTypeSize;
typedef struct _DonnaColumnTypeSizeClass        DonnaColumnTypeSizeClass;
typedef struct _DonnaColumnTypeSizePrivate      DonnaColumnTypeSizePrivate;

struct _DonnaColumnTypeSize
{
    GObject parent;

    DonnaColumnTypeSizePrivate *priv;
};

struct _DonnaColumnTypeSizeClass
{
    GObjectClass parent;
};

GType                   donna_column_type_size_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COLUMN_TYPE_SIZE_H__ */
