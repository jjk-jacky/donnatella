
#ifndef __DONNA_DEBUG_H__
#define __DONNA_DEBUG_H__

G_BEGIN_DECLS

typedef enum
{
    DONNA_DEBUG_NODE         = (1 << 0),
    DONNA_DEBUG_TASK         = (1 << 1),
    DONNA_DEBUG_TREEVIEW     = (1 << 2),
    DONNA_DEBUG_TASK_MANAGER = (1 << 3),
    DONNA_DEBUG_PROVIDER     = (1 << 4),
    DONNA_DEBUG_CONFIG       = (1 << 5),
    DONNA_DEBUG_APP          = (1 << 6),

    DONNA_DEBUG_ALL          = DONNA_DEBUG_NODE | DONNA_DEBUG_TASK
        | DONNA_DEBUG_TREEVIEW | DONNA_DEBUG_TASK_MANAGER
        | DONNA_DEBUG_PROVIDER | DONNA_DEBUG_CONFIG | DONNA_DEBUG_APP
} DonnaDebugFlags;

#ifdef DONNA_DEBUG_ENABLED

#define DONNA_DEBUG(type, name, action) do {                        \
    const gchar *n = name;                                          \
    if ((donna_debug_flags & DONNA_DEBUG_##type)                    \
            && (n == NULL                                           \
                || _donna_debug_is_valid (DONNA_DEBUG_##type, n)))  \
    {                                                               \
        action;                                                     \
    }                                                               \
} while (0)

/* shorthand for G_BREAKPOINT() but also takes a boolean, if TRUE it will ungrab
 * the mouse/keyboard, which allows one to actually switch to GDB and debug even
 * if say a menu was poped up and had grabbed things */
#define GDB(ungrab) do {        \
    if (ungrab)                 \
        _donna_debug_ungrab (); \
    G_BREAKPOINT ();            \
} while (0)

/* internal, used by GDB() */
void        _donna_debug_ungrab     (void);

/* internal, used by DONNA_DEBUG() */
gboolean    _donna_debug_is_valid   (DonnaDebugFlags flag, const gchar *name);

gboolean    donna_debug_set_valid   (gchar *def, GError **error);
void        donna_debug_reset_valid (void);



#else



#define DONNA_DEBUG(type, name, action)
#define GDB(ungrab)

#endif /* DONNA_DEBUG_ENABLED */

extern guint donna_debug_flags;

G_END_DECLS

#endif /* __DONNA_DEBUG_H__ */
