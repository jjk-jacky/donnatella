
#ifndef __DONNA_H__
#define __DONNA_H__

#include "conf.h"
#include "common.h"
#include "columntype.h"

G_BEGIN_DECLS

typedef DonnaColumnType *   (*column_type_loader_fn)    (DonnaConfig *config);

void        donna_init                      (int *argc, char **argv[]);
void        donna_free                      (void);

G_END_DECLS

#endif /* __DONNA_H__ */
