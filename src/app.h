
#ifndef __DONNA_H__
#define __DONNA_H__

#include "app.h"
#include "columntype.h"

G_BEGIN_DECLS

typedef struct _DonnaDonna              DonnaDonna;
typedef struct _DonnaDonnaClass         DonnaDonnaClass;
typedef struct _DonnaDonnaPrivate       DonnaDonnaPrivate;

#define DONNA_TYPE_DONNA                (donna_donna_get_type ())
#define DONNA_DONNA(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_DONNA, DonnaDonna))
#define DONNA_DONNA_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_DONNA, DonnaDonnaClass))
#define DONNA_IS_DONNA(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_DONNA))
#define DONNA_IS_DONNA_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_DONNA))
#define DONNA_DONNA_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_DONNA, DonnaDonnaClass))

struct _DonnaDonna
{
    GObject parent;

    DonnaDonnaPrivate *priv;
};

struct _DonnaDonnaClass
{
    GObjectClass parent;
};

GType               donna_donna_get_type            (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_H__ */
