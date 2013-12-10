
#ifndef __DONNA_PROVIDER_EXEC_H__
#define __DONNA_PROVIDER_EXEC_H__

#include "provider-base.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_EXEC            (donna_provider_exec_get_type ())
#define DONNA_PROVIDER_EXEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_EXEC, DonnaProviderExec))
#define DONNA_PROVIDER_EXEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_EXEC, BonnaProviderExecClass))
#define DONNA_IS_PROVIDER_EXEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_EXEC))
#define DONNA_IS_PROVIDER_EXEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_EXEC))
#define DONNA_PROVIDER_EXEC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_EXEC, DonnaProviderExecClass))

typedef struct _DonnaProviderExec           DonnaProviderExec;
typedef struct _DonnaProviderExecClass      DonnaProviderExecClass;

struct _DonnaProviderExec
{
    DonnaProviderBase parent;
};

struct _DonnaProviderExecClass
{
    DonnaProviderBaseClass parent;
};

GType       donna_provider_exec_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_EXEC_H__ */
