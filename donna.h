
#ifndef __DONNA_H__
#define __DONNA_H__

#include "conf.h"
#include "common.h"
#include "columntype.h"
#include "sharedstring.h"

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

typedef DonnaColumnType *   (*column_type_loader_fn)    (DonnaDonna *donna);

typedef DonnaDonna                      DonnaApp;

#define DONNA_APP(obj)                  ((DonnaApp *) (obj))
#define DONNA_IS_APP(obj)               DONNA_IS_DONNA(obj)

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

DonnaConfig *       donna_app_get_config            (DonnaApp       *app);
DonnaProvider *     donna_app_get_provider          (DonnaApp       *app,
                                                     const gchar    *domain);
DonnaColumnType *   donna_app_get_columntype        (DonnaApp       *app,
                                                     const gchar    *type);
DonnaSharedString * donna_app_get_arrangement       (DonnaApp       *app,
                                                     DonnaNode      *node);
void                donna_app_run_task              (DonnaApp       *app,
                                                     DonnaTask      *task);

G_END_DECLS

#endif /* __DONNA_H__ */
