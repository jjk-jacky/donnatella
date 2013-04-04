#define DONNA_ENABLE_DEBUG

#ifndef __DONNA_DEBUG_H__
#define __DONNA_DEBUG_H__

G_BEGIN_DECLS

typedef enum
{
    DONNA_DEBUG_NODE        = (1 << 0),
    DONNA_DEBUG_TASK        = (1 << 1),

    DONNA_DEBUG_ALL         = DONNA_DEBUG_NODE | DONNA_DEBUG_TASK
} DonnaDebugFlags;

#ifdef DONNA_ENABLE_DEBUG

#define DONNA_DEBUG(type,action)    do {         \
    if (donna_debug_flags & DONNA_DEBUG_##type)  \
    {                                            \
        action;                                  \
    }                                            \
} while (0)

#else

#define DONNA_DEBUG(type, action)

#endif /* DONNA_ENABLE_DEBUG */

extern guint donna_debug_flags;

G_END_DECLS

#endif /* __DONNA_DEBUG_H__ */
