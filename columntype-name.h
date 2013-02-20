
#ifndef __DONNA_COLUMNTYPE_NAME_H__
#define __DONNA_COLUMNTYPE_NAME_H__

#include "columntype.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMNTYPE_NAME              (donna_column_type_name_get_type ())
#define DONNA_COLUMNTYPE_NAME(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMNTYPE_NAME, DonnaColumnTypeName))
#define DONNA_COLUMNTYPE_NAME_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMNTYPE_NAME, DonnaColumnTypeNameClass))
#define DONNA_IS_COLUMNTYPE_NAME(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMNTYPE_NAME))
#define DONNA_IS_COLUMNTYPE_NAME_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMNTYPE_NAME))
#define DONNA_COLUMNTYPE_NAME_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMNTYPE_NAME, DonnaColumnTypeNameClass))

typedef struct _DonnaColumnTypeName             DonnaColumnTypeName;
typedef struct _DonnaColumnTypeNameClass        DonnaColumnTypeNameClass;
typedef struct _DonnaColumnTypeNamePrivate      DonnaColumnTypeNamePrivate;

struct _DonnaColumnTypeName
{
    GObject parent;

    DonnaColumnTypeNamePrivate *priv;
};

struct _DonnaColumnTypeNameClass
{
    GObjectClass parent;
};

GType                   donna_column_type_name_get_type (void) G_GNUC_CONST;

DonnaColumnType *       donna_column_type_name_new      (DonnaConfig *config);

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_NAME_H__ */
