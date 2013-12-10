
#ifndef __DONNA_PROVIDER_COMMAND_H__
#define __DONNA_PROVIDER_COMMAND_H__

#include "provider-base.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_COMMAND             (donna_provider_command_get_type ())
#define DONNA_PROVIDER_COMMAND(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_COMMAND, DonnaProviderCommand))
#define DONNA_PROVIDER_COMMAND_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_COMMAND, BonnaProviderCommandClass))
#define DONNA_IS_PROVIDER_COMMAND(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_COMMAND))
#define DONNA_IS_PROVIDER_COMMAND_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_COMMAND))
#define DONNA_PROVIDER_COMMAND_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_COMMAND, DonnaProviderCommandClass))

typedef struct _DonnaProviderCommand            DonnaProviderCommand;
typedef struct _DonnaProviderCommandClass       DonnaProviderCommandClass;
typedef struct _DonnaProviderCommandPrivate     DonnaProviderCommandPrivate;

#define DONNA_COMMAND_ERROR                     g_quark_from_static_string ("DonnaCommand-Error")
typedef enum
{
    DONNA_COMMAND_ALREADY_EXISTS,
    DONNA_COMMAND_ERROR_NOT_FOUND,
    DONNA_COMMAND_ERROR_SYNTAX,
    DONNA_COMMAND_ERROR_MISSING_ARG,
    DONNA_COMMAND_ERROR_OTHER,
} DonnaCommandError;


typedef DonnaTaskState (*command_fn)            (DonnaTask      *task,
                                                 DonnaApp       *app,
                                                 gpointer       *args,
                                                 gpointer        data);

struct _DonnaProviderCommand
{
    DonnaProviderBase parent;

    DonnaProviderCommandPrivate *priv;
};

struct _DonnaProviderCommandClass
{
    DonnaProviderBaseClass parent;
};

GType       donna_provider_command_get_type     (void) G_GNUC_CONST;

gboolean    donna_provider_command_add_command  (DonnaProviderCommand   *pc,
                                                 const gchar            *name,
                                                 guint                   argc,
                                                 DonnaArgType           *arg_type,
                                                 DonnaArgType            return_type,
                                                 DonnaTaskVisibility     visibility,
                                                 command_fn              func,
                                                 gpointer                data,
                                                 GDestroyNotify          destroy,
                                                 GError                **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_COMMAND_H__ */
