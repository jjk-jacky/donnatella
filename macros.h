
#ifndef __DONNA_MACROS_H__
#define __DONNA_MACROS_H__

G_BEGIN_DECLS

#include <glib.h>
#include <string.h>

/* somehow this one is missing in GLib */
#ifndef g_info
#define g_info(...)     g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __VA_ARGS__)
#endif

#define DONNA_LOG_DOMAIN    "Donnatella"
#define donna_error(...)    g_log (DONNA_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __VA_ARGS__)

#define streq(s1, s2)           (strcmp  ((s1), (s2)) == 0)
#define streqn(s1, s2, n)       (strncmp ((s1), (s2), (n)) == 0)

G_END_DECLS

#endif /* __DONNA_MACROS_H__ */

