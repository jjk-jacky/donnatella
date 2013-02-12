
#ifndef __DONNA_MACROS_H__
#define __DONNA_MACROS_H__

G_BEGIN_DECLS

#include <string.h>


#define streq(s1, s2)           (strcmp  ((s1), (s2)) == 0)
#define streqn(s1, s2, n)       (strncmp ((s1), (s2), (n)) == 0)

G_END_DECLS

#endif /* __DONNA_MACROS_H__ */

