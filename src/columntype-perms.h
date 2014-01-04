
#ifndef __DONNA_COLUMN_TYPE_PERMS_H__
#define __DONNA_COLUMN_TYPE_PERMS_H__

#include "columntype.h"
#include "donna.h"

G_BEGIN_DECLS

#define DONNA_TYPE_COLUMN_TYPE_PERMS            (donna_column_type_perms_get_type ())
#define DONNA_COLUMN_TYPE_PERMS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_COLUMN_TYPE_PERMS, DonnaColumnTypePerms))
#define DONNA_COLUMN_TYPE_PERMS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_COLUMN_TYPE_PERMS, DonnaColumnTypePermsClass))
#define DONNA_IS_COLUMN_TYPE_PERMS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_COLUMN_TYPE_PERMS))
#define DONNA_IS_COLUMN_TYPE_PERMS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_COLUMN_TYPE_PERMS))
#define DONNA_COLUMN_TYPE_PERMS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_COLUMN_TYPE_PERMS, DonnaColumnTypePermsClass))

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

G_END_DECLS

#endif /* __DONNA_COLUMN_TYPE_PERMS_H__ */
