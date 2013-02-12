
#ifndef __DONNA_CONFIG_H__
#define __DONNA_CONFIG_H__

#include "provider-config.h"

G_BEGIN_DECLS

#define DONNA_CONFIG(obj)       DONNA_PROVIDER_CONFIG (obj)
#define DONNA_IS_CONFIG(obj)    DONNA_IS_PROVIDER_CONFIG(obj)

typedef DonnaProviderConfig     DonnaConfig;

G_END_DECLS

#endif /* __DONNA_CONFIG_H__ */
