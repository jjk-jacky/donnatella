
#ifndef __DONNA_H__
#define __DONNA_H__

#include "conf.h"
#include "common.h"

G_BEGIN_DECLS

typedef DonnaProvider * (*get_provider_fn) (const gchar *domain);

typedef struct
{
    DonnaConfig * const   config;
    get_provider_fn const get_provider;
} Donna;


void        donna_start_internal_task       (DonnaTask  *task);

G_END_DECLS

#endif /* __DONNA_H__ */
