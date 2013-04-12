
#ifndef __DONNA_COLUMNTYPE_PERMS_H__
#define __DONNA_COLUMNTYPE_PERMS_H__

#include "columntype.h"
#include "donna.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMNTYPE_PERMS              (donna_column_type_perms_get_type ())
#define DONNA_COLUMNTYPE_PERMS(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMNTYPE_PERMS, DonnaColumnTypePerms))
#define DONNA_COLUMNTYPE_PERMS_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMNTYPE_PERMS, DonnaColumnTypePermsClass))
#define DONNA_IS_COLUMNTYPE_PERMS(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMNTYPE_PERMS))
#define DONNA_IS_COLUMNTYPE_PERMS_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMNTYPE_PERMS))
#define DONNA_COLUMNTYPE_PERMS_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMNTYPE_PERMS, DonnaColumnTypePermsClass))

typedef struct _DonnaColumnTypePerms             DonnaColumnTypePerms;
typedef struct _DonnaColumnTypePermsClass        DonnaColumnTypePermsClass;
typedef struct _DonnaColumnTypePermsPrivate      DonnaColumnTypePermsPrivate;

struct _DonnaColumnTypePerms
{
    GObject parent;

    DonnaColumnTypePermsPrivate *priv;
};

struct _DonnaColumnTypePermsClass
{
    GObjectClass parent;
};

GType                   donna_column_type_perms_get_type (void) G_GNUC_CONST;

DonnaColumnType *       donna_column_type_perms_new      (DonnaApp *app);

G_END_DECLS

#endif /* __DONNA_COLUMNTYPE_PERMS_H__ */
