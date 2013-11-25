#define DONNA_ENABLE_DEBUG

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

#ifdef DONNA_ENABLE_DEBUG

#define DONNA_DEBUG(type,action)    do {         \
    if (donna_debug_flags & DONNA_DEBUG_##type)  \
    {                                            \
        action;                                  \
    }                                            \
} while (0)

/* shorthand for G_BREAKPOINT() but also takes a boolean, if TRUE it will ungrab
 * the mouse/keyboard, which allows one to actually switch to GDB and debug even
 * if say a menu was poped up and had grabbed things */
#define GDB(ungrab) do {                                                    \
    if (ungrab)                                                             \
    {                                                                       \
        GdkDeviceManager *devmngr;                                          \
        GList *list, *l;                                                    \
                                                                            \
        devmngr = gdk_display_get_device_manager (                          \
                gdk_display_get_default ());                                \
        list = gdk_device_manager_list_devices (devmngr,                    \
                GDK_DEVICE_TYPE_MASTER);                                    \
        for (l = list; l; l = l->next)                                      \
        {                                                                   \
            GdkDevice *dev = l->data;                                       \
                                                                            \
            if (gdk_device_get_source (dev) != GDK_SOURCE_MOUSE             \
                    && gdk_device_get_source (dev) != GDK_SOURCE_KEYBOARD)  \
                continue;                                                   \
                                                                            \
            gdk_device_ungrab (dev, GDK_CURRENT_TIME);                      \
        }                                                                   \
        g_list_free (list);                                                 \
    }                                                                       \
    G_BREAKPOINT();                                                         \
} while (0)

#else

#define DONNA_DEBUG(type, action)

#endif /* DONNA_ENABLE_DEBUG */

extern guint donna_debug_flags;

G_END_DECLS

#endif /* __DONNA_DEBUG_H__ */
